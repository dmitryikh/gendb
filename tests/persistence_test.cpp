#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include "account.gendb.h"
#include "config.gendb.h"
#include "database.h"
#include "metadata.gendb.h"
#include "position.gendb.h"
#include "session.gendb.h"

using namespace gendb::tests;

using gendb::MetadataType;

class PersistentDbTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    _path =
        (std::filesystem::temp_directory_path() / (std::string("gendb_") + info->name())).string();
    std::filesystem::remove_all(_path);
  }
  void TearDown() override { std::filesystem::remove_all(_path); }

  std::unique_ptr<Db> OpenDb() {
    auto db = Db::Open({.path = _path, .sync_commits = true});
    EXPECT_TRUE(db.ok()) << db.status();
    return db.ok() ? std::move(*db) : nullptr;
  }

  std::string _path;
};

TEST_F(PersistentDbTest, StateAndIndicesSurviveReopen) {
  {
    auto db = OpenDb();
    auto writer = db->CreateWriter();
    AccountBuilder builder;
    builder.set_account_id(1).set_name("Heidi").set_age(30);
    builder.AddStringField(1000, "from a newer schema");
    ASSERT_TRUE(writer.PutAccount(builder.Build()).ok());
    ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(2).set_age(30).Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
    ASSERT_TRUE(writer.UpdateAccount(1, AccountPatchBuilder().set_name("Heidi Klum").Build()).ok());
    ASSERT_TRUE(writer.DeleteAccount(2).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  auto db = OpenDb();
  EXPECT_EQ(db->engine().Stats().committed_seq, 2u);
  auto snapshot = db->CreateSnapshot();
  Account account;
  ASSERT_TRUE(snapshot.GetAccount(1, account).ok());
  EXPECT_EQ(account.name(), "Heidi Klum");
  EXPECT_EQ(account.View().GetString(1000), "from a newer schema");
  EXPECT_FALSE(snapshot.GetAccount(2, account).ok());

  auto by_age = snapshot.GetAccountByAgeEqual(30);
  ASSERT_TRUE(by_age.Valid());
  EXPECT_EQ(by_age.Value().account_id(), 1u);
  by_age.Next();
  EXPECT_FALSE(by_age.Valid());
}

TEST_F(PersistentDbTest, SequencesAreDurable) {
  uint64_t first = 0;
  {
    auto db = OpenDb();
    auto writer = db->CreateWriter();
    ASSERT_TRUE(writer.NextAccountIdSequence(first).ok());
    ASSERT_TRUE(writer.NextAccountIdSequence(first).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  auto db = OpenDb();
  auto writer = db->CreateWriter();
  uint64_t next = 0;
  ASSERT_TRUE(writer.NextAccountIdSequence(next).ok());
  EXPECT_EQ(next, first + 1);
}

TEST_F(PersistentDbTest, EphemeralCollectionsAreNotPersisted) {
  {
    auto db = OpenDb();
    auto writer = db->CreateWriter();
    ASSERT_TRUE(writer.PutSession(SessionBuilder().set_session_id(1).Build()).ok());
    ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(1).Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
    Session session;
    EXPECT_TRUE(db->CreateSnapshot().GetSession(1, session).ok());
  }
  auto db = OpenDb();
  auto snapshot = db->CreateSnapshot();
  Session session;
  Account account;
  EXPECT_FALSE(snapshot.GetSession(1, session).ok());
  EXPECT_TRUE(snapshot.GetAccount(1, account).ok());
}

// A real crash: the child process dies without closing anything, mid-transaction.
TEST_F(PersistentDbTest, CrashKeepsCommittedAndDropsUncommitted) {
#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
  GTEST_SKIP() << "ThreadSanitizer does not support starting threads in a forked child.";
#endif
#endif
  const pid_t child = fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    auto db = OpenDb();
    auto writer = db->CreateWriter();
    (void)writer.PutAccount(AccountBuilder().set_account_id(1).set_age(1).Build());
    (void)writer.PutAccount(AccountBuilder().set_account_id(2).set_age(2).Build());
    if (!writer.Commit().ok()) _exit(1);
    (void)writer.PutAccount(AccountBuilder().set_account_id(3).Build());
    _exit(0);  // Uncommitted account 3 dies here; no destructor runs.
  }
  int status = 0;
  ASSERT_EQ(waitpid(child, &status, 0), child);
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);

  auto db = OpenDb();
  auto snapshot = db->CreateSnapshot();
  Account account;
  EXPECT_TRUE(snapshot.GetAccount(1, account).ok());
  EXPECT_TRUE(snapshot.GetAccount(2, account).ok());
  EXPECT_FALSE(snapshot.GetAccount(3, account).ok());
}

TEST_F(PersistentDbTest, CheckpointRemovesTheLogItSupersedes) {
  gendb::EngineOptions options{.path = _path, .wal_segment_bytes = 256, .wal_retain_bytes = 0};
  {
    auto db = Db::Open(options);
    ASSERT_TRUE(db.ok()) << db.status();
    {
      auto writer = (*db)->CreateWriter();
      for (uint64_t id = 0; id < 100; ++id) {
        ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(id).set_age(7).Build()).ok());
        ASSERT_TRUE(writer.Commit().ok());
      }
      ASSERT_TRUE(writer.DeleteAccount(5).ok());
      ASSERT_TRUE(writer.Commit().ok());
    }
    const uint64_t before = (*db)->engine().Stats().wal_bytes;
    ASSERT_TRUE((*db)->engine().Checkpoint().ok());
    const gendb::EngineStats stats = (*db)->engine().Stats();
    EXPECT_EQ(stats.checkpoint_lsn, 101u);
    EXPECT_GT(stats.checkpoint_bytes, 0u);
    EXPECT_LT(stats.wal_bytes, before / 4);

    // Commits after the checkpoint come from the log on the next open.
    auto next = (*db)->CreateWriter();
    ASSERT_TRUE(next.PutAccount(AccountBuilder().set_account_id(1000).set_age(7).Build()).ok());
    ASSERT_TRUE(next.Commit().ok());
  }
  size_t checkpoints = 0;
  for (const auto& entry : std::filesystem::directory_iterator(_path)) {
    if (entry.path().filename().string().starts_with("checkpoint-")) ++checkpoints;
  }
  EXPECT_EQ(checkpoints, 1u);

  auto db = Db::Open(options);
  ASSERT_TRUE(db.ok()) << db.status();
  EXPECT_EQ((*db)->engine().Stats().lsn, 102u);
  auto snapshot = (*db)->CreateSnapshot();
  Account account;
  EXPECT_TRUE(snapshot.GetAccount(1000, account).ok());
  EXPECT_TRUE(snapshot.GetAccount(99, account).ok());
  EXPECT_FALSE(snapshot.GetAccount(5, account).ok());
  size_t rows = 0;
  for (auto it = snapshot.GetAccountByAgeEqual(7); it.Valid(); it.Next()) ++rows;
  EXPECT_EQ(rows, 100u);
}

TEST_F(PersistentDbTest, BackgroundCheckpointRunsWhileWriting) {
  auto db = Db::Open({.path = _path,
                      .sync_commits = false,
                      .wal_segment_bytes = 4096,
                      .checkpoint_wal_bytes = 16384,
                      .wal_retain_bytes = 0});
  ASSERT_TRUE(db.ok()) << db.status();
  for (uint64_t id = 0; id < 5000; ++id) {
    auto writer = (*db)->CreateWriter();
    ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(id).Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  for (int i = 0; i < 1000 && (*db)->engine().Stats().checkpoint_lsn == 0; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  const gendb::EngineStats stats = (*db)->engine().Stats();
  EXPECT_GT(stats.checkpoint_lsn, 0u);
  EXPECT_TRUE(stats.background_error.ok()) << stats.background_error;
  ASSERT_TRUE((*db)->engine().Checkpoint().ok());
  EXPECT_LT((*db)->engine().Stats().wal_bytes, 16384u);
}

// Every prefix of the log is a valid state: a torn tail loses only the commit it cuts.
TEST_F(PersistentDbTest, TornTailIsCutOffAtTheLastIntactCommit) {
  {
    auto db = OpenDb();
    for (uint64_t id = 1; id <= 3; ++id) {
      auto writer = db->CreateWriter();
      ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(id).Build()).ok());
      ASSERT_TRUE(writer.Commit().ok());
    }
  }
  std::filesystem::path newest;
  for (const auto& entry : std::filesystem::directory_iterator(_path)) {
    const std::string name = entry.path().filename().string();
    if (name.starts_with("wal-") && entry.file_size() > 200 &&
        (newest.empty() || entry.path() > newest)) {
      newest = entry.path();
    }
  }
  ASSERT_FALSE(newest.empty());
  // Past the sync mark of the clean shutdown, and into the last commit.
  std::filesystem::resize_file(newest, std::filesystem::file_size(newest) - 20);
  {
    auto db = OpenDb();
    ASSERT_NE(db, nullptr);
    EXPECT_EQ(db->engine().Stats().lsn, 2u);
    auto snapshot = db->CreateSnapshot();
    Account account;
    EXPECT_TRUE(snapshot.GetAccount(2, account).ok());
    EXPECT_FALSE(snapshot.GetAccount(3, account).ok());
    // The log continues cleanly from the cut.
    auto writer = db->CreateWriter();
    ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(4).Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  auto db = OpenDb();
  ASSERT_NE(db, nullptr);
  EXPECT_EQ(db->engine().Stats().lsn, 3u);
}

// Logged size of what a function commits.
template <typename F>
uint64_t LogGrowth(Db& db, F commit) {
  const uint64_t before = db.engine().Stats().wal_bytes;
  commit();
  return db.engine().Stats().wal_bytes - before;
}

TEST_F(PersistentDbTest, UpdatesAreLoggedAsPatchesWhenThatIsSmaller) {
  auto db = OpenDb();
  const std::string long_name(1000, 'n');
  const auto put = [&] {
    auto writer = db->CreateWriter();
    ASSERT_TRUE(
        writer.PutAccount(AccountBuilder().set_account_id(1).set_name(long_name).Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
  };
  const uint64_t whole = LogGrowth(*db, put);
  EXPECT_GT(whole, 1000u);

  const uint64_t patch = LogGrowth(*db, [&] {
    auto writer = db->CreateWriter();
    ASSERT_TRUE(writer.UpdateAccount(1, AccountPatchBuilder().set_age(1).Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
  });
  EXPECT_LT(patch, 60u);

  // Several updates of one record in a transaction make one patch.
  const uint64_t merged = LogGrowth(*db, [&] {
    auto writer = db->CreateWriter();
    ASSERT_TRUE(writer.UpdateAccount(1, AccountPatchBuilder().set_age(2).Build()).ok());
    ASSERT_TRUE(writer.UpdateAccount(1, AccountPatchBuilder().set_age(3).Build()).ok());
    ASSERT_TRUE(writer.UpdateAccount(1, AccountPatchBuilder().set_balance(1.f).Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
  });
  EXPECT_LT(merged, 70u);

  // A record new in the transaction has no committed state to patch.
  const uint64_t new_record = LogGrowth(*db, [&] {
    auto writer = db->CreateWriter();
    ASSERT_TRUE(
        writer.PutAccount(AccountBuilder().set_account_id(2).set_name(long_name).Build()).ok());
    ASSERT_TRUE(writer.UpdateAccount(2, AccountPatchBuilder().set_age(1).Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
  });
  EXPECT_GT(new_record, 1000u);

  // A patch larger than the record is not worth it.
  const uint64_t rewritten = LogGrowth(*db, [&] {
    auto writer = db->CreateWriter();
    ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(3).Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
  });
  const uint64_t larger = LogGrowth(*db, [&] {
    auto writer = db->CreateWriter();
    ASSERT_TRUE(writer.UpdateAccount(3, AccountPatchBuilder().set_trader_id("t").Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
  });
  EXPECT_LT(larger, rewritten + 16);
}

// Every way of changing a record, recovered from the log, must give exactly the same bytes.
TEST_F(PersistentDbTest, RecoveryFromPatchesGivesExactlyTheSameBytes) {
  const auto bytes_of = [](Db& db, uint64_t id) {
    const gendb::EngineSnapshot snapshot(db.engine());
    const auto value = snapshot.Get(AccountCollId, ToAccountKey(id));
    return value.has_value() ? std::vector<uint8_t>(value->begin(), value->end())
                             : std::vector<uint8_t>{};
  };
  std::vector<std::vector<uint8_t>> expected;
  {
    auto db = OpenDb();
    auto writer = db->CreateWriter();
    AccountBuilder unknown;
    unknown.set_account_id(1).set_name("Ann").set_age(30).set_balance(1.f);
    unknown.AddStringField(1000, "from a newer schema");
    ASSERT_TRUE(writer.PutAccount(unknown.Build()).ok());
    ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(2).set_age(1).Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());

    ASSERT_TRUE(
        writer.UpdateAccount(1, AccountPatchBuilder().set_age(31).Build()).ok());  // In place.
    ASSERT_TRUE(writer.Commit().ok());
    ASSERT_TRUE(writer.UpdateAccount(1, AccountPatchBuilder().set_name("Annabelle").Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
    ASSERT_TRUE(writer.UpdateAccount(1, AccountPatchBuilder().clear_balance().Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
    // In one transaction: a change of size, then in place, then undoing a removal.
    ASSERT_TRUE(writer.UpdateAccount(1, AccountPatchBuilder().set_name("Bo").Build()).ok());
    ASSERT_TRUE(
        writer.UpdateAccount(1, AccountPatchBuilder().set_age(32).clear_is_active().Build()).ok());
    ASSERT_TRUE(writer.UpdateAccount(1, AccountPatchBuilder().set_balance(2.f).Build()).ok());
    ASSERT_TRUE(writer.UpdateAccount(2, AccountPatchBuilder().set_age(2).Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
    // A patch whose index key moves.
    ASSERT_TRUE(writer.UpdateAccount(2, AccountPatchBuilder().set_age(40).Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());

    Account account;
    ASSERT_TRUE(writer.GetAccount(1, account).ok());
    EXPECT_EQ(account.name(), "Bo");
    EXPECT_EQ(account.age(), 32);
    EXPECT_EQ(account.balance(), 2.f);
    EXPECT_EQ(account.View().GetString(1000), "from a newer schema");
    expected = {bytes_of(*db, 1), bytes_of(*db, 2)};
  }
  {
    // First with the patches replayed from the log, then, since a checkpoint holds whole records,
    // from the checkpoint and then a further patch.
    auto db = OpenDb();
    EXPECT_EQ(bytes_of(*db, 1), expected[0]);
    EXPECT_EQ(bytes_of(*db, 2), expected[1]);
    Account account;
    auto snapshot = db->CreateSnapshot();
    ASSERT_TRUE(snapshot.GetAccount(1, account).ok());
    EXPECT_EQ(account.name(), "Bo");
    EXPECT_EQ(account.age(), 32);
    EXPECT_EQ(account.balance(), 2.f);
    EXPECT_EQ(account.View().GetString(1000), "from a newer schema");
    auto by_age = snapshot.GetAccountByAgeEqual(40);
    ASSERT_TRUE(by_age.Valid());
    EXPECT_EQ(by_age.Value().account_id(), 2u);
    EXPECT_FALSE(snapshot.GetAccountByAgeEqual(2).Valid());
    ASSERT_TRUE(db->engine().Checkpoint().ok());
    auto writer = db->CreateWriter();
    ASSERT_TRUE(writer.UpdateAccount(1, AccountPatchBuilder().set_age(33).Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  auto db = OpenDb();
  ASSERT_NE(db, nullptr);
  EXPECT_EQ(bytes_of(*db, 2), expected[1]);
  Account account;
  ASSERT_TRUE(db->CreateSnapshot().GetAccount(1, account).ok());
  EXPECT_EQ(account.age(), 33);
}

// Cuts the log at every byte: recovery must give exactly the commits that are whole.
TEST_F(PersistentDbTest, LogCutAtAnyByteRecoversTheCommitsBeforeIt) {
  std::vector<uint64_t> commit_ends;
  {
    auto db = OpenDb();
    commit_ends.push_back(db->engine().Stats().wal_bytes);
    for (uint64_t id = 1; id <= 5; ++id) {
      auto writer = db->CreateWriter();
      ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(id).Build()).ok());
      ASSERT_TRUE(writer.Commit().ok());
      commit_ends.push_back(db->engine().Stats().wal_bytes);
    }
  }
  const std::string segment = _path + "/wal-00000000000000000001.log";
  const std::string backup = _path + ".backup";
  std::filesystem::copy_file(segment, backup, std::filesystem::copy_options::overwrite_existing);
  const std::vector<uint64_t> ends = commit_ends;
  for (uint64_t cut = 0; cut <= ends.back(); ++cut) {
    std::filesystem::remove_all(_path);
    std::filesystem::create_directories(_path);
    std::filesystem::copy_file(backup, segment);
    std::filesystem::resize_file(segment, cut);

    uint64_t expected = 0;
    for (size_t i = 1; i < ends.size(); ++i) {
      if (ends[i] <= cut) expected = i;
    }
    auto db = OpenDb();
    ASSERT_NE(db, nullptr) << "cut at " << cut;
    ASSERT_EQ(db->engine().Stats().lsn, expected) << "cut at " << cut;
    size_t rows = 0;
    auto snapshot = db->CreateSnapshot();
    for (auto it = snapshot.ScanAccount(); it.Valid(); it.Next()) ++rows;
    ASSERT_EQ(rows, expected) << "cut at " << cut;
  }
  std::filesystem::remove(backup);
}

TEST_F(PersistentDbTest, DamagedCheckpointIsReportedNotHidden) {
  {
    auto db = OpenDb();
    {
      auto writer = db->CreateWriter();
      ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(1).Build()).ok());
      ASSERT_TRUE(writer.Commit().ok());
    }
    ASSERT_TRUE(db->engine().Checkpoint().ok());
  }
  const std::string file = _path + "/checkpoint-00000000000000000001.gdb";
  std::fstream stream(file, std::ios::in | std::ios::out | std::ios::binary);
  stream.seekp(static_cast<std::streamoff>(std::filesystem::file_size(file) / 2));
  stream.put('\x5a');
  stream.close();
  EXPECT_EQ(Db::Open({.path = _path}).status().code(), absl::StatusCode::kDataLoss);
}

// Kills a real process over and over, in the middle of commits, log rotation and background
// checkpoints. A kill loses nothing the OS has, so every acknowledged commit must survive, whole.
TEST_F(PersistentDbTest, KillAtAnyMomentKeepsEveryAcknowledgedCommit) {
#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
  GTEST_SKIP() << "ThreadSanitizer does not support starting threads in a forked child.";
#endif
#endif
  const gendb::EngineOptions options{.path = _path,
                                     .sync_commits = false,
                                     .wal_segment_bytes = 2048,
                                     .checkpoint_wal_bytes = 8192,
                                     .wal_retain_bytes = 0};
  const std::string padding(200, 'p');
  int32_t acknowledged = 0;
  for (int round = 0; round < 8; ++round) {
    int fds[2];
    ASSERT_EQ(pipe(fds), 0);
    const pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
      close(fds[0]);
      auto db = Db::Open(options);
      if (!db.ok()) _exit(1);
      int32_t next = 0;
      {
        Account account;
        if ((*db)->CreateSnapshot().GetAccount(1, account).ok()) next = account.age();
      }
      for (;;) {
        ++next;
        auto writer = (*db)->CreateWriter();
        for (uint64_t id : {uint64_t{1}, uint64_t{2}}) {
          (void)writer.PutAccount(
              AccountBuilder().set_account_id(id).set_age(next).set_name(padding).Build());
        }
        if (!writer.Commit().ok()) _exit(2);
        if (write(fds[1], &next, sizeof(next)) != sizeof(next)) _exit(3);
      }
    }
    close(fds[1]);
    std::this_thread::sleep_for(std::chrono::milliseconds(5 + round * 7));
    ASSERT_EQ(kill(child, SIGKILL), 0);
    int32_t value;
    while (read(fds[0], &value, sizeof(value)) == sizeof(value)) acknowledged = value;
    close(fds[0]);
    int status = 0;
    ASSERT_EQ(waitpid(child, &status, 0), child);
    ASSERT_TRUE(WIFSIGNALED(status)) << "the child exited with " << WEXITSTATUS(status);

    auto db = Db::Open(options);
    ASSERT_TRUE(db.ok()) << "round " << round << ": " << db.status();
    auto snapshot = (*db)->CreateSnapshot();
    Account first, second;
    if (acknowledged > 0) {
      ASSERT_TRUE(snapshot.GetAccount(1, first).ok());
      ASSERT_TRUE(snapshot.GetAccount(2, second).ok());
      EXPECT_GE(first.age(), acknowledged);
      EXPECT_EQ(first.age(), second.age());  // Commits are whole.
      acknowledged = first.age();
    }
  }
  EXPECT_GT(acknowledged, 0);
}

TEST_F(PersistentDbTest, DamageBeforeTheTailIsReportedNotHidden) {
  {
    auto db = Db::Open({.path = _path, .wal_segment_bytes = 200});
    ASSERT_TRUE(db.ok());
    for (uint64_t id = 1; id <= 10; ++id) {
      auto writer = (*db)->CreateWriter();
      ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(id).Build()).ok());
      ASSERT_TRUE(writer.Commit().ok());
    }
  }
  std::vector<std::filesystem::path> segments;
  for (const auto& entry : std::filesystem::directory_iterator(_path)) {
    if (entry.path().filename().string().starts_with("wal-")) segments.push_back(entry.path());
  }
  std::sort(segments.begin(), segments.end());
  ASSERT_GE(segments.size(), 2u);
  std::filesystem::resize_file(segments[0], std::filesystem::file_size(segments[0]) - 1);
  auto db = Db::Open({.path = _path});
  EXPECT_EQ(db.status().code(), absl::StatusCode::kDataLoss) << db.status();
}

TEST_F(PersistentDbTest, OneProcessAtATime) {
  auto db = OpenDb();
  auto second = Db::Open({.path = _path});
  EXPECT_EQ(second.status().code(), absl::StatusCode::kFailedPrecondition);
}

// A database that holds a collection the schema lost keeps it, as when rolling back a deployment.
TEST_F(PersistentDbTest, CollectionMissingFromTheSchemaIsKeptUntilDiscarded) {
  gendb::EngineSchema full;
  full.collections.push_back({"kept"});
  full.collections.push_back({"removed_later"});
  gendb::EngineSchema reduced;
  reduced.collections.push_back({"kept"});
  const std::array<uint8_t, 1> key{1};
  const std::array<uint8_t, 1> value{2};
  const auto has_record = [&key](gendb::Engine& engine) {
    return gendb::EngineSnapshot(engine).Get(1, key).has_value();
  };
  {
    auto engine = gendb::Engine::Open(full, {.path = _path});
    ASSERT_TRUE(engine.ok());
    gendb::Transaction txn(**engine);
    ASSERT_TRUE(txn.Put(0, key, value).ok());
    ASSERT_TRUE(txn.Put(1, key, value).ok());
    ASSERT_TRUE(txn.Commit().ok());
  }
  // It survives in the log, then in a checkpoint, and with commits on top of it.
  for (bool checkpoint : {false, true}) {
    auto engine = gendb::Engine::Open(reduced, {.path = _path});
    ASSERT_TRUE(engine.ok()) << engine.status();
    EXPECT_THAT((*engine)->Stats().foreign_collections, ::testing::ElementsAre("removed_later"));
    {
      gendb::Transaction txn(**engine);
      ASSERT_TRUE(txn.Put(0, key, value).ok());
      ASSERT_TRUE(txn.Commit().ok());
    }
    if (checkpoint) ASSERT_TRUE((*engine)->Checkpoint().ok());
  }
  {
    auto engine = gendb::Engine::Open(full, {.path = _path});
    ASSERT_TRUE(engine.ok()) << engine.status();
    EXPECT_TRUE((*engine)->Stats().foreign_collections.empty());
    EXPECT_TRUE(has_record(**engine));
  }

  // Dropping it takes a decision, and a checkpoint, with no new commits needed.
  EXPECT_EQ(gendb::Engine::Open(
                reduced, {.path = _path, .replica = true, .discard_unknown_collections = true})
                .status()
                .code(),
            absl::StatusCode::kInvalidArgument);
  {
    auto engine =
        gendb::Engine::Open(reduced, {.path = _path, .discard_unknown_collections = true});
    ASSERT_TRUE(engine.ok()) << engine.status();
    EXPECT_TRUE((*engine)->Stats().foreign_collections.empty());
    ASSERT_TRUE((*engine)->Checkpoint().ok());
  }
  auto engine = gendb::Engine::Open(full, {.path = _path, .wal_retain_bytes = 0});
  ASSERT_TRUE(engine.ok()) << engine.status();
  EXPECT_FALSE(has_record(**engine));
}

TEST_F(PersistentDbTest, BackupRestoresTheStateAsOfThen) {
  const std::string backup_path = _path + ".backup";
  std::filesystem::remove_all(backup_path);
  {
    auto db = OpenDb();
    {
      auto writer = db->CreateWriter();
      ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(1).set_age(7).Build()).ok());
      ASSERT_TRUE(writer.Commit().ok());
    }
    const auto info = db->engine().Backup(backup_path);
    ASSERT_TRUE(info.ok()) << info.status();
    EXPECT_EQ(info->lsn, 1u);
    EXPECT_GT(info->bytes, 0u);
    EXPECT_EQ(db->engine().Backup(backup_path).status().code(), absl::StatusCode::kAlreadyExists);

    auto later = db->CreateWriter();
    ASSERT_TRUE(later.PutAccount(AccountBuilder().set_account_id(2).Build()).ok());
    ASSERT_TRUE(later.Commit().ok());
  }
  {
    auto restored = Db::Open({.path = backup_path});
    ASSERT_TRUE(restored.ok()) << restored.status();
    EXPECT_EQ((*restored)->engine().Stats().lsn, 1u);
    auto snapshot = (*restored)->CreateSnapshot();
    Account account;
    EXPECT_TRUE(snapshot.GetAccount(1, account).ok());
    EXPECT_FALSE(snapshot.GetAccount(2, account).ok());
    EXPECT_TRUE(snapshot.GetAccountByAgeEqual(7).Valid());
  }
  std::filesystem::remove_all(backup_path);
}

TEST(BackupTest, AMemoryOnlyDatabaseCanBeBackedUpAndOpened) {
  const std::string path =
      (std::filesystem::temp_directory_path() / "gendb_memory_backup").string();
  std::filesystem::remove_all(path);
  Db db;
  {
    auto writer = db.CreateWriter();
    ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(1).Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  ASSERT_TRUE(db.engine().Backup(path).ok());
  auto restored = Db::Open({.path = path});
  ASSERT_TRUE(restored.ok()) << restored.status();
  Account account;
  EXPECT_TRUE((*restored)->CreateSnapshot().GetAccount(1, account).ok());
  restored->reset();
  std::filesystem::remove_all(path);
}

// Indices are built in one go on open, on several threads, and must be what inserting one by one
// would give: in order, complete, and working for later changes.
TEST_F(PersistentDbTest, IndicesBuiltOnOpenMatchTheRecords) {
  constexpr int kAccounts = 100'000;
  const auto age_of = [](uint64_t id) { return static_cast<int32_t>(id * 7919 % 100); };
  {
    auto db = Db::Open({.path = _path, .sync_commits = false});
    ASSERT_TRUE(db.ok());
    {
      auto writer = (*db)->CreateWriter();
      for (uint64_t id = 1; id <= kAccounts; ++id) {
        ASSERT_TRUE(
            writer.PutAccount(AccountBuilder().set_account_id(id).set_age(age_of(id)).Build())
                .ok());
      }
      ASSERT_TRUE(writer.Commit().ok());
    }
    ASSERT_TRUE((*db)->engine().Checkpoint().ok());
    // In the log, on top of the checkpoint: moved within the index, and gone from it.
    auto writer = (*db)->CreateWriter();
    ASSERT_TRUE(writer.UpdateAccount(1, AccountPatchBuilder().set_age(500).Build()).ok());
    ASSERT_TRUE(writer.DeleteAccount(2).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  for (unsigned threads : {1u, 3u, 8u}) {
    auto db = Db::Open({.path = _path, .recovery_threads = threads});
    ASSERT_TRUE(db.ok()) << db.status();
    auto snapshot = (*db)->CreateSnapshot();
    int rows = 0;
    int32_t previous_age = -1;
    uint64_t previous_id = 0;
    for (auto it = snapshot.GetAccountByAgeRange(std::nullopt, std::nullopt); it.Valid();
         it.Next()) {
      const int32_t age = it.Value().age();
      const uint64_t id = it.Value().account_id();
      ASSERT_TRUE(age > previous_age || (age == previous_age && id > previous_id));
      if (id != 3) ASSERT_EQ(age, id == 1 ? 500 : age_of(id));  // Each round moves account 3.
      previous_age = age;
      previous_id = id;
      ++rows;
    }
    EXPECT_EQ(rows, kAccounts - 1);
    EXPECT_FALSE(snapshot.GetAccountByAgeEqual(age_of(2)).Value().account_id() == 2u);
    // The index keeps working after being built this way.
    auto writer = (*db)->CreateWriter();
    ASSERT_TRUE(
        writer.UpdateAccount(3, AccountPatchBuilder().set_age(600 + int(threads)).Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
    auto later = (*db)->CreateSnapshot();
    EXPECT_TRUE(later.GetAccountByAgeEqual(600 + int(threads)).Valid());
  }
}
