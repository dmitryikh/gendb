#include "generated/database.h"

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
#include "metadata.gendb.h"
#include "position.gendb.h"
#include "session.gendb.h"

using namespace gendb::tests;

using gendb::MetadataType;

TEST(DbTest, AddAndGetAccount) {
  Db db;
  {
    auto writer = db.CreateWriter();
    AccountBuilder builder;
    builder.set_account_id(1);
    builder.set_name("Alice");
    std::vector<uint8_t> buffer = builder.Build();
    EXPECT_TRUE(writer.PutAccount(buffer).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  {
    auto snapshot = db.CreateSnapshot();
    Account account;
    EXPECT_TRUE(snapshot.GetAccount(1, account).ok());
    EXPECT_EQ(account.account_id(), 1);
    EXPECT_EQ(account.name(), "Alice");
  }
}

TEST(DbTest, AddAndGetPosition) {
  Db db;
  {
    auto writer = db.CreateWriter();
    PositionBuilder builder;
    builder.set_position_id(10);
    builder.set_account_id(1);
    builder.set_instrument("AAPL");
    std::vector<uint8_t> buffer = builder.Build();
    EXPECT_TRUE(writer.PutPosition(buffer).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  {
    auto snapshot = db.CreateSnapshot();
    Position position;
    EXPECT_TRUE(snapshot.GetPosition(10, position).ok());
    EXPECT_EQ(position.position_id(), 10);
    EXPECT_EQ(position.account_id(), 1);
    EXPECT_EQ(position.instrument(), "AAPL");
  }
}

TEST(DbTest, UpdateAccountWithPatch) {
  Db db;
  {
    auto writer = db.CreateWriter();
    AccountBuilder builder;
    builder.set_account_id(2);
    builder.set_name("Bob");
    builder.set_balance(100.0f);
    std::vector<uint8_t> buffer = builder.Build();
    EXPECT_TRUE(writer.PutAccount(buffer).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  {
    auto writer = db.CreateWriter();
    auto patch = AccountPatchBuilder().set_balance(200.0f).set_name("Robert").Build();
    EXPECT_TRUE(writer.UpdateAccount(2, patch).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  {
    auto snapshot = db.CreateSnapshot();
    Account account;
    EXPECT_TRUE(snapshot.GetAccount(2, account).ok());
    EXPECT_EQ(account.balance(), 200.0f);
    EXPECT_EQ(account.name(), "Robert");
  }
}

TEST(DbTest, UpdatePositionWithPatch) {
  Db db;
  {
    auto writer = db.CreateWriter();
    PositionBuilder builder;
    builder.set_position_id(20);
    builder.set_account_id(2);
    builder.set_instrument("GOOG");
    builder.set_open_price(100.0f);
    std::vector<uint8_t> buffer = builder.Build();
    EXPECT_TRUE(writer.PutPosition(buffer).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  {
    auto writer = db.CreateWriter();
    auto patch = PositionPatchBuilder().set_open_price(101.5f).set_instrument("MSFT").Build();
    EXPECT_TRUE(writer.UpdatePosition(20, patch).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  {
    auto snapshot = db.CreateSnapshot();
    Position position;
    EXPECT_TRUE(snapshot.GetPosition(20, position).ok());
    EXPECT_EQ(position.open_price(), 101.5f);
    EXPECT_EQ(position.instrument(), "MSFT");
  }
}

TEST(DbTest, NoChangesWithoutCommit) {
  Db db;
  {
    auto writer = db.CreateWriter();
    AccountBuilder builder;
    builder.set_account_id(3);
    builder.set_name("Charlie");
    std::vector<uint8_t> buffer = builder.Build();
    EXPECT_TRUE(writer.PutAccount(buffer).ok());
    // Do not commit
  }
  {
    auto snapshot = db.CreateSnapshot();
    Account account;
    // Should not find the uncommitted account
    EXPECT_FALSE(snapshot.GetAccount(3, account).ok());
  }
}

TEST(DbTest, NoYetCommittedChangesNotVisibleFromOtherReaders) {
  Db db;
  {
    auto writer = db.CreateWriter();
    AccountBuilder builder;
    builder.set_account_id(5);
    builder.set_name("Eve");
    std::vector<uint8_t> buffer = builder.Build();
    EXPECT_TRUE(writer.PutAccount(buffer).ok());
    // Do not commit yet.
    {
      auto snapshot = db.CreateSnapshot();
      Account account;
      // Should not find the uncommitted account
      EXPECT_FALSE(snapshot.GetAccount(5, account).ok());
    }
    ASSERT_TRUE(writer.Commit().ok());
  }
  {
    auto snapshot = db.CreateSnapshot();
    Account account;
    // Now it's visible.
    EXPECT_TRUE(snapshot.GetAccount(5, account).ok());
    EXPECT_EQ(account.name(), "Eve");
  }
}

TEST(DbTest, NoYetCommittedChangesVisibleInWriter) {
  Db db;
  {
    auto writer = db.CreateWriter();
    AccountBuilder builder;
    builder.set_account_id(4);
    builder.set_name("David");
    std::vector<uint8_t> buffer = builder.Build();
    EXPECT_TRUE(writer.PutAccount(buffer).ok());

    Account account;
    EXPECT_TRUE(writer.GetAccount(4, account).ok());
    EXPECT_EQ(account.name(), "David");
  }
}

TEST(DbTest, GetAccountByAgeRange) {
  Db db;
  {
    auto writer = db.CreateWriter();
    AccountBuilder builder;
    builder.set_account_id(100);
    builder.set_name("Anna");
    builder.set_age(20);
    std::vector<uint8_t> buffer = builder.Build();
    EXPECT_TRUE(writer.PutAccount(buffer).ok());

    builder.set_account_id(101);
    builder.set_name("Ben");
    builder.set_age(25);
    buffer = builder.Build();
    EXPECT_TRUE(writer.PutAccount(buffer).ok());

    builder.set_account_id(102);
    builder.set_name("Cara");
    builder.set_age(30);
    buffer = builder.Build();
    EXPECT_TRUE(writer.PutAccount(buffer).ok());

    ASSERT_TRUE(writer.Commit().ok());
  }
  {
    auto snapshot = db.CreateSnapshot();
    auto it = snapshot.GetAccountByAgeRange(21, 31);
    std::vector<std::string> names;
    while (it.Valid()) {
      auto account = it.Value();
      names.emplace_back(account.name());
      it.Next();
    }
    EXPECT_THAT(names, ::testing::UnorderedElementsAre("Ben", "Cara"));
  }
}

TEST(DbTest, GetAccountByAgeEqual) {
  Db db;
  {
    auto writer = db.CreateWriter();
    AccountBuilder builder;
    builder.set_account_id(200);
    builder.set_name("Dan");
    builder.set_age(40);
    std::vector<uint8_t> buffer = builder.Build();
    EXPECT_TRUE(writer.PutAccount(buffer).ok());

    builder.set_account_id(201);
    builder.set_name("Eve");
    builder.set_age(40);
    buffer = builder.Build();
    EXPECT_TRUE(writer.PutAccount(buffer).ok());

    builder.set_account_id(202);
    builder.set_name("Finn");
    builder.set_age(41);
    buffer = builder.Build();
    EXPECT_TRUE(writer.PutAccount(buffer).ok());

    ASSERT_TRUE(writer.Commit().ok());
  }
  {
    auto snapshot = db.CreateSnapshot();
    auto it = snapshot.GetAccountByAgeEqual(40);
    std::vector<std::string> names;
    while (it.Valid()) {
      auto account = it.Value();
      names.emplace_back(account.name());
      it.Next();
    }
    EXPECT_THAT(names, ::testing::UnorderedElementsAre("Dan", "Eve"));
  }
}

TEST(DbTest, GetPositionByAccountIdRange) {
  Db db;
  {
    auto writer = db.CreateWriter();
    PositionBuilder builder;
    builder.set_position_id(1);
    builder.set_account_id(10);
    builder.set_instrument("AAPL");
    std::vector<uint8_t> buffer = builder.Build();
    EXPECT_TRUE(writer.PutPosition(buffer).ok());

    builder.set_position_id(2);
    builder.set_account_id(15);
    builder.set_instrument("GOOG");
    buffer = builder.Build();
    EXPECT_TRUE(writer.PutPosition(buffer).ok());

    builder.set_position_id(3);
    builder.set_account_id(20);
    builder.set_instrument("MSFT");
    buffer = builder.Build();
    EXPECT_TRUE(writer.PutPosition(buffer).ok());

    ASSERT_TRUE(writer.Commit().ok());
  }
  {
    auto snapshot = db.CreateSnapshot();
    auto it = snapshot.GetPositionByAccountIdRange(11, 21);
    std::vector<std::string> instruments;
    while (it.Valid()) {
      auto position = it.Value();
      instruments.emplace_back(position.instrument());
      it.Next();
    }
    EXPECT_THAT(instruments, ::testing::UnorderedElementsAre("GOOG", "MSFT"));
  }
}

TEST(DbTest, GetPositionByAccountIdEqual) {
  Db db;
  {
    auto writer = db.CreateWriter();
    PositionBuilder builder;
    builder.set_position_id(4);
    builder.set_account_id(30);
    builder.set_instrument("TSLA");
    std::vector<uint8_t> buffer = builder.Build();
    EXPECT_TRUE(writer.PutPosition(buffer).ok());

    builder.set_position_id(5);
    builder.set_account_id(30);
    builder.set_instrument("AMZN");
    buffer = builder.Build();
    EXPECT_TRUE(writer.PutPosition(buffer).ok());

    builder.set_position_id(6);
    builder.set_account_id(31);
    builder.set_instrument("NFLX");
    buffer = builder.Build();
    EXPECT_TRUE(writer.PutPosition(buffer).ok());

    ASSERT_TRUE(writer.Commit().ok());
  }
  {
    auto snapshot = db.CreateSnapshot();
    auto it = snapshot.GetPositionByAccountIdEqual(30);
    std::vector<std::string> instruments;
    while (it.Valid()) {
      auto position = it.Value();
      instruments.emplace_back(position.instrument());
      it.Next();
    }
    EXPECT_THAT(instruments, ::testing::UnorderedElementsAre("TSLA", "AMZN"));
  }
}

TEST(DbTest, PositionByAccountIdIndex_UncommittedNotVisibleToSnapshot) {
  Db db;
  auto writer = db.CreateWriter();
  PositionBuilder builder;
  builder.set_position_id(100);
  builder.set_account_id(50);
  builder.set_instrument("AAPL");
  std::vector<uint8_t> buffer = builder.Build();
  EXPECT_TRUE(writer.PutPosition(buffer).ok());
  // Do not commit yet.
  {
    auto snapshot = db.CreateSnapshot();
    auto it = snapshot.GetPositionByAccountIdEqual(50);
    // Should not see the uncommitted position
    EXPECT_FALSE(it.Valid());
  }
  ASSERT_TRUE(writer.Commit().ok());
  // Now changes are committed and should be visible to all readers.
  {
    auto snapshot = db.CreateSnapshot();
    auto it = snapshot.GetPositionByAccountIdEqual(50);
    ASSERT_TRUE(it.Valid());
    auto position = it.Value();
    EXPECT_EQ(position.position_id(), 100);
    EXPECT_EQ(position.account_id(), 50);
    EXPECT_EQ(position.instrument(), "AAPL");
  }
}

TEST(DbTest, PositionByAccountIdIndex_UncommittedVisibleToWriter) {
  Db db;
  {
    auto writer = db.CreateWriter();
    PositionBuilder builder;
    builder.set_position_id(101);
    builder.set_account_id(51);
    builder.set_instrument("GOOG");
    std::vector<uint8_t> buffer = builder.Build();
    EXPECT_TRUE(writer.PutPosition(buffer).ok());

    auto it = writer.GetPositionByAccountIdEqual(51);
    // Should see the uncommitted position
    ASSERT_TRUE(it.Valid());
    auto position = it.Value();
    EXPECT_EQ(position.position_id(), 101);
    EXPECT_EQ(position.account_id(), 51);
    EXPECT_EQ(position.instrument(), "GOOG");
  }
}

TEST(DbTest, NextAccountIdSequence) {
  Db db;
  {
    auto writer = db.CreateWriter();
    uint64_t next_id = 0;
    // Should start from 1
    EXPECT_TRUE(writer.NextAccountIdSequence(next_id).ok());
    EXPECT_EQ(next_id, 1);
    // Add an account
    EXPECT_TRUE(
        writer.PutAccount(AccountBuilder().set_account_id(next_id).set_name("First").Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }

  // Next should be incremented
  {
    auto writer2 = db.CreateWriter();
    uint64_t next_id2 = 0;
    EXPECT_TRUE(writer2.NextAccountIdSequence(next_id2).ok());
    EXPECT_EQ(next_id2, 2);
    // Add another account
    EXPECT_TRUE(
        writer2.PutAccount(AccountBuilder().set_account_id(next_id2).set_name("Second").Build())
            .ok());
    ASSERT_TRUE(writer2.Commit().ok());
  }

  {
    // Next should be incremented again
    auto writer3 = db.CreateWriter();
    uint64_t next_id3 = 0;
    EXPECT_TRUE(writer3.NextAccountIdSequence(next_id3).ok());
    EXPECT_EQ(next_id3, 3);
  }
}

TEST(DbTest, NextPositionIdSequence) {
  Db db;
  {
    auto writer = db.CreateWriter();
    int32_t next_id = 0;
    // Should start from 1
    EXPECT_TRUE(writer.NextPositionIdSequence(next_id).ok());
    EXPECT_EQ(next_id, 1);
    // Add a position
    EXPECT_TRUE(
        writer.PutPosition(PositionBuilder().set_position_id(next_id).set_account_id(1).Build())
            .ok());
    ASSERT_TRUE(writer.Commit().ok());
  }

  // Next should be incremented
  {
    auto writer2 = db.CreateWriter();
    int32_t next_id2 = 0;
    EXPECT_TRUE(writer2.NextPositionIdSequence(next_id2).ok());
    EXPECT_EQ(next_id2, 2);
    // Add another position
    EXPECT_TRUE(
        writer2.PutPosition(PositionBuilder().set_position_id(next_id2).set_account_id(2).Build())
            .ok());
    ASSERT_TRUE(writer2.Commit().ok());
  }

  // Next should be incremented again
  {
    auto writer3 = db.CreateWriter();
    int32_t next_id3 = 0;
    EXPECT_TRUE(writer3.NextPositionIdSequence(next_id3).ok());
    EXPECT_EQ(next_id3, 3);
  }
}

TEST(DbTest, NextAccountIdSequence_NotCommitted) {
  Db db;
  {
    auto writer = db.CreateWriter();
    uint64_t next_id = 0;
    EXPECT_TRUE(writer.NextAccountIdSequence(next_id).ok());
    EXPECT_EQ(next_id, 1);
    EXPECT_TRUE(
        writer.PutAccount(AccountBuilder().set_account_id(next_id).set_name("Uncommitted").Build())
            .ok());
    // Do not commit
  }
  // Sequence should not be incremented
  auto writer2 = db.CreateWriter();
  uint64_t next_id2 = 0;
  EXPECT_TRUE(writer2.NextAccountIdSequence(next_id2).ok());
  EXPECT_EQ(next_id2, 1);
}

TEST(DbTest, UnknownFieldsSurviveUpdate) {
  Db db;
  {
    auto writer = db.CreateWriter();
    AccountBuilder builder;
    builder.set_account_id(50).set_name("Eve");
    builder.AddStringField(1000, "from a newer schema");
    EXPECT_TRUE(writer.PutAccount(builder.Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  {
    auto writer = db.CreateWriter();
    EXPECT_TRUE(writer.UpdateAccount(50, AccountPatchBuilder().set_name("Evelyn").Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  auto snapshot = db.CreateSnapshot();
  Account account;
  ASSERT_TRUE(snapshot.GetAccount(50, account).ok());
  EXPECT_EQ(account.name(), "Evelyn");
  EXPECT_EQ(account.View().GetString(1000), "from a newer schema");
}

TEST(DbTest, IndexFollowsPatchesOfIndexedField) {
  Db db;
  {
    auto writer = db.CreateWriter();
    EXPECT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(60).set_age(20).Build()).ok());
    EXPECT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(61).set_age(20).Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  {
    auto writer = db.CreateWriter();
    // Moves to another key, leaves the index, and does not touch the indexed field.
    EXPECT_TRUE(writer.UpdateAccount(60, AccountPatchBuilder().set_age(21).Build()).ok());
    EXPECT_TRUE(writer.UpdateAccount(61, AccountPatchBuilder().clear_age().Build()).ok());
    EXPECT_TRUE(writer.UpdateAccount(60, AccountPatchBuilder().set_name("x").Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  auto snapshot = db.CreateSnapshot();
  auto ids_with_age = [&](int32_t age) {
    std::vector<uint64_t> ids;
    for (auto it = snapshot.GetAccountByAgeEqual(age); it.Valid(); it.Next()) {
      ids.push_back(it.Value().account_id());
    }
    return ids;
  };
  EXPECT_THAT(ids_with_age(20), ::testing::IsEmpty());
  EXPECT_THAT(ids_with_age(21), ::testing::ElementsAre(60));
}

// The whole path a value takes: generated builder, transaction, secondary index, sequence, patch,
// commit, and generated read.
TEST(DbTest, EndToEndWorkflow) {
  Db db;
  int32_t position_id = 0;
  {
    auto writer = db.CreateWriter();
    uint64_t account_id = 0;
    ASSERT_TRUE(writer.NextAccountIdSequence(account_id).ok());
    const std::string name = "Grace";
    AccountBuilder account;
    account.set_account_id(account_id).set_name(name).set_age(33).set_is_active(true);
    ASSERT_TRUE(writer.PutAccount(account.Build()).ok());

    ASSERT_TRUE(writer.NextPositionIdSequence(position_id).ok());
    PositionBuilder position;
    position.set_position_id(position_id)
        .set_account_id(static_cast<int32_t>(account_id))
        .set_instrument("EURUSD")
        .set_direction(Direction::kBuy);
    ASSERT_TRUE(writer.PutPosition(position.Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  {
    auto writer = db.CreateWriter();
    ASSERT_TRUE(writer
                    .UpdatePosition(position_id, PositionPatchBuilder()
                                                     .set_instrument("GBPUSD-long")
                                                     .set_direction(Direction::kSell)
                                                     .set_volume(250)
                                                     .Build())
                    .ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  auto snapshot = db.CreateSnapshot();
  auto positions = snapshot.GetPositionByAccountIdEqual(1);
  ASSERT_TRUE(positions.Valid());
  auto position = positions.Value();
  EXPECT_EQ(position.instrument(), "GBPUSD-long");
  EXPECT_EQ(position.direction(), Direction::kSell);
  EXPECT_EQ(position.volume(), 250);

  Account account;
  ASSERT_TRUE(snapshot.GetAccount(1, account).ok());
  EXPECT_EQ(account.name(), "Grace");
  EXPECT_TRUE(account.is_active());
  positions.Next();
  EXPECT_FALSE(positions.Valid());
}

TEST(DbTest, DeleteRemovesRecordAndIndexEntry) {
  Db db;
  {
    auto writer = db.CreateWriter();
    ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(70).set_age(40).Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  {
    auto writer = db.CreateWriter();
    ASSERT_TRUE(writer.DeleteAccount(70).ok());
    Account account;
    EXPECT_TRUE(absl::IsNotFound(writer.GetAccount(70, account)));
    EXPECT_FALSE(writer.GetAccountByAgeEqual(40).Valid());
    EXPECT_TRUE(absl::IsNotFound(writer.DeleteAccount(70)));
    ASSERT_TRUE(writer.Commit().ok());
  }
  auto snapshot = db.CreateSnapshot();
  Account account;
  EXPECT_TRUE(absl::IsNotFound(snapshot.GetAccount(70, account)));
  EXPECT_FALSE(snapshot.GetAccountByAgeEqual(40).Valid());
}

TEST(DbTest, PutOverwriteMovesIndexEntry) {
  Db db;
  auto writer = db.CreateWriter();
  ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(71).set_age(1).Build()).ok());
  ASSERT_TRUE(writer.Commit().ok());
  ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(71).set_age(2).Build()).ok());
  ASSERT_TRUE(writer.Commit().ok());
  EXPECT_FALSE(writer.GetAccountByAgeEqual(1).Valid());
  EXPECT_TRUE(writer.GetAccountByAgeEqual(2).Valid());
}

// A loop that applies changes sees the effect of its earlier iterations, before any commit.
TEST(DbTest, WriterSeesItsOwnChangesAcrossALoopBeforeCommit) {
  Db db;
  auto writer = db.CreateWriter();
  ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(1).set_age(0).Build()).ok());
  for (int32_t i = 1; i <= 3; ++i) {
    Account account;
    ASSERT_TRUE(writer.GetAccount(1, account).ok());
    EXPECT_EQ(account.age(), i - 1);
    ASSERT_TRUE(writer.UpdateAccount(1, AccountPatchBuilder().set_age(i).Build()).ok());
  }
  ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(2).set_age(3).Build()).ok());
  ASSERT_TRUE(writer.DeleteAccount(2).ok());
  ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(3).set_age(3).Build()).ok());

  Account account;
  EXPECT_FALSE(writer.GetAccount(2, account).ok());
  std::vector<uint64_t> by_age;
  for (auto it = writer.GetAccountByAgeEqual(3); it.Valid(); it.Next()) {
    by_age.push_back(it.Value().account_id());
  }
  EXPECT_THAT(by_age, ::testing::UnorderedElementsAre(1u, 3u));

  // Nobody else sees any of it yet.
  EXPECT_FALSE(db.CreateSnapshot().GetAccount(1, account).ok());
  ASSERT_TRUE(writer.Commit().ok());
  EXPECT_TRUE(db.CreateSnapshot().GetAccount(1, account).ok());
}

TEST(DbTest, ScanSeesEveryRecordOfTheSnapshot) {
  Db db;
  {
    auto writer = db.CreateWriter();
    for (uint64_t id = 1; id <= 100; ++id) {
      ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(id).Build()).ok());
    }
    ASSERT_TRUE(writer.Commit().ok());
  }
  auto snapshot = db.CreateSnapshot();
  {
    auto writer = db.CreateWriter();
    ASSERT_TRUE(writer.DeleteAccount(1).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  size_t count = 0;
  for (auto it = snapshot.ScanAccount(); it.Valid(); it.Next()) ++count;
  EXPECT_EQ(count, 100u);
}

TEST(SnapshotTest, IsolatedFromLaterCommitsUntilDestroyed) {
  Db db;
  const auto put = [&](int32_t age) {
    auto writer = db.CreateWriter();
    ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(1).set_age(age).Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
  };
  put(10);
  {
    auto snapshot = db.CreateSnapshot();
    Account old_account;
    ASSERT_TRUE(snapshot.GetAccount(1, old_account).ok());

    for (int32_t age = 11; age < 20; ++age) put(age);
    auto writer = db.CreateWriter();
    ASSERT_TRUE(writer.DeleteAccount(1).ok());
    ASSERT_TRUE(writer.Commit().ok());

    // The view from before the commits still reads its bytes, and new reads agree with it.
    EXPECT_EQ(old_account.age(), 10);
    Account again;
    ASSERT_TRUE(snapshot.GetAccount(1, again).ok());
    EXPECT_EQ(again.age(), 10);
    EXPECT_TRUE(snapshot.GetAccountByAgeEqual(10).Valid());
    EXPECT_FALSE(snapshot.GetAccountByAgeEqual(15).Valid());
    EXPECT_GT(db.engine().Stats().pending_garbage, 0u);
    EXPECT_EQ(db.engine().Stats().active_snapshots, 1u);
  }
  // The next commits reclaim everything the snapshot was keeping, including the deleted record.
  put(30);
  put(31);
  put(32);
  EXPECT_LE(db.engine().Stats().pending_garbage, 3u);
}

// Transfers keep the sum of two records constant, so a torn read shows up as a different sum.
// Most useful under ThreadSanitizer.
TEST(SnapshotTest, ConcurrentReadersOnlySeeWholeCommits) {
  Db db;
  const auto put_balances = [&](ScopedWrite& writer, int32_t a, int32_t b) {
    ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(1).set_age(a).Build()).ok());
    ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(2).set_age(b).Build()).ok());
  };
  {
    auto writer = db.CreateWriter();
    put_balances(writer, 500, 500);
    ASSERT_TRUE(writer.Commit().ok());
  }
  std::atomic<bool> done{false};
  std::atomic<int> torn{0};
  std::vector<std::thread> readers;
  for (int i = 0; i < 4; ++i) {
    readers.emplace_back([&] {
      while (!done.load(std::memory_order_relaxed)) {
        auto snapshot = db.CreateSnapshot();
        Account a, b;
        if (!snapshot.GetAccount(1, a).ok() || !snapshot.GetAccount(2, b).ok() ||
            a.age() + b.age() != 1000) {
          torn.fetch_add(1);
        }
      }
    });
  }
  for (int32_t i = 0; i < 2000; ++i) {
    auto writer = db.CreateWriter();
    put_balances(writer, 500 + i % 100, 500 - i % 100);
    // New keys force the hash table to grow under the readers.
    const std::string name = std::to_string(i);
    ASSERT_TRUE(writer.PutConfig(ConfigBuilder().set_config_name(name).Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  done = true;
  for (auto& reader : readers) reader.join();
  EXPECT_EQ(torn.load(), 0);
}

// Deleting and re-adding records makes the writer erase table slots and unlink index entries while
// readers walk them. Most useful under ThreadSanitizer and AddressSanitizer.
TEST(SnapshotTest, ConcurrentReadersSurviveReclamation) {
  Db db;
  std::atomic<bool> done{false};
  std::atomic<int> wrong{0};
  std::vector<std::thread> readers;
  for (int i = 0; i < 4; ++i) {
    readers.emplace_back([&] {
      while (!done.load(std::memory_order_relaxed)) {
        auto snapshot = db.CreateSnapshot();
        // Every commit leaves exactly 10 records, each with an age equal to its ID's cycle.
        int rows = 0;
        for (auto it = snapshot.GetAccountByAgeRange(0, 1000); it.Valid(); it.Next()) {
          rows += it.Value().has_account_id() ? 1 : 0;
        }
        int scanned = 0;
        for (auto it = snapshot.ScanAccount(); it.Valid(); it.Next()) ++scanned;
        if (snapshot.GetAccountByAgeRange(0, 1).Valid() || (rows != 0 && rows != 10) ||
            (scanned != 0 && scanned != 10)) {
          wrong.fetch_add(1);
        }
      }
    });
  }
  uint64_t first_id = 0;
  for (int32_t cycle = 1; cycle <= 1000; ++cycle) {
    auto writer = db.CreateWriter();
    if (cycle > 1) {
      for (uint64_t id = first_id; id < first_id + 10; ++id) {
        ASSERT_TRUE(writer.DeleteAccount(id).ok());
      }
    }
    // Half of the keys come back under the same ID, half are new.
    first_id = cycle % 2 == 0 ? first_id : first_id + 5;
    for (uint64_t id = first_id; id < first_id + 10; ++id) {
      AccountBuilder builder;
      builder.set_account_id(id).set_age(1 + cycle % 900);
      ASSERT_TRUE(writer.PutAccount(builder.Build()).ok());
    }
    ASSERT_TRUE(writer.Commit().ok());
  }
  done = true;
  for (auto& reader : readers) reader.join();
  EXPECT_EQ(wrong.load(), 0);
}

// There are only so many slots for snapshots: the rest must be just as safe.
TEST(SnapshotTest, MoreSnapshotsThanSlotsStayIsolated) {
  Db db;
  const auto put = [&db](int32_t age) {
    auto writer = db.CreateWriter();
    ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(1).set_age(age).Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
  };
  put(0);
  std::vector<Snapshot> snapshots;
  for (int32_t i = 1; i <= 200; ++i) {
    snapshots.push_back(db.CreateSnapshot());
    put(i);
  }
  EXPECT_EQ(db.engine().Stats().active_snapshots, 200u);
  for (size_t i = 0; i < snapshots.size(); ++i) {
    Account account;
    ASSERT_TRUE(snapshots[i].GetAccount(1, account).ok());
    EXPECT_EQ(account.age(), static_cast<int32_t>(i));
  }
  snapshots.clear();
  EXPECT_EQ(db.engine().Stats().active_snapshots, 0u);
  // Index entries take two commits to go: one to unlink them, and one to free them.
  put(1000);
  put(1001);
  EXPECT_LE(db.engine().Stats().pending_garbage, 3u);
}

// Snapshots opened and closed on many threads, more at once than there are slots, while versions
// they could be reading are reclaimed. For ThreadSanitizer above all.
TEST(SnapshotTest, ManyThreadsPinningThroughSlotsAndOverflowDuringReclamation) {
  Db db;
  {
    auto writer = db.CreateWriter();
    ASSERT_TRUE(
        writer.PutAccount(AccountBuilder().set_account_id(1).set_age(1).set_balance(1.f).Build())
            .ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  std::atomic<bool> done{false};
  std::atomic<int> torn{0};
  std::vector<std::thread> readers;
  for (int r = 0; r < 8; ++r) {
    readers.emplace_back([&] {
      while (!done.load()) {
        std::vector<Snapshot> held;
        for (int i = 0; i < 12; ++i) held.push_back(db.CreateSnapshot());
        for (const Snapshot& snapshot : held) {
          Account account;
          if (!snapshot.GetAccount(1, account).ok() ||
              static_cast<float>(account.age()) != account.balance()) {
            torn.fetch_add(1);
          }
        }
      }
    });
  }
  for (int32_t i = 2; i < 3000; ++i) {
    auto writer = db.CreateWriter();
    ASSERT_TRUE(
        writer.UpdateAccount(1, AccountPatchBuilder().set_age(i).set_balance(float(i)).Build())
            .ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  done = true;
  for (auto& reader : readers) reader.join();
  EXPECT_EQ(torn.load(), 0);
  EXPECT_EQ(db.engine().Stats().active_snapshots, 0u);
}

TEST(TransactionTest, RollbackDiscardsEverything) {
  Db db;
  {
    auto writer = db.CreateWriter();
    ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(1).set_age(5).Build()).ok());
    uint64_t id = 0;
    ASSERT_TRUE(writer.NextAccountIdSequence(id).ok());
    writer.Rollback();
    Account account;
    EXPECT_FALSE(writer.GetAccount(1, account).ok());
    EXPECT_FALSE(writer.GetAccountByAgeEqual(5).Valid());
    uint64_t again = 0;
    ASSERT_TRUE(writer.NextAccountIdSequence(again).ok());
    EXPECT_EQ(again, id);  // No gap.
  }
  EXPECT_EQ(db.engine().Stats().committed_seq, 0u);
}
