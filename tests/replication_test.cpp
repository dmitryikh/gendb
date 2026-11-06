#include "gendb/replication.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "account.fbs.h"
#include "database.h"
#include "tcp_transport.h"

namespace {

using gendb::tests::Account;
using gendb::tests::AccountBuilder;
using gendb::tests::AccountChange;
using gendb::tests::AccountPatchBuilder;
using gendb::tests::Db;
using gendb::tests::Session;
using gendb::tests::SessionBuilder;

constexpr auto kPatience = std::chrono::seconds(30);

// The simplest possible transport: two message queues in one process.
class Pipe {
 public:
  void Push(std::string message) {
    {
      std::lock_guard lock(_mutex);
      _messages.push_back(std::move(message));
    }
    _cv.notify_all();
  }
  absl::Status Pop(std::string* message) {
    std::unique_lock lock(_mutex);
    _cv.wait(lock, [this] { return _closed || !_messages.empty(); });
    if (_closed) return absl::UnavailableError("closed");
    *message = std::move(_messages.front());
    _messages.pop_front();
    return absl::OkStatus();
  }
  void Close() {
    {
      std::lock_guard lock(_mutex);
      _closed = true;
    }
    _cv.notify_all();
  }

 private:
  std::mutex _mutex;
  std::condition_variable _cv;
  std::deque<std::string> _messages;
  bool _closed = false;
};

class PipeConnection final : public gendb::Connection {
 public:
  PipeConnection(std::shared_ptr<Pipe> in, std::shared_ptr<Pipe> out)
      : _in(std::move(in)), _out(std::move(out)) {}
  ~PipeConnection() override { Close(); }

  absl::Status Send(std::string_view message) override {
    _out->Push(std::string(message));
    return absl::OkStatus();
  }
  absl::Status Receive(std::string* message) override { return _in->Pop(message); }
  void Close() override {
    _in->Close();
    _out->Close();
  }

 private:
  std::shared_ptr<Pipe> _in, _out;
};

// A primary's end: each connection gets a thread calling Serve, like a server's accept loop.
class LoopbackServer {
 public:
  explicit LoopbackServer(gendb::Engine& engine, gendb::ReplicationServerOptions options = {})
      : _server(engine, options) {}
  ~LoopbackServer() {
    _server.Shutdown();
    for (auto& thread : _threads) thread.join();
  }

  gendb::Connector connector() {
    return [this]() -> absl::StatusOr<std::unique_ptr<gendb::Connection>> {
      std::lock_guard lock(_mutex);
      if (_refuse) return absl::UnavailableError("refused");
      auto to_server = std::make_shared<Pipe>();
      auto to_client = std::make_shared<Pipe>();
      auto server_side = std::make_shared<PipeConnection>(to_server, to_client);
      _open.push_back(server_side);
      _threads.emplace_back([this, server_side] { (void)_server.Serve(*server_side); });
      return std::unique_ptr<gendb::Connection>(new PipeConnection(to_client, to_server));
    };
  }
  // Breaks every connection, as a network failure would.
  void DropConnections() {
    std::lock_guard lock(_mutex);
    for (auto& connection : _open) connection->Close();
    _open.clear();
  }
  void Refuse(bool refuse) {
    std::lock_guard lock(_mutex);
    _refuse = refuse;
  }
  gendb::ReplicationServer& server() { return _server; }

 private:
  gendb::ReplicationServer _server;
  std::mutex _mutex;
  std::vector<std::shared_ptr<PipeConnection>> _open;
  std::vector<std::thread> _threads;
  bool _refuse = false;
};

void Put(Db& db, uint64_t id, int32_t age = 30) {
  auto writer = db.CreateWriter();
  ASSERT_TRUE(writer.PutAccount(id, AccountBuilder().set_account_id(id).set_age(age).Build()).ok());
  ASSERT_TRUE(writer.Commit().ok());
}

size_t CountAccounts(Db& db) {
  size_t rows = 0;
  auto snapshot = db.CreateSnapshot();
  for (auto it = snapshot.ScanAccount(); it.Valid(); it.Next()) ++rows;
  return rows;
}

class ReplicationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    _dir = std::filesystem::temp_directory_path() / (std::string("gendb_repl_") + info->name());
    std::filesystem::remove_all(_dir);
    std::filesystem::create_directories(_dir);
  }
  void TearDown() override { std::filesystem::remove_all(_dir); }
  std::string Path(std::string_view name) const { return (_dir / name).string(); }

  std::filesystem::path _dir;
};

TEST_F(ReplicationTest, SeedsFromCheckpointThenFollowsTheLog) {
  Db primary;
  LoopbackServer server(primary.engine());
  // Before the replica exists: reaches it through the checkpoint.
  for (uint64_t id = 1; id <= 100; ++id) Put(primary, id);

  Db replica_db;
  gendb::Replica replica(replica_db.engine(), server.connector());
  ASSERT_TRUE(replica.WaitForLsn(100, kPatience)) << replica.Status().last_error;
  EXPECT_EQ(CountAccounts(replica_db), 100u);
  EXPECT_EQ(replica.Status().checkpoints_installed, 1u);

  // After it connected: reaches it through the log, with indices.
  {
    auto writer = primary.CreateWriter();
    ASSERT_TRUE(writer.UpdateAccount(1, AccountPatchBuilder().set_age(41).Build()).ok());
    ASSERT_TRUE(writer.DeleteAccount(2).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  ASSERT_TRUE(replica.WaitForLsn(101, kPatience));

  auto snapshot = replica_db.CreateSnapshot();
  Account account;
  ASSERT_TRUE(snapshot.GetAccount(1, account).ok());
  EXPECT_EQ(account.age(), 41);
  EXPECT_FALSE(snapshot.GetAccount(2, account).ok());
  auto by_age = snapshot.GetAccountByAgeEqual(41);
  ASSERT_TRUE(by_age.Valid());
  EXPECT_EQ(by_age.Value().account_id(), 1u);
  EXPECT_EQ(replica.Status().checkpoints_installed, 1u);
}

TEST_F(ReplicationTest, ReplicaRejectsLocalWritesExceptEphemeral) {
  Db primary;
  LoopbackServer server(primary.engine());
  Db replica_db;
  gendb::Replica replica(replica_db.engine(), server.connector());

  auto writer = replica_db.CreateWriter();
  ASSERT_TRUE(writer.PutAccount(1, AccountBuilder().set_account_id(1).Build()).ok());
  EXPECT_EQ(writer.Commit().code(), absl::StatusCode::kFailedPrecondition);

  // Its own sessions are its own, and not replicated, on either side.
  ASSERT_TRUE(writer.PutSession(7, SessionBuilder().set_session_id(7).Build()).ok());
  EXPECT_TRUE(writer.Commit().ok());
  EXPECT_EQ(replica_db.engine().Stats().lsn, 0u);

  auto primary_writer = primary.CreateWriter();
  ASSERT_TRUE(primary_writer.PutSession(8, SessionBuilder().set_session_id(8).Build()).ok());
  ASSERT_TRUE(primary_writer.Commit().ok());
  EXPECT_EQ(primary.engine().Stats().lsn, 0u);
}

TEST_F(ReplicationTest, ReconnectContinuesFromThePositionWithoutACheckpoint) {
  Db primary;
  LoopbackServer server(primary.engine());
  Db replica_db;
  gendb::Replica replica(replica_db.engine(), server.connector(),
                         {.min_backoff = std::chrono::milliseconds(1)});
  Put(primary, 1);
  ASSERT_TRUE(replica.WaitForLsn(1, kPatience));

  server.Refuse(true);
  server.DropConnections();
  for (uint64_t id = 2; id <= 50; ++id) Put(primary, id);
  server.Refuse(false);

  ASSERT_TRUE(replica.WaitForLsn(50, kPatience)) << replica.Status().last_error;
  EXPECT_EQ(CountAccounts(replica_db), 50u);
  EXPECT_EQ(replica.Status().checkpoints_installed, 1u);  // The first one only.
  EXPECT_GE(replica.Status().connections, 2u);
}

TEST_F(ReplicationTest, FallingBehindARingThatForgotMeansANewCheckpoint) {
  Db primary;
  LoopbackServer server(primary.engine(), {.commit_ring_bytes = 256});
  Db replica_db;
  gendb::Replica replica(replica_db.engine(), server.connector(),
                         {.min_backoff = std::chrono::milliseconds(1)});
  Put(primary, 1);
  ASSERT_TRUE(replica.WaitForLsn(1, kPatience));

  server.Refuse(true);
  server.DropConnections();
  for (uint64_t id = 2; id <= 100; ++id) Put(primary, id);
  server.Refuse(false);

  ASSERT_TRUE(replica.WaitForLsn(100, kPatience)) << replica.Status().last_error;
  EXPECT_EQ(CountAccounts(replica_db), 100u);
  EXPECT_EQ(replica.Status().checkpoints_installed, 2u);
}

// A replica whose history differs ends up exactly equal to the primary, atomically, and its
// subscribers hear about the differences only.
TEST_F(ReplicationTest, CheckpointReplacesDivergedStateAndReportsOnlyDifferences) {
  Db primary;
  Put(primary, 1, 10);  // Same on both.
  Put(primary, 2, 20);  // Differs.
  Put(primary, 3, 30);  // Only on the primary.

  Db replica_db;
  Put(replica_db, 1, 10);
  Put(replica_db, 2, 99);
  Put(replica_db, 4, 40);  // Only on the replica.

  std::vector<std::pair<gendb::ChangeKind, uint64_t>> changes;
  auto subscription = replica_db.Subscribe({.on_account = [&changes](const AccountChange& change) {
    const uint64_t id = change.kind() == gendb::ChangeKind::kDelete ? change.before().account_id()
                                                                    : change.after().account_id();
    changes.emplace_back(change.kind(), id);
  }});

  LoopbackServer server(primary.engine());
  gendb::Replica replica(replica_db.engine(), server.connector());
  ASSERT_TRUE(replica.WaitForLsn(3, kPatience)) << replica.Status().last_error;
  for (int i = 0; i < 1000 && replica.Status().checkpoints_installed == 0; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  subscription.WaitUntilDelivered();

  EXPECT_THAT(changes,
              ::testing::UnorderedElementsAre(std::pair(gendb::ChangeKind::kUpdate, uint64_t{2}),
                                              std::pair(gendb::ChangeKind::kInsert, uint64_t{3}),
                                              std::pair(gendb::ChangeKind::kDelete, uint64_t{4})));
  auto snapshot = replica_db.CreateSnapshot();
  Account account;
  ASSERT_TRUE(snapshot.GetAccount(2, account).ok());
  EXPECT_EQ(account.age(), 20);
  EXPECT_FALSE(snapshot.GetAccount(4, account).ok());
  EXPECT_FALSE(snapshot.GetAccountByAgeEqual(99).Valid());
  EXPECT_FALSE(snapshot.GetAccountByAgeEqual(40).Valid());
  EXPECT_TRUE(snapshot.GetAccountByAgeEqual(30).Valid());
}

TEST_F(ReplicationTest, PersistentReplicaResumesAfterRestart) {
  auto primary = Db::Open({.path = Path("primary")});
  ASSERT_TRUE(primary.ok());
  LoopbackServer server((*primary)->engine());
  for (uint64_t id = 1; id <= 10; ++id) Put(**primary, id);

  const gendb::tests::DbOptions options{.path = Path("replica"), .replica = true};
  {
    auto replica_db = Db::Open(options);
    ASSERT_TRUE(replica_db.ok());
    gendb::Replica replica((*replica_db)->engine(), server.connector());
    ASSERT_TRUE(replica.WaitForLsn(10, kPatience)) << replica.Status().last_error;
    Put(**primary, 11);
    ASSERT_TRUE(replica.WaitForLsn(11, kPatience));
  }
  for (uint64_t id = 12; id <= 20; ++id) Put(**primary, id);

  auto replica_db = Db::Open(options);
  ASSERT_TRUE(replica_db.ok()) << replica_db.status();
  EXPECT_EQ((*replica_db)->engine().Stats().lsn, 11u);  // Recovered from its own disk.
  EXPECT_EQ(CountAccounts(**replica_db), 11u);
  gendb::Replica replica((*replica_db)->engine(), server.connector());
  ASSERT_TRUE(replica.WaitForLsn(20, kPatience)) << replica.Status().last_error;
  EXPECT_EQ(replica.Status().checkpoints_installed, 0u);
  EXPECT_EQ(CountAccounts(**replica_db), 20u);
}

// A replica far behind is served from the primary's disk once the ring has moved on.
TEST_F(ReplicationTest, LaggingReplicaIsServedFromTheWal) {
  auto primary =
      Db::Open({.path = Path("primary"), .sync_commits = false, .wal_segment_bytes = 1024});
  ASSERT_TRUE(primary.ok());
  LoopbackServer server((*primary)->engine(), {.commit_ring_bytes = 128});
  Db replica_db;
  gendb::Replica replica(replica_db.engine(), server.connector(),
                         {.min_backoff = std::chrono::milliseconds(1)});
  Put(**primary, 1);
  ASSERT_TRUE(replica.WaitForLsn(1, kPatience));

  server.Refuse(true);
  server.DropConnections();
  for (uint64_t id = 2; id <= 500; ++id) Put(**primary, id);
  server.Refuse(false);

  ASSERT_TRUE(replica.WaitForLsn(500, kPatience)) << replica.Status().last_error;
  EXPECT_EQ(CountAccounts(replica_db), 500u);
  EXPECT_EQ(replica.Status().checkpoints_installed, 1u);  // No second one.
}

TEST_F(ReplicationTest, PromotionStartsANewHistoryThatOldFollowersMustReseedFrom) {
  auto old_primary = Db::Open({.path = Path("a")});
  ASSERT_TRUE(old_primary.ok());
  auto promoted = Db::Open({.path = Path("b"), .replica = true});
  ASSERT_TRUE(promoted.ok());
  {
    LoopbackServer server((*old_primary)->engine());
    Put(**old_primary, 1);
    gendb::Replica replica((*promoted)->engine(), server.connector());
    ASSERT_TRUE(replica.WaitForLsn(1, kPatience)) << replica.Status().last_error;
    replica.Stop();
  }
  // Commits only the old primary has, as when failover loses a tail.
  Put(**old_primary, 2);

  ASSERT_TRUE((*promoted)->engine().PromoteToPrimary().ok());
  Put(**promoted, 3);

  // The old primary rejoins as a follower, and loses what the new history does not have.
  old_primary->reset();
  auto rejoined = Db::Open({.path = Path("a"), .replica = true});
  ASSERT_TRUE(rejoined.ok()) << rejoined.status();
  LoopbackServer server((*promoted)->engine());
  gendb::Replica replica((*rejoined)->engine(), server.connector());
  ASSERT_TRUE(replica.WaitForLsn(2, kPatience));
  for (int i = 0; i < 1000 && replica.Status().checkpoints_installed == 0; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  EXPECT_EQ(replica.Status().checkpoints_installed, 1u);
  auto snapshot = (*rejoined)->CreateSnapshot();
  Account account;
  EXPECT_TRUE(snapshot.GetAccount(1, account).ok());
  EXPECT_FALSE(snapshot.GetAccount(2, account).ok());
  EXPECT_TRUE(snapshot.GetAccount(3, account).ok());
}

// A large commit does not fit in one transport message.
TEST_F(ReplicationTest, CommitLargerThanAMessageArrivesWhole) {
  Db primary;
  LoopbackServer server(primary.engine());
  Db replica_db;
  gendb::Replica replica(replica_db.engine(), server.connector());
  const std::string name(3 << 20, 'x');
  {
    // Closed before waiting: starting a replica's session takes the writer lock for a moment.
    auto writer = primary.CreateWriter();
    ASSERT_TRUE(
        writer.PutAccount(1, AccountBuilder().set_account_id(1).set_name(name).Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  ASSERT_TRUE(replica.WaitForLsn(1, kPatience)) << replica.Status().last_error;
  Account account;
  auto snapshot = replica_db.CreateSnapshot();
  ASSERT_TRUE(snapshot.GetAccount(1, account).ok());
  EXPECT_EQ(account.name(), name);
}

// Readers on the replica must only ever see whole commits of the primary, under load.
TEST_F(ReplicationTest, ReadersSeeWholeCommitsWhileFollowing) {
  Db primary;
  LoopbackServer server(primary.engine());
  Db replica_db;
  gendb::Replica replica(replica_db.engine(), server.connector());

  std::atomic<bool> done{false};
  std::atomic<bool> torn{false};
  std::thread reader([&] {
    while (!done.load()) {
      auto snapshot = replica_db.CreateSnapshot();
      Account a, b;
      const bool has_a = snapshot.GetAccount(1, a).ok();
      const bool has_b = snapshot.GetAccount(2, b).ok();
      if (has_a != has_b || (has_a && a.age() != b.age())) torn = true;
    }
  });
  for (int32_t round = 1; round <= 2000; ++round) {
    auto writer = primary.CreateWriter();
    ASSERT_TRUE(
        writer.PutAccount(1, AccountBuilder().set_account_id(1).set_age(round).Build()).ok());
    ASSERT_TRUE(
        writer.PutAccount(2, AccountBuilder().set_account_id(2).set_age(round).Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  ASSERT_TRUE(replica.WaitForLsn(2000, kPatience)) << replica.Status().last_error;
  done = true;
  reader.join();
  EXPECT_FALSE(torn.load());
}

// The replica's records must be the primary's, byte for byte, however they were changed.
TEST_F(ReplicationTest, PatchesGiveTheReplicaExactlyThePrimarysBytes) {
  Db primary;
  LoopbackServer server(primary.engine());
  Db replica_db;
  gendb::Replica replica(replica_db.engine(), server.connector());

  AccountBuilder builder;
  builder.set_account_id(1).set_name("Ann").set_age(30).set_balance(1.f);
  builder.AddStringField(1000, "from a newer schema");
  {
    auto writer = primary.CreateWriter();
    ASSERT_TRUE(writer.PutAccount(1, builder.Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
    ASSERT_TRUE(writer.UpdateAccount(1, AccountPatchBuilder().set_age(31).Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
    ASSERT_TRUE(writer.UpdateAccount(1, AccountPatchBuilder().set_name("Annabelle").Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
    ASSERT_TRUE(writer.UpdateAccount(1, AccountPatchBuilder().clear_balance().Build()).ok());
    ASSERT_TRUE(
        writer.UpdateAccount(1, AccountPatchBuilder().set_balance(2.f).set_name("B").Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  ASSERT_TRUE(replica.WaitForLsn(primary.engine().Stats().lsn, kPatience))
      << replica.Status().last_error;
  EXPECT_EQ(replica.Status().checkpoints_installed, 1u);

  const auto key = gendb::tests::ToAccountKey(uint64_t{1});
  gendb::EngineSnapshot on_primary(primary.engine());
  gendb::EngineSnapshot on_replica(replica_db.engine());
  const auto ours = on_primary.Get(gendb::tests::AccountCollId, key);
  const auto theirs = on_replica.Get(gendb::tests::AccountCollId, key);
  ASSERT_TRUE(ours.has_value() && theirs.has_value());
  EXPECT_TRUE(std::equal(ours->begin(), ours->end(), theirs->begin(), theirs->end()));
}

// Two databases with the same history and LSN but different contents, as only a bug or a damaged
// disk can make. The checksum on patches catches it at the next patch, and a checkpoint heals it.
TEST_F(ReplicationTest, DivergedReplicaIsCaughtByThePatchChecksumAndHealed) {
  const auto update_age = [](Db& db, int32_t age) {
    auto writer = db.CreateWriter();
    ASSERT_TRUE(writer.UpdateAccount(1, AccountPatchBuilder().set_age(age).Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
  };
  {
    auto db = Db::Open({.path = Path("primary")});
    ASSERT_TRUE(db.ok());
    auto writer = (*db)->CreateWriter();
    ASSERT_TRUE(
        writer.PutAccount(1, AccountBuilder().set_account_id(1).set_age(1).set_balance(1.f).Build())
            .ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  std::filesystem::copy(Path("primary"), Path("replica"), std::filesystem::copy_options::recursive);

  auto primary = Db::Open({.path = Path("primary")});
  ASSERT_TRUE(primary.ok());
  update_age(**primary, 100);
  {
    auto diverged = Db::Open({.path = Path("replica")});
    ASSERT_TRUE(diverged.ok());
    update_age(**diverged, 200);
  }

  auto replica_db = Db::Open({.path = Path("replica"), .replica = true});
  ASSERT_TRUE(replica_db.ok());
  ASSERT_EQ((*replica_db)->engine().Stats().lsn, 2u);
  LoopbackServer server((*primary)->engine());
  gendb::Replica replica((*replica_db)->engine(), server.connector(),
                         {.min_backoff = std::chrono::milliseconds(1)});
  {
    auto writer = (*primary)->CreateWriter();
    ASSERT_TRUE(writer.UpdateAccount(1, AccountPatchBuilder().set_balance(2.f).Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  ASSERT_TRUE(replica.WaitForLsn(3, kPatience)) << replica.Status().last_error;
  for (int i = 0; i < 1000 && replica.Status().checkpoints_installed == 0; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  EXPECT_EQ(replica.Status().checkpoints_installed, 1u);
  Account account;
  ASSERT_TRUE((*replica_db)->CreateSnapshot().GetAccount(1, account).ok());
  EXPECT_EQ(account.age(), 100);
  EXPECT_EQ(account.balance(), 2.f);
}

// Two versions of one program, by raw collections, as what matters is what each schema has.
const std::array<uint8_t, 1> kKey1{1}, kKey2{2}, kKey3{3}, kKey4{4};
const std::array<uint8_t, 1> kValue{42};

gendb::EngineSchema OldSchema() {
  gendb::EngineSchema schema;
  schema.collections.push_back({"users"});
  return schema;
}

// "orders" is new, with an index on the value.
gendb::EngineSchema NewSchema() {
  gendb::EngineSchema schema;
  schema.collections.push_back({"users"});
  schema.collections.push_back({"orders"});
  schema.indices.push_back(
      {"orders_by_value", 1, [](gendb::BytesConstView message, std::string& out) {
         out.append(reinterpret_cast<const char*>(message.data()), message.size());
         return true;
       }});
  return schema;
}

constexpr size_t kUsers = 0;
constexpr size_t kOrders = 1;

void PutRaw(gendb::Engine& engine, size_t collection, gendb::BytesConstView key) {
  gendb::Transaction txn(engine);
  txn.Put(collection, key, kValue);
  ASSERT_TRUE(txn.Commit().ok());
}

// The primary is upgraded first: the old replica keeps a collection it does not know, across a
// local checkpoint and restarts, and its upgrade finds the data there, indexed, with no resync.
TEST_F(ReplicationTest, PrimaryCanBeUpgradedBeforeItsReplicas) {
  gendb::Engine primary(NewSchema());
  LoopbackServer server(primary);
  PutRaw(primary, kUsers, kKey1);
  PutRaw(primary, kOrders, kKey1);  // Reaches the replica in a checkpoint.

  const gendb::EngineOptions options{.path = Path("replica"), .replica = true};
  {
    auto old_binary = gendb::Engine::Open(OldSchema(), options);
    ASSERT_TRUE(old_binary.ok()) << old_binary.status();
    gendb::Replica replica(**old_binary, server.connector());
    ASSERT_TRUE(replica.WaitForLsn(2, kPatience)) << replica.Status().last_error;
    PutRaw(primary, kOrders, kKey2);  // Reaches it in the log.
    ASSERT_TRUE(replica.WaitForLsn(3, kPatience)) << replica.Status().last_error;
    EXPECT_THAT((*old_binary)->Stats().foreign_collections, ::testing::ElementsAre("orders"));
    EXPECT_EQ(replica.Status().checkpoints_installed, 1u);
    ASSERT_TRUE((*old_binary)->Checkpoint().ok());
    PutRaw(primary, kOrders, kKey3);
    ASSERT_TRUE(replica.WaitForLsn(4, kPatience)) << replica.Status().last_error;
  }
  {
    auto restarted = gendb::Engine::Open(OldSchema(), options);
    ASSERT_TRUE(restarted.ok()) << restarted.status();
    EXPECT_EQ((*restarted)->Stats().lsn, 4u);
    EXPECT_THAT((*restarted)->Stats().foreign_collections, ::testing::ElementsAre("orders"));
    gendb::Replica replica(**restarted, server.connector());
    ASSERT_TRUE(replica.WaitForLsn(4, kPatience)) << replica.Status().last_error;
    EXPECT_EQ(replica.Status().checkpoints_installed, 0u);
  }

  auto upgraded = gendb::Engine::Open(NewSchema(), options);
  ASSERT_TRUE(upgraded.ok()) << upgraded.status();
  EXPECT_TRUE((*upgraded)->Stats().foreign_collections.empty());
  gendb::Replica replica(**upgraded, server.connector());
  PutRaw(primary, kOrders, kKey4);
  ASSERT_TRUE(replica.WaitForLsn(5, kPatience)) << replica.Status().last_error;
  EXPECT_EQ(replica.Status().checkpoints_installed, 0u);

  gendb::EngineSnapshot snapshot(**upgraded);
  for (const auto& key : {kKey1, kKey2, kKey3, kKey4}) {
    EXPECT_TRUE(snapshot.Get(kOrders, key).has_value());
  }
  size_t indexed = 0;
  for (gendb::IndexCursor it(snapshot, 0, "", ""); it.Valid(); it.Next()) ++indexed;
  EXPECT_EQ(indexed, 4u);
}

// An old replica can even take over: what it kept goes on to the next replica.
TEST_F(ReplicationTest, PromotedOldReplicaPassesOnWhatItDoesNotKnow) {
  gendb::Engine primary(NewSchema());
  gendb::Engine old_binary(OldSchema());
  {
    LoopbackServer server(primary);
    PutRaw(primary, kOrders, kKey1);
    gendb::Replica replica(old_binary, server.connector());
    ASSERT_TRUE(replica.WaitForLsn(1, kPatience)) << replica.Status().last_error;
    replica.Stop();
  }
  ASSERT_TRUE(old_binary.PromoteToPrimary().ok());
  PutRaw(old_binary, kUsers, kKey2);

  LoopbackServer server(old_binary);
  gendb::Engine next(NewSchema());
  gendb::Replica replica(next, server.connector());
  ASSERT_TRUE(replica.WaitForLsn(2, kPatience)) << replica.Status().last_error;
  gendb::EngineSnapshot snapshot(next);
  EXPECT_TRUE(snapshot.Get(kOrders, kKey1).has_value());
  EXPECT_TRUE(snapshot.Get(kUsers, kKey2).has_value());
}

// A replica can feed another, and learns of a collection after that one has connected.
TEST_F(ReplicationTest, CollectionLearnedMidwayReachesReplicasDownstream) {
  gendb::Engine middle(OldSchema());
  LoopbackServer middle_server(middle);
  gendb::Engine leaf(OldSchema());
  gendb::Replica leaf_replica(leaf, middle_server.connector(),
                              {.min_backoff = std::chrono::milliseconds(1)});

  gendb::Engine primary(NewSchema());
  LoopbackServer server(primary);
  PutRaw(primary, kOrders, kKey1);
  gendb::Replica middle_replica(middle, server.connector());
  ASSERT_TRUE(middle_replica.WaitForLsn(1, kPatience)) << middle_replica.Status().last_error;
  PutRaw(primary, kOrders, kKey2);
  ASSERT_TRUE(leaf_replica.WaitForLsn(2, kPatience)) << leaf_replica.Status().last_error;
  EXPECT_THAT(leaf.Stats().foreign_collections, ::testing::ElementsAre("orders"));
  leaf_replica.Stop();
  middle_replica.Stop();
}

// Durable on the primary, ephemeral here: the local records and the primary's do not mix, and
// subscribers hear only of what this schema can read.
TEST_F(ReplicationTest, ForeignDataStaysApartFromAnEphemeralCollectionOfTheSameName) {
  gendb::EngineSchema here;
  here.collections.push_back({"users"});
  here.collections.push_back({"orders", /*ephemeral=*/true});
  gendb::Engine replica_engine(here);
  const std::array<uint8_t, 1> local{7};
  {
    gendb::Transaction txn(replica_engine);
    txn.Put(kOrders, kKey1, local);
    ASSERT_TRUE(txn.Commit().ok());
  }
  std::vector<size_t> heard;
  auto subscription = replica_engine.Subscribe(
      [&heard](const gendb::CommitEvent& event) {
        for (const gendb::Change& change : event.changes) heard.push_back(change.collection);
      },
      {.delivery = gendb::Delivery::kInline});

  gendb::Engine primary(NewSchema());
  LoopbackServer server(primary);
  gendb::Replica replica(replica_engine, server.connector());
  PutRaw(primary, kOrders, kKey1);
  PutRaw(primary, kUsers, kKey1);
  ASSERT_TRUE(replica.WaitForLsn(2, kPatience)) << replica.Status().last_error;
  replica.Stop();

  EXPECT_THAT(replica_engine.Stats().foreign_collections, ::testing::ElementsAre("orders"));
  gendb::EngineSnapshot snapshot(replica_engine);
  const auto value = snapshot.Get(kOrders, kKey1);
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ((*value)[0], 7);
  EXPECT_THAT(heard, ::testing::Each(kUsers));
  EXPECT_FALSE(heard.empty());
}

// A busy writer retakes the writer mutex at once, which must not lock out maintenance.
TEST_F(ReplicationTest, BusyWriterStarvesNeitherCheckpointNorNewReplica) {
  auto primary = Db::Open({.path = Path("primary"), .sync_commits = false});
  ASSERT_TRUE(primary.ok());
  LoopbackServer server((*primary)->engine());
  std::atomic<bool> done{false};
  std::thread writer_thread([&] {
    uint64_t id = 0;
    while (!done.load()) {
      auto writer = (*primary)->CreateWriter();
      (void)writer.PutAccount(++id % 1000, AccountBuilder().set_account_id(id).Build());
      (void)writer.Commit();
    }
  });
  EXPECT_TRUE((*primary)->engine().Checkpoint().ok());
  {
    Db replica_db;
    gendb::Replica replica(replica_db.engine(), server.connector());
    EXPECT_TRUE(replica.WaitForLsn(1, kPatience)) << replica.Status().last_error;
  }
  done = true;
  writer_thread.join();
}

// Raw TCP, the transport in examples/: real sockets, no gRPC.
TEST_F(ReplicationTest, WorksOverRawTcp) {
  Db primary;
  gendb::ReplicationServer server(primary.engine());
  uint16_t port = 0;
  auto listener = gendb::examples::TcpListen(port);
  ASSERT_TRUE(listener.ok()) << listener.status();
  std::vector<std::thread> sessions;
  std::thread acceptor([&] {
    for (;;) {
      const int fd = accept(*listener, nullptr, nullptr);
      if (fd < 0) return;
      sessions.emplace_back([&server, fd] {
        gendb::examples::TcpConnection connection(fd);
        (void)server.Serve(connection);
      });
    }
  });

  for (uint64_t id = 1; id <= 100; ++id) Put(primary, id);
  {
    Db replica_db;
    gendb::Replica replica(replica_db.engine(), gendb::examples::TcpConnector("127.0.0.1", port));
    ASSERT_TRUE(replica.WaitForLsn(100, kPatience)) << replica.Status().last_error;
    Put(primary, 101);
    ASSERT_TRUE(replica.WaitForLsn(101, kPatience));
    EXPECT_EQ(CountAccounts(replica_db), 101u);
  }
  server.Shutdown();
  shutdown(*listener, SHUT_RDWR);
  close(*listener);
  acceptor.join();
  for (auto& session : sessions) session.join();
}

}  // namespace
