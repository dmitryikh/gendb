// What the storage promises when things go wrong, against a file system that keeps only what a disk
// promises: every crash point of a workload, in every way power can fail, and failed operations.
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "account.fbs.h"
#include "database.h"
#include "gendb/replication.h"
#include "lib/loopback_transport.h"
#include "lib/memory_file_system.h"

namespace {

using gendb::testing::LoopbackServer;
using gendb::testing::MemoryFileSystem;
using gendb::tests::Account;
using gendb::tests::AccountBuilder;
using gendb::tests::AccountPatchBuilder;
using gendb::tests::Db;
using gendb::tests::DbOptions;
using Loss = MemoryFileSystem::Loss;

constexpr char kPath[] = "/db";
constexpr auto kPatience = std::chrono::seconds(30);
#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define GENDB_TSAN 1
#endif
#endif
// Each attempt starts threads, which ThreadSanitizer makes slow.
#ifdef GENDB_TSAN
constexpr uint32_t kInstallCrashAttempts = 1;
#else
constexpr uint32_t kInstallCrashAttempts = 6;
#endif
using State = std::map<uint64_t, int32_t>;  // Account ID to age.

DbOptions Options(const std::shared_ptr<MemoryFileSystem>& file_system) {
  return {.path = kPath,
          .file_system = file_system,
          .wal_segment_bytes = 256,   // Rotates often.
          .checkpoint_wal_bytes = 0,  // Checkpoints only where the workload asks.
          .wal_retain_bytes = 0};
}

// Runs commits of each kind, durable and not, with checkpoints between them, and stops at the first
// failure. The states of all commits are kept, to check any recovered position against.
struct Workload {
  std::vector<State> states{State{}};  // By LSN.
  uint64_t acknowledged_durable = 0;   // Commits that must survive anything.
  int checkpoint_every = 7;

  void Run(Db& db, int commits) {
    State state;
    for (int i = 1; i <= commits; ++i) {
      if (checkpoint_every > 0 && i % checkpoint_every == 0 && !db.engine().Checkpoint().ok()) {
        return;
      }
      auto writer = db.CreateWriter();
      const uint64_t id = uint64_t(i) % 5;
      const bool sync = i % 3 == 0;
      // All of these either do the same to `state` or the commit fails, and the workload ends.
      if (!state.contains(id)) {
        if (!writer.PutAccount(AccountBuilder().set_account_id(id).set_age(i).Build()).ok()) return;
        state[id] = i;
      } else if (i % 4 == 0) {
        if (!writer.DeleteAccount(id).ok()) return;
        state.erase(id);
      } else {
        if (!writer.UpdateAccount(id, AccountPatchBuilder().set_age(i).Build()).ok()) return;
        state[id] = i;
      }
      // What it commits may survive even if it reports failure.
      states.push_back(state);
      if (!writer.Commit({.sync = sync}).ok()) return;
      if (sync) acknowledged_durable = states.size() - 1;
    }
  }
};

State ReadState(Db& db) {
  State state;
  auto snapshot = db.CreateSnapshot();
  for (auto it = snapshot.ScanAccount(); it.Valid(); it.Next()) {
    state[it.Value().account_id()] = it.Value().age();
  }
  return state;
}

// Whatever was recovered must be exactly one commit's state, no older than what was promised, with
// indices that agree, and the database must keep working.
void CheckRecovered(const std::shared_ptr<MemoryFileSystem>& file_system, const Workload& workload,
                    const std::string& where) {
  SCOPED_TRACE(where);
  uint64_t lsn;
  {
    auto db = Db::Open(Options(file_system));
    ASSERT_TRUE(db.ok()) << db.status();
    lsn = (*db)->engine().Stats().lsn;
    ASSERT_GE(lsn, workload.acknowledged_durable);
    ASSERT_LT(lsn, workload.states.size());
    const State& expected = workload.states[lsn];
    ASSERT_EQ(ReadState(**db), expected);
    auto snapshot = (*db)->CreateSnapshot();
    for (const auto& [id, age] : expected) {
      bool found = false;
      for (auto it = snapshot.GetAccountByAgeEqual(age); it.Valid(); it.Next()) {
        found = found || it.Value().account_id() == id;
      }
      ASSERT_TRUE(found) << "Account " << id << " is not in the index";
    }
    auto writer = (*db)->CreateWriter();
    ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(100).set_age(100).Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  // A clean restart after that recovery.
  auto db = Db::Open(Options(file_system));
  ASSERT_TRUE(db.ok()) << db.status();
  ASSERT_EQ((*db)->engine().Stats().lsn, lsn + 1);
}

TEST(FaultInjectionTest, PowerLossAtEveryOperationKeepsThePromises) {
  constexpr int kCommits = 40;
  uint64_t total_operations;
  {
    auto file_system = std::make_shared<MemoryFileSystem>();
    auto db = Db::Open(Options(file_system));
    ASSERT_TRUE(db.ok()) << db.status();
    Workload workload;
    workload.Run(**db, kCommits);
    ASSERT_EQ(workload.states.size(), kCommits + 1u);
    total_operations = file_system->operations();
  }
  ASSERT_GT(total_operations, 100u);

  for (uint64_t at = 0; at <= total_operations; ++at) {
    for (Loss loss : {Loss::kNothing, Loss::kAllUnsynced, Loss::kPartOfUnsynced}) {
      auto file_system = std::make_shared<MemoryFileSystem>();
      file_system->CrashAfter(at);
      Workload workload;
      {
        auto db = Db::Open(Options(file_system));
        if (db.ok()) workload.Run(**db, kCommits);
      }
      file_system->PowerLoss(loss, static_cast<uint32_t>(at));
      CheckRecovered(file_system, workload,
                     absl::StrCat("Crash at operation ", at, ", loss ", static_cast<int>(loss)));
      if (::testing::Test::HasFatalFailure()) return;
    }
  }
}

// One long segment, with sync marks in it and unsynced commits among synced ones. A hole in what
// was never promised is the tail of a crash, however much intact data follows it.
TEST(FaultInjectionTest, PowerLossInALongSegmentCutsOnlyWhatWasNeverPromised) {
  constexpr int kCommits = 240;  // Over the sync mark interval several times.
  const auto options = [](const std::shared_ptr<MemoryFileSystem>& file_system) {
    DbOptions db_options = Options(file_system);
    db_options.wal_segment_bytes = uint64_t{1} << 30;
    return db_options;
  };
  uint64_t total_operations;
  {
    auto file_system = std::make_shared<MemoryFileSystem>();
    auto db = Db::Open(options(file_system));
    ASSERT_TRUE(db.ok()) << db.status();
    Workload workload;
    workload.checkpoint_every = 0;
    workload.Run(**db, kCommits);
    total_operations = file_system->operations();
  }
  for (uint64_t at = 0; at <= total_operations; at += 3) {
    for (uint32_t seed = 0; seed < 3; ++seed) {
      auto file_system = std::make_shared<MemoryFileSystem>();
      file_system->CrashAfter(at);
      Workload workload;
      workload.checkpoint_every = 0;
      {
        auto db = Db::Open(options(file_system));
        if (db.ok()) workload.Run(**db, kCommits);
      }
      file_system->PowerLoss(Loss::kPartOfUnsynced, static_cast<uint32_t>(at) * 7 + seed);
      SCOPED_TRACE(absl::StrCat("Crash at operation ", at, ", seed ", seed));
      auto db = Db::Open(options(file_system));
      ASSERT_TRUE(db.ok()) << db.status();
      const uint64_t lsn = (*db)->engine().Stats().lsn;
      ASSERT_GE(lsn, workload.acknowledged_durable);
      ASSERT_LT(lsn, workload.states.size());
      ASSERT_EQ(ReadState(**db), workload.states[lsn]);
    }
  }
}

TEST(FaultInjectionTest, FailedWriteOrSyncStopsTheLogAndLosesNothingAcknowledged) {
  for (uint64_t at = 0; at < 60; ++at) {
    auto file_system = std::make_shared<MemoryFileSystem>();
    auto db = Db::Open(Options(file_system));
    ASSERT_TRUE(db.ok()) << db.status();
    Workload healthy;
    healthy.Run(**db, 3);

    file_system->FailAfter(at, absl::ResourceExhaustedError("No space left"));
    Workload workload = healthy;
    int failures = 0;
    for (int i = 0; i < 30; ++i) {
      auto writer = (*db)->CreateWriter();
      const State before = ReadState(**db);
      ASSERT_TRUE(
          writer.PutAccount(AccountBuilder().set_account_id(50).set_age(1000 + i).Build()).ok());
      if (writer.Commit({.sync = true}).ok()) {
        ASSERT_EQ(failures, 0) << "A commit succeeded after the log stopped";
        continue;
      }
      ++failures;
      // A failed commit changes nothing that readers see, and they keep working.
      ASSERT_EQ(ReadState(**db), before);
    }
    db->reset();
    if (failures == 0) continue;  // The failure landed outside of what commits touch.
    file_system->PowerLoss(Loss::kAllUnsynced, static_cast<uint32_t>(at));
    auto reopened = Db::Open(Options(file_system));
    ASSERT_TRUE(reopened.ok()) << "At " << at << ": " << reopened.status();
    auto writer = (*reopened)->CreateWriter();
    ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(51).Build()).ok());
    EXPECT_TRUE(writer.Commit().ok());
  }
}

TEST(FaultInjectionTest, FailedCheckpointLeavesTheDatabaseWorking) {
  for (uint64_t at = 0; at < 8; ++at) {
    auto file_system = std::make_shared<MemoryFileSystem>();
    auto db = Db::Open(Options(file_system));
    ASSERT_TRUE(db.ok()) << db.status();
    Workload workload;
    workload.Run(**db, 6);
    const State state = ReadState(**db);

    file_system->FailAfter(at, absl::InternalError("Disk error"));
    const absl::Status failed = (*db)->engine().Checkpoint();
    file_system->FailAfter(1 << 30, absl::OkStatus());
    if (failed.ok()) continue;
    // A rotation that failed stops the log; any later failure leaves it running.
    db->reset();
    file_system->PowerLoss(Loss::kAllUnsynced, static_cast<uint32_t>(at));
    auto reopened = Db::Open(Options(file_system));
    ASSERT_TRUE(reopened.ok()) << reopened.status();
    EXPECT_GE((*reopened)->engine().Stats().lsn, workload.acknowledged_durable);
    EXPECT_TRUE((*reopened)->engine().Checkpoint().ok());
    (void)state;
  }
}

// A read error says nothing about the data. Taking it for a torn tail would destroy good commits.
TEST(FaultInjectionTest, ReadErrorDuringRecoveryFailsAndCutsNothing) {
  auto file_system = std::make_shared<MemoryFileSystem>();
  {
    auto db = Db::Open(Options(file_system));
    ASSERT_TRUE(db.ok());
    Workload workload;
    workload.Run(**db, 3);
  }
  const std::vector<std::string> paths = file_system->Paths();
  std::map<std::string, std::string> before;
  for (const std::string& path : paths) before[path] = file_system->Contents(path);

  file_system->FailReads("wal-");
  EXPECT_EQ(Db::Open(Options(file_system)).status().code(), absl::StatusCode::kInternal);
  for (const auto& [path, contents] : before) EXPECT_EQ(file_system->Contents(path), contents);

  file_system->FailReads("");
  auto db = Db::Open(Options(file_system));
  ASSERT_TRUE(db.ok()) << db.status();
  EXPECT_EQ((*db)->engine().Stats().lsn, 3u);
}

// The size field of a frame in a torn tail is garbage, and must not decide how much memory to use.
TEST(FaultInjectionTest, GarbageSizeInATornTailDoesNotAllocateIt) {
  auto file_system = std::make_shared<MemoryFileSystem>();
  {
    auto db = Db::Open(Options(file_system));
    ASSERT_TRUE(db.ok());
    Workload workload;
    workload.Run(**db, 3);
  }
  std::string last;
  for (const std::string& path : file_system->Paths()) {
    if (path.find("wal-") != std::string::npos) last = path;
  }
  file_system->AppendBytes(last, std::string("\x12\x34\x56\x78\xff\xff\xff\xff", 8));
  auto db = Db::Open(Options(file_system));
  ASSERT_TRUE(db.ok()) << db.status();
  EXPECT_EQ((*db)->engine().Stats().lsn, 3u);
  EXPECT_LE(file_system->largest_read(), size_t{1} << 20);
}

TEST(FaultInjectionTest, ReadErrorPartWayThroughALogFailsAndCutsNothing) {
  auto file_system = std::make_shared<MemoryFileSystem>();
  DbOptions options = Options(file_system);
  options.wal_segment_bytes = uint64_t{1} << 30;
  {
    auto db = Db::Open(options);
    ASSERT_TRUE(db.ok());
    const std::string name(1 << 16, 'n');
    for (uint64_t id = 1; id <= 40; ++id) {  // Over 1 MB, the size the log is read in.
      auto writer = (*db)->CreateWriter();
      ASSERT_TRUE(
          writer.PutAccount(AccountBuilder().set_account_id(id).set_name(name).Build()).ok());
      ASSERT_TRUE(writer.Commit().ok());
    }
  }
  const std::string segment = "/db/wal-00000000000000000001.log";
  const std::string before = file_system->Contents(segment);
  ASSERT_GT(before.size(), size_t{1} << 21);

  file_system->FailReads("wal-", uint64_t{1} << 20);
  EXPECT_EQ(Db::Open(options).status().code(), absl::StatusCode::kInternal);
  EXPECT_EQ(file_system->Contents(segment), before);

  file_system->FailReads("");
  auto db = Db::Open(options);
  ASSERT_TRUE(db.ok()) << db.status();
  EXPECT_EQ((*db)->engine().Stats().lsn, 40u);
}

std::string LastSegment(const MemoryFileSystem& file_system) {
  std::string last;
  size_t bytes = 0;
  for (const std::string& path : file_system.Paths()) {
    if (path.find("wal-") == std::string::npos) continue;
    const size_t size = file_system.Contents(path).size();
    // An engine that opened leaves an empty new segment last; look for the one holding commits.
    if (size >= bytes) {
      bytes = size;
      last = path;
    }
  }
  return last;
}

// After a clean shutdown every byte of the log is known to be good, so damage is always reported,
// where the same damage could be the tail of a crash.
TEST(FaultInjectionTest, DamageInTheLastSegmentIsToldApartFromATornTail) {
  const auto options = [](const std::shared_ptr<MemoryFileSystem>& file_system) {
    DbOptions db_options = Options(file_system);
    db_options.wal_segment_bytes = 1 << 20;
    return db_options;
  };
  const auto fill = [](Db& db) {
    for (uint64_t id = 1; id <= 200; ++id) {  // Well over the sync mark interval.
      auto writer = db.CreateWriter();
      ASSERT_TRUE(
          writer.PutAccount(AccountBuilder().set_account_id(id).set_name("some name").Build())
              .ok());
      ASSERT_TRUE(writer.Commit({.sync = true}).ok());
    }
  };

  {  // A clean shutdown, then damage anywhere.
    auto file_system = std::make_shared<MemoryFileSystem>();
    {
      auto db = Db::Open(options(file_system));
      ASSERT_TRUE(db.ok());
      fill(**db);
    }
    const std::string segment = LastSegment(*file_system);
    const size_t size = file_system->Contents(segment).size();
    file_system->FlipByte(segment, size - 40);
    EXPECT_EQ(Db::Open(options(file_system)).status().code(), absl::StatusCode::kDataLoss);
  }
  {  // A crash, then damage well before the end: the marks after it prove it had been durable.
    auto file_system = std::make_shared<MemoryFileSystem>();
    {
      auto db = Db::Open(options(file_system));
      ASSERT_TRUE(db.ok());
      fill(**db);
      file_system->CrashAfter(0);
    }
    file_system->PowerLoss(Loss::kAllUnsynced);
    const std::string segment = LastSegment(*file_system);
    file_system->FlipByte(segment, 400);
    EXPECT_EQ(Db::Open(options(file_system)).status().code(), absl::StatusCode::kDataLoss);
  }
  {  // A crash, then damage in the very last commit: it may be a torn write, so it is cut.
    auto file_system = std::make_shared<MemoryFileSystem>();
    {
      auto db = Db::Open(options(file_system));
      ASSERT_TRUE(db.ok());
      fill(**db);
      file_system->CrashAfter(0);
    }
    file_system->PowerLoss(Loss::kAllUnsynced);
    const std::string segment = LastSegment(*file_system);
    file_system->FlipByte(segment, file_system->Contents(segment).size() - 5);
    auto db = Db::Open(options(file_system));
    ASSERT_TRUE(db.ok()) << db.status();
    EXPECT_EQ((*db)->engine().Stats().lsn, 199u);
  }
}

// A replica replaces its files to install a checkpoint. Wherever the power goes out, it recovers to
// a state it can continue from.
TEST(FaultInjectionTest, PowerLossWhileInstallingACheckpoint) {
  Db primary;
  for (uint64_t id = 1; id <= 20; ++id) {
    auto writer = primary.CreateWriter();
    ASSERT_TRUE(
        writer.PutAccount(AccountBuilder().set_account_id(id).set_age(int32_t(id)).Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  LoopbackServer server(primary.engine());
  const auto replica_options = [](const std::shared_ptr<MemoryFileSystem>& file_system) {
    DbOptions options = Options(file_system);
    options.replica = true;
    return options;
  };
  const auto converges = [&](const std::shared_ptr<MemoryFileSystem>& file_system) {
    auto db = Db::Open(replica_options(file_system));
    ASSERT_TRUE(db.ok()) << db.status();
    gendb::Replica replica((*db)->engine(), server.connector(),
                           {.min_backoff = std::chrono::milliseconds(1)});
    // Its own LSN can be past the primary's: it is the state that has to arrive.
    const auto deadline = std::chrono::steady_clock::now() + kPatience;
    const State expected = ReadState(primary);
    while (ReadState(**db) != expected && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_EQ(ReadState(**db), expected) << replica.Status().last_error;
  };

  // A database with state of its own, on another history and several segments, to be replaced.
  const auto with_own_state = [] {
    auto file_system = std::make_shared<MemoryFileSystem>();
    auto db = Db::Open(Options(file_system));
    EXPECT_TRUE(db.ok()) << db.status();
    Workload workload;
    workload.checkpoint_every = 0;
    workload.Run(**db, 30);
    return file_system;
  };
  uint64_t total_operations;
  {
    auto file_system = with_own_state();
    const uint64_t before = file_system->operations();
    converges(file_system);
    total_operations = file_system->operations() - before;
  }
  ASSERT_GT(total_operations, 20u);
  for (uint64_t at = 0; at <= total_operations; ++at) {
    for (uint32_t attempt = 0; attempt < kInstallCrashAttempts; ++attempt) {
      const Loss loss = attempt == 0 ? Loss::kAllUnsynced : Loss::kPartOfUnsynced;
      SCOPED_TRACE(absl::StrCat("Crash at operation ", at, ", attempt ", attempt));
      auto file_system = with_own_state();
      file_system->CrashAfter(at);
      {
        auto db = Db::Open(replica_options(file_system));
        if (db.ok()) {
          gendb::Replica replica((*db)->engine(), server.connector(),
                                 {.min_backoff = std::chrono::milliseconds(1)});
          for (int i = 0;
               i < 2000 && !file_system->crashed() && replica.Status().checkpoints_installed == 0;
               ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
          }
        }
      }
      file_system->PowerLoss(loss, static_cast<uint32_t>(at) * 31 + attempt);
      converges(file_system);
      if (::testing::Test::HasFatalFailure()) return;
    }
  }
}

// Wherever the power goes out during a backup, what is there afterwards is either nothing or whole.
TEST(FaultInjectionTest, PowerLossDuringABackupNeverLeavesAPartialOne) {
  const auto with_data = [] {
    auto file_system = std::make_shared<MemoryFileSystem>();
    auto db = Db::Open(Options(file_system));
    EXPECT_TRUE(db.ok());
    Workload workload;
    workload.checkpoint_every = 0;
    workload.Run(**db, 10);
    return file_system;
  };
  uint64_t total_operations;
  {
    auto file_system = with_data();
    auto db = Db::Open(Options(file_system));
    ASSERT_TRUE(db.ok());
    const uint64_t before = file_system->operations();
    ASSERT_TRUE((*db)->engine().Backup("/backup").ok());
    total_operations = file_system->operations() - before;
  }
  for (uint64_t at = 0; at <= total_operations; ++at) {
    for (Loss loss : {Loss::kAllUnsynced, Loss::kPartOfUnsynced}) {
      auto file_system = with_data();
      auto db = Db::Open(Options(file_system));
      ASSERT_TRUE(db.ok());
      file_system->CrashAfter(at);
      const auto info = (*db)->engine().Backup("/backup");
      db->reset();
      file_system->PowerLoss(loss, static_cast<uint32_t>(at));
      SCOPED_TRACE(absl::StrCat("Crash at operation ", at, ", loss ", static_cast<int>(loss)));
      DbOptions options = Options(file_system);
      options.path = "/backup";
      auto restored = Db::Open(options);
      ASSERT_TRUE(restored.ok()) << restored.status();
      if (info.ok()) {
        EXPECT_EQ((*restored)->engine().Stats().lsn, info->lsn);
      }
    }
  }
}

class RecordingObserver final : public gendb::EngineObserver {
 public:
  void OnLog(gendb::LogLevel level, std::string_view message) override {
    if (level != gendb::LogLevel::kInfo) warnings.emplace_back(message);
  }
  void OnCommit(const gendb::CommitInfo& info) override {
    ++commits;
    last_commit = info;
  }
  void OnCheckpoint(const gendb::CheckpointInfo& info) override { checkpoints.push_back(info); }
  void OnRecovery(const gendb::RecoveryInfo& info) override { recovery = info; }

  std::vector<std::string> warnings;
  int commits = 0;
  gendb::CommitInfo last_commit{};
  std::vector<gendb::CheckpointInfo> checkpoints;
  std::optional<gendb::RecoveryInfo> recovery;
};

TEST(ObserverTest, SeesCommitsCheckpointsRecoveryAndProblems) {
  auto file_system = std::make_shared<MemoryFileSystem>();
  auto observer = std::make_shared<RecordingObserver>();
  DbOptions options = Options(file_system);
  options.observer = observer;
  {
    auto db = Db::Open(options);
    ASSERT_TRUE(db.ok());
    ASSERT_TRUE(observer->recovery.has_value());
    EXPECT_EQ(observer->recovery->lsn, 0u);
    {
      auto writer = (*db)->CreateWriter();
      ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(1).Build()).ok());
      ASSERT_TRUE(writer.Commit({.sync = true}).ok());
    }
    EXPECT_EQ(observer->commits, 1);
    EXPECT_EQ(observer->last_commit.lsn, 1u);
    EXPECT_EQ(observer->last_commit.records, 1u);
    EXPECT_TRUE(observer->last_commit.synced);
    EXPECT_GT(observer->last_commit.log_bytes, 0u);

    ASSERT_TRUE((*db)->engine().Checkpoint().ok());
    ASSERT_EQ(observer->checkpoints.size(), 1u);
    EXPECT_EQ(observer->checkpoints[0].lsn, 1u);
    EXPECT_GT(observer->checkpoints[0].bytes, 0u);
    EXPECT_TRUE(observer->checkpoints[0].status.ok());
    {
      auto writer = (*db)->CreateWriter();
      ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(2).Build()).ok());
      ASSERT_TRUE(writer.Commit({.sync = true}).ok());
      ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(3).Build()).ok());
      ASSERT_TRUE(writer.Commit({.sync = false}).ok());
    }
    file_system->CrashAfter(0);
  }
  file_system->PowerLoss(Loss::kPartOfUnsynced, 3);
  auto db = Db::Open(options);
  ASSERT_TRUE(db.ok()) << db.status();
  ASSERT_TRUE(observer->recovery.has_value());
  EXPECT_GE(observer->recovery->commits_replayed, 1u);
}

}  // namespace
