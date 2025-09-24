#include <gtest/gtest.h>

#include "account.fbs.h"
#include "gendb/message_patch.h"
#include "lib/parse_text.h"
#include "position.fbs.h"

using namespace gendb::tests;

class AccountPatchTest : public ::testing::Test {
 protected:
  Account account;
  std::vector<uint8_t> buffer;
  void SetUp() override {
    buffer = ParseText<Account>(R"m(
      account_id: 42
      age: 30
      balance: 100.0
      is_active: true
    )m");
    account = Account(buffer);
  }
};

TEST_F(AccountPatchTest, ModifyFixedSizeField) {
  auto patch = AccountPatchBuilder().set_account_id(24).Build();
  EXPECT_TRUE(CanApplyPatchInplace<Account>(patch, buffer));
  EXPECT_TRUE(ApplyPatchInplace(patch, buffer));
  EXPECT_EQ(account.account_id(), 24);
}

TEST_F(AccountPatchTest, ModifyNonFixedSizeField) {
  auto patch = AccountPatchBuilder().set_name("TestName").Build();
  EXPECT_FALSE(CanApplyPatchInplace<Account>(patch, buffer));
}

TEST_F(AccountPatchTest, RemoveFixedSizeField) {
  auto patch = AccountPatchBuilder().clear_account_id().Build();
  EXPECT_FALSE(CanApplyPatchInplace<Account>(patch, buffer));
}

TEST_F(AccountPatchTest, RemoveNonFixedSizeField) {
  auto patch = AccountPatchBuilder().clear_name().Build();
  EXPECT_TRUE(CanApplyPatchInplace<Account>(patch, buffer));
  EXPECT_TRUE(ApplyPatchInplace(patch, buffer));
  EXPECT_TRUE(account.name().empty());
}

TEST_F(AccountPatchTest, ModifyAndRemoveFields) {
  auto patch = AccountPatchBuilder().set_age(31).set_balance(200.0f).clear_name().Build();
  EXPECT_TRUE(CanApplyPatchInplace<Account>(patch, buffer));
  EXPECT_TRUE(ApplyPatchInplace(patch, buffer));
  EXPECT_EQ(account.age(), 31);
  EXPECT_EQ(account.balance(), 200.0f);
  EXPECT_TRUE(account.name().empty());
}

TEST_F(AccountPatchTest, ModifyAndRemoveExistingFields) {
  auto patch = AccountPatchBuilder().set_age(31).set_balance(200.0f).clear_account_id().Build();
  EXPECT_FALSE(CanApplyPatchInplace<Account>(patch, buffer));
}

TEST_F(AccountPatchTest, ModifyNonFixedAndRemoveFixed) {
  auto patch = AccountPatchBuilder().set_name("TestName").clear_account_id().Build();
  EXPECT_FALSE(CanApplyPatchInplace<Account>(patch, buffer));
}

TEST_F(AccountPatchTest, ModifyAndRemoveNonFixed) {
  auto patch = AccountPatchBuilder().set_name("TestName").clear_config_name().Build();
  EXPECT_FALSE(CanApplyPatchInplace<Account>(patch, buffer));
}

TEST(PositionTest, SetGetFields) {
  auto buffer = ParseText<Position>(R"m(
    account_id: 2002
    instrument: "EURUSD"
    open_price: 1.2345
    position_id: 42
    volume: 100
  )m");
  Position position{buffer};

  EXPECT_EQ(position.account_id(), 2002);
  EXPECT_EQ(position.instrument(), "EURUSD");
  EXPECT_EQ(position.open_price(), 1.2345f);
  EXPECT_EQ(position.position_id(), 42);
  EXPECT_EQ(position.volume(), 100);
  EXPECT_TRUE(position.has_account_id());
  EXPECT_TRUE(position.has_instrument());
  EXPECT_TRUE(position.has_open_price());
  EXPECT_TRUE(position.has_position_id());
  EXPECT_TRUE(position.has_volume());
}

TEST(AccountTest, EmptyMessageHasNoFields) {
  auto buffer = ParseText<Account>("");  // Empty message
  Account account{buffer};

  EXPECT_FALSE(account.has_account_id());
  EXPECT_FALSE(account.has_is_active());
}

TEST(AccountTest, EmptyMessageReturnsDefaults) {
  auto buffer = ParseText<Account>("");  // Empty message
  Account account{buffer};

  EXPECT_EQ(account.account_id(), 0);
  EXPECT_EQ(account.is_active(), false);
}

TEST(AccountTest, AllFieldsSetGet) {
  auto buffer = ParseText<Account>(R"m(
    account_id: 42
    trader_id: "T123"
    name: "Bob"
    address: "123 Main St"
    age: 30
    is_active: true
    balance: 1000.5
    config_name: "default"
  )m");
  Account account{buffer};

  EXPECT_EQ(account.account_id(), 42);
  EXPECT_EQ(account.trader_id(), "T123");
  EXPECT_EQ(account.name(), "Bob");
  EXPECT_EQ(account.address(), "123 Main St");
  EXPECT_EQ(account.age(), 30);
  EXPECT_EQ(account.is_active(), true);
  EXPECT_EQ(account.balance(), 1000.5f);
  EXPECT_EQ(account.config_name(), "default");
  EXPECT_TRUE(account.has_account_id());
  EXPECT_TRUE(account.has_trader_id());
  EXPECT_TRUE(account.has_name());
  EXPECT_TRUE(account.has_address());
  EXPECT_TRUE(account.has_age());
  EXPECT_TRUE(account.has_is_active());
  EXPECT_TRUE(account.has_balance());
  EXPECT_TRUE(account.has_config_name());
}

TEST(PositionTest, AllFieldsSetGet) {
  auto buffer = ParseText<Position>(R"m(
    position_id: 101
    account_id: 202
    volume: 500
    instrument: "AAPL"
    open_price: 123.45
  )m");
  Position position{buffer};

  EXPECT_EQ(position.position_id(), 101);
  EXPECT_EQ(position.account_id(), 202);
  EXPECT_EQ(position.volume(), 500);
  EXPECT_EQ(position.instrument(), "AAPL");
  EXPECT_EQ(position.open_price(), 123.45f);
  EXPECT_TRUE(position.has_position_id());
  EXPECT_TRUE(position.has_account_id());
  EXPECT_TRUE(position.has_volume());
  EXPECT_TRUE(position.has_instrument());
  EXPECT_TRUE(position.has_open_price());
}

TEST(AccountTest, PartialUpdate) {
  AccountBuilder builder;
  builder.set_account_id(1);
  builder.set_name("User1");
  builder.set_is_active(false);
  std::vector<uint8_t> buffer = builder.Build();
  Account account{buffer};

  AccountBuilder patch_builder{account};
  patch_builder.set_is_active(true);
  patch_builder.set_name("User2");
  std::vector<uint8_t> patch_buffer = patch_builder.Build();
  Account patch_account{patch_buffer};

  EXPECT_EQ(patch_account.account_id(), 1);
  EXPECT_EQ(patch_account.is_active(), true);
  EXPECT_EQ(patch_account.name(), "User2");
}

TEST(PositionTest, PartialUpdate) {
  PositionBuilder builder;
  builder.set_position_id(10);
  builder.set_account_id(20);
  builder.set_volume(30);
  builder.set_instrument("IBM");
  builder.set_open_price(99.99f);
  std::vector<uint8_t> buffer = builder.Build();
  Position position{buffer};

  PositionBuilder patch_builder{position};
  patch_builder.set_open_price(101.01f);
  patch_builder.set_instrument("GOOG");
  std::vector<uint8_t> patch_buffer = patch_builder.Build();
  Position patch_position{patch_buffer};

  EXPECT_EQ(patch_position.position_id(), 10);
  EXPECT_EQ(patch_position.account_id(), 20);
  EXPECT_EQ(patch_position.volume(), 30);
  EXPECT_EQ(patch_position.open_price(), 101.01f);
  EXPECT_EQ(patch_position.instrument(), "GOOG");
}

TEST(AccountTest, PatchWithUnknownFields) {
  AccountBuilder builder;
  builder.set_account_id(123456);
  builder.set_is_active(false);
  builder.set_name("Test User");
  std::vector<uint8_t> buffer = builder.Build();
  Account account{buffer};

  // {
  //   gendb::MessageBuilder patch_builder{account};
  //   patch_builder.AddStringField(6, "New Field#6");
  //   patch_builder.AddField<int32_t>(8, 321);
  //   std::vector<uint8_t> patch_buffer = patch_builder.Build();
  //   Account patch_account{patch_buffer};

  //   // Known fields are accessible as usual.
  //   EXPECT_EQ(patch_account.account_id(), 123456);
  //   EXPECT_EQ(patch_account.is_active(), false);
  //   EXPECT_EQ(patch_account.name(), "Test User");

  //   // Unknown fields can be accessible by the base API.
  //   EXPECT_EQ(patch_account.ReadScalarField<int32_t>(8, 0), 321);
  //   EXPECT_EQ(patch_account.ReadStringField(6, ""), "New Field#6");
  // }
}

TEST(AccountTest, PatchUnknownField) {
  AccountBuilder builder;
  builder.set_account_id(123456);
  builder.set_is_active(false);
  builder.set_name("Test User");
  builder.AddStringField(6, "Unknown Field#6");
  std::vector<uint8_t> buffer = builder.Build();
  Account account{buffer};

  // {
  //   gendb::MessageBuilder patch_builder{account};
  //   patch_builder.AddStringField(6, "Patched Field#6");
  //   std::vector<uint8_t> patch_buffer = patch_builder.Build();
  //   Account patch_account{patch_buffer};

  //   // Known fields are accessible as usual.
  //   EXPECT_EQ(patch_account.account_id(), 123456);
  //   EXPECT_EQ(patch_account.is_active(), false);
  //   EXPECT_EQ(patch_account.name(), "Test User");

  //   // Unknown fields.
  //   EXPECT_EQ(patch_account.ReadStringField(6, ""), "Patched Field#6");
  // }
}

TEST(AccountTest, ApplyPatch_ModifyFixedField) {
  AccountBuilder builder;
  builder.set_account_id(42);
  builder.set_age(30);
  builder.set_balance(100.0f);
  builder.set_is_active(true);
  std::vector<uint8_t> buffer = builder.Build();
  Account account(buffer);

  auto patch = AccountPatchBuilder().set_balance(200.0f).Build();
  std::vector<uint8_t> patched_buffer;
  ApplyPatch(patch, buffer, patched_buffer);
  Account patched(patched_buffer);
  EXPECT_EQ(patched.balance(), 200.0f);
  EXPECT_EQ(patched.account_id(), 42);
  EXPECT_EQ(patched.age(), 30);
  EXPECT_EQ(patched.is_active(), true);
}

TEST(AccountTest, ApplyPatch_ModifyNonFixedField) {
  AccountBuilder builder;
  builder.set_account_id(42);
  builder.set_name("OldName");
  std::vector<uint8_t> buffer = builder.Build();
  Account account(buffer);

  auto patch = AccountPatchBuilder().set_name("NewName").Build();
  std::vector<uint8_t> patched_buffer;
  ApplyPatch(patch, buffer, patched_buffer);
  Account patched(patched_buffer);
  EXPECT_EQ(patched.name(), "NewName");
  EXPECT_EQ(patched.account_id(), 42);
}

TEST(AccountTest, ApplyPatch_RemoveFixedField) {
  AccountBuilder builder;
  builder.set_account_id(42);
  builder.set_age(30);
  std::vector<uint8_t> buffer = builder.Build();
  Account account(buffer);

  auto patch = AccountPatchBuilder().clear_age().Build();
  std::vector<uint8_t> patched_buffer;
  ApplyPatch(patch, buffer, patched_buffer);
  Account patched(patched_buffer);
  EXPECT_FALSE(patched.has_age());
  EXPECT_EQ(patched.account_id(), 42);
}

TEST(AccountTest, ApplyPatch_RemoveNonFixedField) {
  AccountBuilder builder;
  builder.set_account_id(42);
  builder.set_name("OldName");
  std::vector<uint8_t> buffer = builder.Build();
  Account account(buffer);

  auto patch = AccountPatchBuilder().clear_name().Build();
  std::vector<uint8_t> patched_buffer;
  ApplyPatch(patch, buffer, patched_buffer);
  Account patched(patched_buffer);
  EXPECT_TRUE(patched.name().empty());
  EXPECT_EQ(patched.account_id(), 42);
}

TEST(AccountTest, ApplyPatch_ModifyAndRemoveFields) {
  AccountBuilder builder;
  builder.set_account_id(42);
  builder.set_age(30);
  builder.set_balance(100.0f);
  builder.set_name("OldName");
  std::vector<uint8_t> buffer = builder.Build();
  Account account(buffer);

  auto patch = AccountPatchBuilder().set_balance(200.0f).clear_name().Build();
  std::vector<uint8_t> patched_buffer;
  ApplyPatch(patch, buffer, patched_buffer);
  Account patched(patched_buffer);
  EXPECT_EQ(patched.balance(), 200.0f);
  EXPECT_TRUE(patched.name().empty());
  EXPECT_EQ(patched.account_id(), 42);
  EXPECT_EQ(patched.age(), 30);
}

TEST(ClosedPositionTest, SetGetFields) {
  auto buffer = ParseText<ClosedPosition>(R"m(
    account_id: 1001
    instrument: "GBPUSD"
    position_id: 55
    profit: 12.34
    volume: 200
  )m");
  ClosedPosition closed_position{buffer};

  EXPECT_EQ(closed_position.account_id(), 1001);
  EXPECT_EQ(closed_position.instrument(), "GBPUSD");
  EXPECT_EQ(closed_position.position_id(), 55);
  EXPECT_EQ(closed_position.profit(), 12.34f);
  EXPECT_EQ(closed_position.volume(), 200);
  EXPECT_TRUE(closed_position.has_account_id());
  EXPECT_TRUE(closed_position.has_instrument());
  EXPECT_TRUE(closed_position.has_position_id());
  EXPECT_TRUE(closed_position.has_profit());
  EXPECT_TRUE(closed_position.has_volume());
}

TEST(ClosedPositionTest, EmptyMessageHasNoFields) {
  auto buffer = ParseText<ClosedPosition>("");  // Empty message
  ClosedPosition closed_position{buffer};

  EXPECT_FALSE(closed_position.has_account_id());
  EXPECT_FALSE(closed_position.has_instrument());
  EXPECT_FALSE(closed_position.has_position_id());
  EXPECT_FALSE(closed_position.has_profit());
  EXPECT_FALSE(closed_position.has_volume());
}

TEST(ClosedPositionTest, EmptyMessageReturnsDefaults) {
  auto buffer = ParseText<ClosedPosition>("");  // Empty message
  ClosedPosition closed_position{buffer};

  EXPECT_EQ(closed_position.account_id(), 0);
  EXPECT_EQ(closed_position.position_id(), 0);
  EXPECT_EQ(closed_position.profit(), 0.0f);
  EXPECT_EQ(closed_position.volume(), 0);
  EXPECT_EQ(closed_position.instrument(), "");
}

TEST(ClosedPositionTest, AllFieldsSetGet) {
  auto buffer = ParseText<ClosedPosition>(R"m(
    account_id: 77
    instrument: "MSFT"
    position_id: 88
    profit: 99.99
    volume: 123
  )m");
  ClosedPosition closed_position{buffer};

  EXPECT_EQ(closed_position.account_id(), 77);
  EXPECT_EQ(closed_position.instrument(), "MSFT");
  EXPECT_EQ(closed_position.position_id(), 88);
  EXPECT_EQ(closed_position.profit(), 99.99f);
  EXPECT_EQ(closed_position.volume(), 123);
  EXPECT_TRUE(closed_position.has_account_id());
  EXPECT_TRUE(closed_position.has_instrument());
  EXPECT_TRUE(closed_position.has_position_id());
  EXPECT_TRUE(closed_position.has_profit());
  EXPECT_TRUE(closed_position.has_volume());
}

TEST(PositionTest, PatchDirectionEnum) {
  auto buffer = ParseText<Position>(R"m(
    position_id: 1
    direction: kBuy
  )m");
  Position position{buffer};

  EXPECT_EQ(position.direction(), Direction::kBuy);
  EXPECT_TRUE(position.has_direction());

  // Change direction and check
  std::vector<uint8_t> patched_buffer;
  ApplyPatch(PositionPatchBuilder{}.set_direction(Direction::kSell).Build(), buffer,
             patched_buffer);
  Position patched{patched_buffer};
  EXPECT_EQ(patched.direction(), Direction::kSell);

  std::vector<uint8_t> patched_buffer2;
  ApplyPatch(PositionPatchBuilder{}.clear_direction().Build(), patched_buffer, patched_buffer2);
  Position cleared{patched_buffer2};
  EXPECT_EQ(cleared.direction(), Direction::kUnknown);
  EXPECT_FALSE(cleared.has_direction());
}

TEST(PositionTest, DefaultDirectionEnum) {
  auto buffer = ParseText<Position>(R"m(
    position_id: 2
  )m");  // Do not set direction
  Position position{buffer};

  EXPECT_EQ(position.direction(), Direction::kUnknown);
  EXPECT_FALSE(position.has_direction());
}

TEST(AccountTest, ParseTextBasic) {
  auto buffer = ParseText<Account>(R"m(
    account_id : 42
    age: 30
    name: "John"
    is_active: true
    balance: 1234.56
  )m");

  Account account(buffer);

  EXPECT_EQ(account.account_id(), 42);
  EXPECT_EQ(account.age(), 30);
  EXPECT_EQ(account.name(), "John");
  EXPECT_EQ(account.is_active(), true);
  EXPECT_EQ(account.balance(), 1234.56f);
}

TEST(ClosedPositionTest, ParseTextGeneric) {
  // This test demonstrates that ParseText works generically without explicit instantiation
  auto buffer = ParseText<ClosedPosition>(R"m(
    position_id: 123
    account_id: 456
    volume: 1000
    instrument: "EURUSD"
    profit: 150.75
  )m");

  ClosedPosition position(buffer);

  EXPECT_EQ(position.position_id(), 123);
  EXPECT_EQ(position.account_id(), 456);
  EXPECT_EQ(position.volume(), 1000);
  EXPECT_EQ(position.instrument(), "EURUSD");
  EXPECT_EQ(position.profit(), 150.75f);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
