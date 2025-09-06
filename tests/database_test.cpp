#include "generated/database.h"

#include <gtest/gtest.h>

using namespace gendb::tests;

TEST(DbTest, AddAndGetAccount) {
  Db db;
  {
    auto writer = db.CreateWriter();
    AccountFbBuilder builder;
    builder.set_account_id(1);
    builder.set_name("Alice");
    std::vector<uint8_t> buffer = builder.Build();
    EXPECT_TRUE(writer.PutAccount(1, buffer).ok());
    writer.Commit();
  }
  {
    auto guard = db.SharedLock();
    AccountFb account;
    EXPECT_TRUE(guard.GetAccount(1, account).ok());
    EXPECT_EQ(account.account_id(), 1);
    EXPECT_EQ(account.name(), "Alice");
  }
}

TEST(DbTest, AddAndGetPosition) {
  Db db;
  {
    auto writer = db.CreateWriter();
    PositionFbBuilder builder;
    builder.set_position_id(10);
    builder.set_account_id(1);
    builder.set_instrument("AAPL");
    std::vector<uint8_t> buffer = builder.Build();
    EXPECT_TRUE(writer.PutPosition(10, buffer).ok());
    writer.Commit();
  }
  {
    auto guard = db.SharedLock();
    PositionFb position;
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
    AccountFbBuilder builder;
    builder.set_account_id(2);
    builder.set_name("Bob");
    builder.set_balance(100.0f);
    std::vector<uint8_t> buffer = builder.Build();
    EXPECT_TRUE(writer.PutAccount(2, buffer).ok());
    writer.Commit();
  }
  {
    auto writer = db.CreateWriter();
    auto patch = AccountFbPatchBuilder().set_balance(200.0f).set_name("Robert").BuildPatch();
    EXPECT_TRUE(writer.UpdateAccount(2, patch).ok());
    writer.Commit();
  }
  {
    auto guard = db.SharedLock();
    AccountFb account;
    EXPECT_TRUE(guard.GetAccount(2, account).ok());
    EXPECT_EQ(account.balance(), 200.0f);
    EXPECT_EQ(account.name(), "Robert");
  }
}

TEST(DbTest, UpdatePositionWithPatch) {
  Db db;
  {
    auto writer = db.CreateWriter();
    PositionFbBuilder builder;
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
    auto patch =
        PositionFbPatchBuilder().set_open_price(101.5f).set_instrument("MSFT").BuildPatch();
    EXPECT_TRUE(writer.UpdatePosition(20, patch).ok());
    writer.Commit();
  }
  {
    auto guard = db.SharedLock();
    PositionFb position;
    EXPECT_TRUE(guard.GetPosition(20, position).ok());
    EXPECT_EQ(position.open_price(), 101.5f);
    EXPECT_EQ(position.instrument(), "MSFT");
  }
}

TEST(DbTest, NoChangesWithoutCommit) {
  Db db;
  {
    auto writer = db.CreateWriter();
    AccountFbBuilder builder;
    builder.set_account_id(3);
    builder.set_name("Charlie");
    std::vector<uint8_t> buffer = builder.Build();
    EXPECT_TRUE(writer.PutAccount(3, buffer).ok());
    // Do not commit
  }
  {
    auto guard = db.SharedLock();
    AccountFb account;
    // Should not find the uncommitted account
    EXPECT_FALSE(guard.GetAccount(3, account).ok());
  }
}

TEST(DbTest, NoYetCommittedChangesNotVisibleFromOtherReaders) {
  Db db;
  {
    auto writer = db.CreateWriter();
    AccountFbBuilder builder;
    builder.set_account_id(5);
    builder.set_name("Eve");
    std::vector<uint8_t> buffer = builder.Build();
    EXPECT_TRUE(writer.PutAccount(5, buffer).ok());
    // Do not commit yet.
    {
      auto guard = db.SharedLock();
      AccountFb account;
      // Should not find the uncommitted account
      EXPECT_FALSE(guard.GetAccount(5, account).ok());
    }
    writer.Commit();
  }
  {
    auto guard = db.SharedLock();
    AccountFb account;
    // Now it's visible.
    EXPECT_TRUE(guard.GetAccount(5, account).ok());
    EXPECT_EQ(account.name(), "Eve");
  }
}

TEST(DbTest, NoYetCommittedChangesVisibleInWriter) {
  Db db;
  {
    auto writer = db.CreateWriter();
    AccountFbBuilder builder;
    builder.set_account_id(4);
    builder.set_name("David");
    std::vector<uint8_t> buffer = builder.Build();
    EXPECT_TRUE(writer.PutAccount(4, buffer).ok());

    AccountFb account;
    EXPECT_TRUE(writer.GetAccount(4, account).ok());
    EXPECT_EQ(account.name(), "David");
  }
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
