#include "generated/database.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "metadata.fbs.h"

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
    writer.Commit();
  }
  {
    auto guard = db.SharedLock();
    Account account;
    EXPECT_TRUE(guard.GetAccount(1, account).ok());
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
    writer.Commit();
  }
  {
    auto guard = db.SharedLock();
    Position position;
    EXPECT_TRUE(guard.GetPosition(10, position).ok());
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
    writer.Commit();
  }
  {
    auto writer = db.CreateWriter();
    auto patch = AccountPatchBuilder().set_balance(200.0f).set_name("Robert").Build();
    EXPECT_TRUE(writer.UpdateAccount(2, patch).ok());
    writer.Commit();
  }
  {
    auto guard = db.SharedLock();
    Account account;
    EXPECT_TRUE(guard.GetAccount(2, account).ok());
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
    writer.Commit();
  }
  {
    auto writer = db.CreateWriter();
    auto patch = PositionPatchBuilder().set_open_price(101.5f).set_instrument("MSFT").Build();
    EXPECT_TRUE(writer.UpdatePosition(20, patch).ok());
    writer.Commit();
  }
  {
    auto guard = db.SharedLock();
    Position position;
    EXPECT_TRUE(guard.GetPosition(20, position).ok());
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
    auto guard = db.SharedLock();
    Account account;
    // Should not find the uncommitted account
    EXPECT_FALSE(guard.GetAccount(3, account).ok());
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
      auto guard = db.SharedLock();
      Account account;
      // Should not find the uncommitted account
      EXPECT_FALSE(guard.GetAccount(5, account).ok());
    }
    writer.Commit();
  }
  {
    auto guard = db.SharedLock();
    Account account;
    // Now it's visible.
    EXPECT_TRUE(guard.GetAccount(5, account).ok());
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

int main(int argc, char **argv) {
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

    writer.Commit();
  }
  {
    auto guard = db.SharedLock();
    auto it = guard.GetAccountByAgeRange(21, 31);
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

    writer.Commit();
  }
  {
    auto guard = db.SharedLock();
    auto it = guard.GetAccountByAgeEqual(40);
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

    writer.Commit();
  }
  {
    auto guard = db.SharedLock();
    auto it = guard.GetPositionByAccountIdRange(11, 21);
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

    writer.Commit();
  }
  {
    auto guard = db.SharedLock();
    auto it = guard.GetPositionByAccountIdEqual(30);
    std::vector<std::string> instruments;
    while (it.Valid()) {
      auto position = it.Value();
      instruments.emplace_back(position.instrument());
      it.Next();
    }
    EXPECT_THAT(instruments, ::testing::UnorderedElementsAre("TSLA", "AMZN"));
  }
}

TEST(DbTest, PositionByAccountIdIndex_UncommittedNotVisibleToGuard) {
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
    auto guard = db.SharedLock();
    auto it = guard.GetPositionByAccountIdEqual(50);
    // Should not see the uncommitted position
    EXPECT_FALSE(it.Valid());
  }
  writer.Commit();
  // Now changes are committed and should be visible to all readers.
  {
    auto guard = db.SharedLock();
    auto it = guard.GetPositionByAccountIdEqual(50);
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
