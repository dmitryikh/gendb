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

#include "account.fbs.h"
#include "config.fbs.h"
#include "metadata.fbs.h"
#include "position.fbs.h"
#include "session.fbs.h"

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
    EXPECT_TRUE(writer.PutAccount(1, buffer).ok());
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
    EXPECT_TRUE(writer.PutPosition(10, buffer).ok());
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
    EXPECT_TRUE(writer.PutAccount(2, buffer).ok());
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
    EXPECT_TRUE(writer.PutPosition(20, buffer).ok());
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
    EXPECT_TRUE(writer.PutAccount(3, buffer).ok());
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
    EXPECT_TRUE(writer.PutAccount(5, buffer).ok());
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
    EXPECT_TRUE(writer.PutAccount(4, buffer).ok());

    Account account;
    EXPECT_TRUE(writer.GetAccount(4, account).ok());
    EXPECT_EQ(account.name(), "David");
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
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
    EXPECT_TRUE(writer.PutAccount(100, buffer).ok());

    builder.set_account_id(101);
    builder.set_name("Ben");
    builder.set_age(25);
    buffer = builder.Build();
    EXPECT_TRUE(writer.PutAccount(101, buffer).ok());

    builder.set_account_id(102);
    builder.set_name("Cara");
    builder.set_age(30);
    buffer = builder.Build();
    EXPECT_TRUE(writer.PutAccount(102, buffer).ok());

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
    EXPECT_TRUE(writer.PutAccount(200, buffer).ok());

    builder.set_account_id(201);
    builder.set_name("Eve");
    builder.set_age(40);
    buffer = builder.Build();
    EXPECT_TRUE(writer.PutAccount(201, buffer).ok());

    builder.set_account_id(202);
    builder.set_name("Finn");
    builder.set_age(41);
    buffer = builder.Build();
    EXPECT_TRUE(writer.PutAccount(202, buffer).ok());

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
    EXPECT_TRUE(writer.PutPosition(1, buffer).ok());

    builder.set_position_id(2);
    builder.set_account_id(15);
    builder.set_instrument("GOOG");
    buffer = builder.Build();
    EXPECT_TRUE(writer.PutPosition(2, buffer).ok());

    builder.set_position_id(3);
    builder.set_account_id(20);
    builder.set_instrument("MSFT");
    buffer = builder.Build();
    EXPECT_TRUE(writer.PutPosition(3, buffer).ok());

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
    EXPECT_TRUE(writer.PutPosition(4, buffer).ok());

    builder.set_position_id(5);
    builder.set_account_id(30);
    builder.set_instrument("AMZN");
    buffer = builder.Build();
    EXPECT_TRUE(writer.PutPosition(5, buffer).ok());

    builder.set_position_id(6);
    builder.set_account_id(31);
    builder.set_instrument("NFLX");
    buffer = builder.Build();
    EXPECT_TRUE(writer.PutPosition(6, buffer).ok());

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
  EXPECT_TRUE(writer.PutPosition(100, buffer).ok());
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
    EXPECT_TRUE(writer.PutPosition(101, buffer).ok());

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
        writer
            .PutAccount(next_id, AccountBuilder().set_account_id(next_id).set_name("First").Build())
            .ok());
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
        writer2
            .PutAccount(next_id2,
                        AccountBuilder().set_account_id(next_id2).set_name("Second").Build())
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
        writer
            .PutPosition(next_id,
                         PositionBuilder().set_position_id(next_id).set_account_id(1).Build())
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
        writer2
            .PutPosition(next_id2,
                         PositionBuilder().set_position_id(next_id2).set_account_id(2).Build())
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
        writer
            .PutAccount(next_id,
                        AccountBuilder().set_account_id(next_id).set_name("Uncommitted").Build())
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
    EXPECT_TRUE(writer.PutAccount(50, builder.Build()).ok());
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
    EXPECT_TRUE(
        writer.PutAccount(60, AccountBuilder().set_account_id(60).set_age(20).Build()).ok());
    EXPECT_TRUE(
        writer.PutAccount(61, AccountBuilder().set_account_id(61).set_age(20).Build()).ok());
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
    ASSERT_TRUE(writer.PutAccount(account_id, account.Build()).ok());

    ASSERT_TRUE(writer.NextPositionIdSequence(position_id).ok());
    PositionBuilder position;
    position.set_position_id(position_id)
        .set_account_id(static_cast<int32_t>(account_id))
        .set_instrument("EURUSD")
        .set_direction(Direction::kBuy);
    ASSERT_TRUE(writer.PutPosition(position_id, position.Build()).ok());
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
    ASSERT_TRUE(
        writer.PutAccount(70, AccountBuilder().set_account_id(70).set_age(40).Build()).ok());
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
  ASSERT_TRUE(writer.PutAccount(71, AccountBuilder().set_account_id(71).set_age(1).Build()).ok());
  ASSERT_TRUE(writer.Commit().ok());
  ASSERT_TRUE(writer.PutAccount(71, AccountBuilder().set_account_id(71).set_age(2).Build()).ok());
  ASSERT_TRUE(writer.Commit().ok());
  EXPECT_FALSE(writer.GetAccountByAgeEqual(1).Valid());
  EXPECT_TRUE(writer.GetAccountByAgeEqual(2).Valid());
}

TEST(DbTest, ScanSeesEveryRecordOfTheSnapshot) {
  Db db;
  {
    auto writer = db.CreateWriter();
    for (uint64_t id = 1; id <= 100; ++id) {
      ASSERT_TRUE(writer.PutAccount(id, AccountBuilder().set_account_id(id).Build()).ok());
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
    ASSERT_TRUE(writer.PutAccount(1, AccountBuilder().set_account_id(1).set_age(age).Build()).ok());
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
    ASSERT_TRUE(writer.PutAccount(1, AccountBuilder().set_account_id(1).set_age(a).Build()).ok());
    ASSERT_TRUE(writer.PutAccount(2, AccountBuilder().set_account_id(2).set_age(b).Build()).ok());
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
    ASSERT_TRUE(writer.PutConfig("config" + std::to_string(i), ConfigBuilder().Build()).ok());
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
      ASSERT_TRUE(writer.PutAccount(id, builder.Build()).ok());
    }
    ASSERT_TRUE(writer.Commit().ok());
  }
  done = true;
  for (auto& reader : readers) reader.join();
  EXPECT_EQ(wrong.load(), 0);
}

TEST(TransactionTest, RollbackDiscardsEverything) {
  Db db;
  {
    auto writer = db.CreateWriter();
    ASSERT_TRUE(writer.PutAccount(1, AccountBuilder().set_account_id(1).set_age(5).Build()).ok());
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
    ASSERT_TRUE(writer.PutAccount(1, builder.Build()).ok());
    ASSERT_TRUE(writer.PutAccount(2, AccountBuilder().set_account_id(2).set_age(30).Build()).ok());
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
    ASSERT_TRUE(writer.PutSession(1, SessionBuilder().set_session_id(1).Build()).ok());
    ASSERT_TRUE(writer.PutAccount(1, AccountBuilder().set_account_id(1).Build()).ok());
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
    (void)writer.PutAccount(1, AccountBuilder().set_account_id(1).set_age(1).Build());
    (void)writer.PutAccount(2, AccountBuilder().set_account_id(2).set_age(2).Build());
    if (!writer.Commit().ok()) _exit(1);
    (void)writer.PutAccount(3, AccountBuilder().set_account_id(3).Build());
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
        ASSERT_TRUE(
            writer.PutAccount(id, AccountBuilder().set_account_id(id).set_age(7).Build()).ok());
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
    ASSERT_TRUE(
        next.PutAccount(1000, AccountBuilder().set_account_id(1000).set_age(7).Build()).ok());
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
    ASSERT_TRUE(writer.PutAccount(id % 100, AccountBuilder().set_account_id(id).Build()).ok());
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
      ASSERT_TRUE(writer.PutAccount(id, AccountBuilder().set_account_id(id).Build()).ok());
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
  std::filesystem::resize_file(newest, std::filesystem::file_size(newest) - 3);
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
    ASSERT_TRUE(writer.PutAccount(4, AccountBuilder().set_account_id(4).Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  auto db = OpenDb();
  ASSERT_NE(db, nullptr);
  EXPECT_EQ(db->engine().Stats().lsn, 3u);
}

// Cuts the log at every byte: recovery must give exactly the commits that are whole.
TEST_F(PersistentDbTest, LogCutAtAnyByteRecoversTheCommitsBeforeIt) {
  std::vector<uint64_t> commit_ends;
  {
    auto db = OpenDb();
    commit_ends.push_back(db->engine().Stats().wal_bytes);
    for (uint64_t id = 1; id <= 5; ++id) {
      auto writer = db->CreateWriter();
      ASSERT_TRUE(writer.PutAccount(id, AccountBuilder().set_account_id(id).Build()).ok());
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
      ASSERT_TRUE(writer.PutAccount(1, AccountBuilder().set_account_id(1).Build()).ok());
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
              id, AccountBuilder().set_account_id(id).set_age(next).set_name(padding).Build());
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
      ASSERT_TRUE(writer.PutAccount(id, AccountBuilder().set_account_id(id).Build()).ok());
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

// A database that holds a collection the schema lost does not silently drop it.
TEST_F(PersistentDbTest, UnknownCollectionNeedsExplicitDiscard) {
  gendb::EngineSchema schema;
  schema.collections.push_back({"kept"});
  schema.collections.push_back({"removed_later"});
  const std::array<uint8_t, 1> key{1};
  const std::array<uint8_t, 1> value{2};
  {
    auto engine = gendb::Engine::Open(schema, {.path = _path});
    ASSERT_TRUE(engine.ok());
    gendb::Transaction txn(**engine);
    txn.Put(0, key, value);
    txn.Put(1, key, value);
    ASSERT_TRUE(txn.Commit().ok());
  }
  schema.collections.pop_back();
  auto refused = gendb::Engine::Open(schema, {.path = _path});
  EXPECT_EQ(refused.status().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_THAT(refused.status().message(), ::testing::HasSubstr("removed_later"));

  auto engine = gendb::Engine::Open(schema, {.path = _path, .discard_unknown_collections = true});
  ASSERT_TRUE(engine.ok()) << engine.status();
  ASSERT_TRUE((*engine)->Checkpoint().ok());
  engine->reset();
  EXPECT_TRUE(gendb::Engine::Open(schema, {.path = _path}).ok());
}

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
