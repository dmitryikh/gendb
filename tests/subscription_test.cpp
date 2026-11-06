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
      .on_commit = [&commits](uint64_t seq) { commits.push_back(seq); },
  });

  auto writer = db.CreateWriter();
  ASSERT_TRUE(writer.PutAccount(1, AccountBuilder().set_account_id(1).set_age(30).Build()).ok());
  ASSERT_TRUE(writer.Commit().ok());
  ASSERT_TRUE(writer.UpdateAccount(1, AccountPatchBuilder().set_age(31).Build()).ok());
  ASSERT_TRUE(writer.Commit().ok());
  // A Put reports just the same, field by field.
  ASSERT_TRUE(
      writer.PutAccount(1, AccountBuilder().set_account_id(1).set_age(31).set_name("A").Build())
          .ok());
  ASSERT_TRUE(writer.Commit().ok());
  ASSERT_TRUE(writer.DeleteAccount(1).ok());
  ASSERT_TRUE(writer.Commit().ok());
  // Not a collection this subscriber follows.
  ASSERT_TRUE(writer.PutConfig("c", ConfigBuilder().set_config_name("c").Build()).ok());
  ASSERT_TRUE(writer.Commit().ok());
  // Rolled back: never seen.
  ASSERT_TRUE(writer.PutAccount(2, AccountBuilder().set_account_id(2).Build()).ok());
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
             ASSERT_TRUE(
                 writer.PutConfig(std::to_string(id), ConfigBuilder().set_config_name("x").Build())
                     .ok());
             ASSERT_TRUE(writer.Commit().ok());
             ++delivered;
           }},
      {.max_queue_bytes = 1});
  for (uint64_t id = 0; id < 200; ++id) {
    auto writer = db.CreateWriter();
    ASSERT_TRUE(writer.PutAccount(id, AccountBuilder().set_account_id(id).Build()).ok());
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
    ASSERT_TRUE(writer.PutAccount(1, AccountBuilder().set_account_id(1).Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
    EXPECT_EQ(calls, 1);  // Already delivered.
    EXPECT_TRUE(visible_to_snapshot);
  }
  auto writer = db.CreateWriter();
  ASSERT_TRUE(writer.PutAccount(2, AccountBuilder().set_account_id(2).Build()).ok());
  ASSERT_TRUE(writer.Commit().ok());
  EXPECT_EQ(calls, 1);
}
