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
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "account.fbs.h"
#include "config.fbs.h"
#include "database.h"
#include "metadata.fbs.h"
#include "position.fbs.h"
#include "session.fbs.h"

using namespace gendb::tests;

using gendb::MetadataType;

class ChangeSubscriptionTest : public ::testing::Test {
 protected:
  Db db;
};

TEST_F(ChangeSubscriptionTest, DeliversBeforeAfterAndWhatChanged) {
  struct Seen {
    gendb::ChangeKind kind;
    uint64_t id;
    bool age_changed;
    bool name_changed;
    int32_t age_before;
    int32_t age_after;
  };
  std::vector<Seen> seen;
  std::vector<uint64_t> commits;
  auto subscription = db.Subscribe({
      .on_account =
          [&seen](const gendb::tests::AccountChange& change) {
            seen.push_back({change.kind(),
                            change.kind() == gendb::ChangeKind::kDelete
                                ? change.before().account_id()
                                : change.after().account_id(),
                            change.age_changed(), change.name_changed(), change.before().age(),
                            change.after().age()});
          },
      .on_commit =
          [&commits](const gendb::ChangePosition& position) { commits.push_back(position.lsn); },
  });

  auto writer = db.CreateWriter();
  ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(1).set_age(30).Build()).ok());
  ASSERT_TRUE(writer.Commit().ok());
  ASSERT_TRUE(writer.UpdateAccount(1, AccountPatchBuilder().set_age(31).Build()).ok());
  ASSERT_TRUE(writer.Commit().ok());
  // A Put reports just the same, field by field.
  ASSERT_TRUE(
      writer.PutAccount(AccountBuilder().set_account_id(1).set_age(31).set_name("A").Build()).ok());
  ASSERT_TRUE(writer.Commit().ok());
  ASSERT_TRUE(writer.DeleteAccount(1).ok());
  ASSERT_TRUE(writer.Commit().ok());
  // Not a collection this subscriber follows.
  ASSERT_TRUE(writer.PutConfig(ConfigBuilder().set_config_name("c").Build()).ok());
  ASSERT_TRUE(writer.Commit().ok());
  // Rolled back: never seen.
  ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(2).Build()).ok());
  writer.Rollback();
  subscription.WaitUntilDelivered();

  ASSERT_EQ(seen.size(), 4u);
  EXPECT_EQ(seen[0].kind, gendb::ChangeKind::kInsert);
  EXPECT_TRUE(seen[0].age_changed);
  EXPECT_EQ(seen[1].kind, gendb::ChangeKind::kUpdate);
  EXPECT_TRUE(seen[1].age_changed);
  EXPECT_FALSE(seen[1].name_changed);
  EXPECT_EQ(seen[1].age_before, 30);
  EXPECT_EQ(seen[1].age_after, 31);
  EXPECT_EQ(seen[2].kind, gendb::ChangeKind::kUpdate);
  EXPECT_FALSE(seen[2].age_changed);
  EXPECT_TRUE(seen[2].name_changed);
  EXPECT_EQ(seen[3].kind, gendb::ChangeKind::kDelete);
  EXPECT_EQ(seen[3].id, 1u);
  EXPECT_EQ(commits.size(), 4u);
}

TEST_F(ChangeSubscriptionTest, QueuedCallbackCanCommitAndBackPressureDoesNotDeadlock) {
  // The queue is always over its tiny limit, and the callback itself writes.
  std::atomic<int> delivered{0};
  auto subscription = db.Subscribe(
      {.on_account =
           [this, &delivered](const gendb::tests::AccountChange& change) {
             auto writer = db.CreateWriter();
             const uint64_t id = change.after().account_id();
             ASSERT_TRUE(writer.PutConfig(ConfigBuilder().set_config_name("x").Build()).ok());
             ASSERT_TRUE(writer.Commit().ok());
             ++delivered;
           }},
      {.max_queue_bytes = 1});
  for (uint64_t id = 0; id < 200; ++id) {
    auto writer = db.CreateWriter();
    ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(id).Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  subscription.WaitUntilDelivered();
  EXPECT_EQ(delivered.load(), 200);
}

TEST_F(ChangeSubscriptionTest, InlineRunsDuringCommitAndCancelStopsDelivery) {
  int calls = 0;
  bool visible_to_snapshot = false;
  {
    auto subscription = db.Subscribe({.on_account =
                                          [&](const gendb::tests::AccountChange&) {
                                            ++calls;
                                            Account account;
                                            visible_to_snapshot =
                                                db.CreateSnapshot().GetAccount(1, account).ok();
                                          }},
                                     {.delivery = gendb::Delivery::kInline});
    auto writer = db.CreateWriter();
    ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(1).Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
    EXPECT_EQ(calls, 1);  // Already delivered.
    EXPECT_TRUE(visible_to_snapshot);
  }
  auto writer = db.CreateWriter();
  ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(2).Build()).ok());
  ASSERT_TRUE(writer.Commit().ok());
  EXPECT_EQ(calls, 1);
}

class ResumeSubscriptionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    _path =
        (std::filesystem::temp_directory_path() / (std::string("gendb_") + info->name())).string();
    std::filesystem::remove_all(_path);
    auto opened = Db::Open({.path = _path, .sync_commits = false, .wal_retain_bytes = 0});
    ASSERT_TRUE(opened.ok()) << opened.status();
    db = std::move(*opened);
  }
  void TearDown() override {
    db.reset();
    std::filesystem::remove_all(_path);
  }
  void Put(uint64_t id, int32_t age) {
    auto writer = db->CreateWriter();
    ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(id).set_age(age).Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  void Remove(uint64_t id) {
    auto writer = db->CreateWriter();
    ASSERT_TRUE(writer.DeleteAccount(id).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }

  struct Seen {
    gendb::ChangeKind kind;
    uint64_t id;  // Decoded from the key, which is all a delete has while catching up.
    int32_t age;
  };
  ChangeHandlers Recording() {
    return {
        .on_account =
            [this](const AccountChange& change) {
              uint64_t id = 0;
              for (uint8_t byte : change.key()) id = id << 8 | byte;
              std::lock_guard lock(_mutex);
              seen.push_back({change.kind(), id, change.after().age()});
            },
        .on_commit =
            [this](const gendb::ChangePosition& position) {
              std::lock_guard lock(_mutex);
              last_position = position;
            },
    };
  }
  std::vector<Seen> TakeSeen() {
    std::lock_guard lock(_mutex);
    return std::exchange(seen, {});
  }
  // The catch-up runs on the subscription's thread, and ends with its position moved.
  void WaitForPosition(uint64_t lsn) {
    for (int i = 0; i < 10000; ++i) {
      {
        std::lock_guard lock(_mutex);
        if (last_position.lsn >= lsn) return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ADD_FAILURE() << "The subscriber did not reach LSN " << lsn;
  }

  std::unique_ptr<Db> db;
  std::vector<Seen> seen;
  gendb::ChangePosition last_position;
  std::mutex _mutex;
  std::string _path;
};

TEST_F(ResumeSubscriptionTest, CatchesUpWithOneChangePerRecordThenFollows) {
  Put(1, 10);
  Put(2, 20);
  Put(3, 30);
  const gendb::ChangePosition stored = db->engine().CurrentChangePosition();
  // While the subscriber is away: changed twice, deleted, new, and created and deleted again.
  Put(1, 11);
  Put(1, 12);
  Remove(2);
  Put(4, 40);
  Put(5, 50);
  Remove(5);

  auto subscription = db->SubscribeFrom(stored, Recording());
  ASSERT_TRUE(subscription.ok()) << subscription.status();
  Put(6, 60);  // Live, right after.
  WaitForPosition(10);

  std::vector<Seen> changes = TakeSeen();
  ASSERT_EQ(changes.size(), 5u);
  EXPECT_EQ(changes.back().kind, gendb::ChangeKind::kInsert);
  EXPECT_EQ(changes.back().id, 6u);
  changes.pop_back();
  std::sort(changes.begin(), changes.end(),
            [](const Seen& a, const Seen& b) { return a.id < b.id; });
  EXPECT_EQ(changes[0].kind, gendb::ChangeKind::kUpsert);
  EXPECT_EQ(changes[0].id, 1u);
  EXPECT_EQ(changes[0].age, 12);
  EXPECT_EQ(changes[1].kind, gendb::ChangeKind::kDelete);
  EXPECT_EQ(changes[1].id, 2u);
  EXPECT_EQ(changes[2].kind, gendb::ChangeKind::kUpsert);
  EXPECT_EQ(changes[2].id, 4u);
  EXPECT_EQ(changes[3].kind, gendb::ChangeKind::kDelete);
  EXPECT_EQ(changes[3].id, 5u);
}

// A position stored from the handler, used to resume over and over while the writer keeps going:
// following the changes must end up exactly at the database's state.
TEST_F(ResumeSubscriptionTest, ResumingWhileWritingLosesNothing) {
  std::atomic<bool> stop{false};
  std::thread writer_thread([this, &stop] {
    for (uint64_t id = 1; !stop.load(); ++id) Put(id % 50, static_cast<int32_t>(id));
  });
  std::map<uint64_t, int32_t> mirror;
  const auto apply_seen = [&] {
    for (const Seen& change : TakeSeen()) {
      if (change.kind == gendb::ChangeKind::kDelete) {
        mirror.erase(change.id);
      } else {
        mirror[change.id] = change.age;
      }
    }
  };
  gendb::ChangePosition position = db->engine().CurrentChangePosition();
  for (int round = 0; round < 10; ++round) {
    auto subscription = db->SubscribeFrom(position, Recording());
    ASSERT_TRUE(subscription.ok()) << subscription.status();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    subscription->Cancel();
    apply_seen();
    std::lock_guard lock(_mutex);
    if (last_position.lsn > position.lsn) position = last_position;
  }
  stop = true;
  writer_thread.join();
  {
    auto subscription = db->SubscribeFrom(position, Recording());
    ASSERT_TRUE(subscription.ok()) << subscription.status();
    WaitForPosition(db->engine().Stats().lsn);
  }
  apply_seen();
  auto snapshot = db->CreateSnapshot();
  std::map<uint64_t, int32_t> actual;
  for (auto it = snapshot.ScanAccount(); it.Valid(); it.Next()) {
    actual[it.Value().account_id()] = it.Value().age();
  }
  EXPECT_EQ(mirror, actual);
}

TEST_F(ResumeSubscriptionTest, RefusesPositionsItCannotHonour) {
  Put(1, 1);
  gendb::ChangePosition position = db->engine().CurrentChangePosition();

  gendb::ChangePosition other_history = position;
  other_history.history_low ^= 1;
  EXPECT_EQ(db->SubscribeFrom(other_history, {}).status().code(), absl::StatusCode::kOutOfRange);

  gendb::ChangePosition ahead = position;
  ahead.lsn += 1;
  EXPECT_EQ(db->SubscribeFrom(ahead, {}).status().code(), absl::StatusCode::kOutOfRange);

  EXPECT_EQ(db->SubscribeFrom(position, {}, {.delivery = gendb::Delivery::kInline}).status().code(),
            absl::StatusCode::kInvalidArgument);

  // Compaction removes the log that the position needs.
  Put(2, 2);
  ASSERT_TRUE(db->engine().Checkpoint().ok());
  Put(3, 3);
  EXPECT_EQ(db->SubscribeFrom(position, {}).status().code(), absl::StatusCode::kOutOfRange);
  EXPECT_TRUE(db->SubscribeFrom(db->engine().CurrentChangePosition(), {}).ok());
}

TEST(ResumeSubscriptionMemoryTest, AMemoryOnlyDatabaseCanOnlyStartFromNow) {
  Db db;
  {
    auto writer = db.CreateWriter();
    ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(1).Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  const gendb::ChangePosition now = db.engine().CurrentChangePosition();
  EXPECT_TRUE(db.SubscribeFrom(now, {}).ok());
  gendb::ChangePosition earlier = now;
  earlier.lsn = 0;
  EXPECT_EQ(db.SubscribeFrom(earlier, {}).status().code(), absl::StatusCode::kOutOfRange);
}
