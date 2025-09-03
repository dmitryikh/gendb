#include <gtest/gtest.h>

#include "AccountFb.h"
#include "PositionFb.h"
#include "gendb/bits.h"

using namespace gendb::tests;

class AccountFbPatchTest : public ::testing::Test {
 protected:
  AccountFb account;
  std::vector<uint8_t> buffer;
  void SetUp() override {
    AccountFbBuilder builder;
    builder.set_account_id(42);
    builder.set_age(30);
    builder.set_balance(100.0f);
    builder.set_is_active(true);
    buffer = builder.Build();
    account = AccountFb(buffer);
  }
};

TEST_F(AccountFbPatchTest, ModifyFixedSizeField) {
  auto patch = AccountFbPatchBuilder().set_account_id(24).BuildPatch();
  EXPECT_TRUE(account.CanApplyPatchInplace(patch));
  EXPECT_TRUE(account.ApplyPatchInplace(patch));
  EXPECT_EQ(account.account_id(), 24);
}

TEST_F(AccountFbPatchTest, ModifyNonFixedSizeField) {
  auto patch = AccountFbPatchBuilder().set_name("TestName").BuildPatch();
  EXPECT_FALSE(account.CanApplyPatchInplace(patch));
}

TEST_F(AccountFbPatchTest, RemoveFixedSizeField) {
  auto patch = AccountFbPatchBuilder().clear_account_id().BuildPatch();
  EXPECT_FALSE(account.CanApplyPatchInplace(patch));
}

TEST_F(AccountFbPatchTest, RemoveNonFixedSizeField) {
  auto patch = AccountFbPatchBuilder().clear_name().BuildPatch();
  EXPECT_TRUE(account.CanApplyPatchInplace(patch));
  EXPECT_TRUE(account.ApplyPatchInplace(patch));
  EXPECT_TRUE(account.name().empty());
}

TEST_F(AccountFbPatchTest, ModifyAndRemoveFields) {
  auto patch = AccountFbPatchBuilder().set_age(31).set_balance(200.0f).clear_name().BuildPatch();
  EXPECT_TRUE(account.CanApplyPatchInplace(patch));
  EXPECT_TRUE(account.ApplyPatchInplace(patch));
  EXPECT_EQ(account.age(), 31);
  EXPECT_EQ(account.balance(), 200.0f);
  EXPECT_TRUE(account.name().empty());
}

TEST_F(AccountFbPatchTest, ModifyAndRemoveExistingFields) {
  auto patch =
      AccountFbPatchBuilder().set_age(31).set_balance(200.0f).clear_account_id().BuildPatch();
  EXPECT_FALSE(account.CanApplyPatchInplace(patch));
}

TEST_F(AccountFbPatchTest, ModifyNonFixedAndRemoveFixed) {
  auto patch = AccountFbPatchBuilder().set_name("TestName").clear_account_id().BuildPatch();
  EXPECT_FALSE(account.CanApplyPatchInplace(patch));
}

TEST_F(AccountFbPatchTest, ModifyAndRemoveNonFixed) {
  auto patch = AccountFbPatchBuilder().set_name("TestName").clear_config_name().BuildPatch();
  EXPECT_FALSE(account.CanApplyPatchInplace(patch));
}

TEST(PositionFbTest, SetGetFields) {
  PositionFbBuilder builder;
  builder.set_account_id(2002);
  builder.set_instrument("EURUSD");
  builder.set_open_price(1.2345f);
  builder.set_position_id(42);
  builder.set_volume(100);
  std::vector<uint8_t> buffer = builder.Build();
  PositionFb position{buffer};

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

TEST(AccountFbTest, EmptyMessageHasNoFields) {
  AccountFbBuilder builder;
  std::vector<uint8_t> buffer = builder.Build();
  AccountFb account{buffer};

  EXPECT_FALSE(account.has_account_id());
  EXPECT_FALSE(account.has_is_active());
}

TEST(AccountFbTest, EmptyMessageReturnsDefaults) {
  AccountFbBuilder builder;
  std::vector<uint8_t> buffer = builder.Build();
  AccountFb account{buffer};

  EXPECT_EQ(account.account_id(), 0);
  EXPECT_EQ(account.is_active(), false);
}

TEST(AccountFbTest, AllFieldsSetGet) {
  AccountFbBuilder builder;
  builder.set_account_id(42);
  builder.set_trader_id("T123");
  builder.set_name("Bob");
  builder.set_address("123 Main St");
  builder.set_age(30);
  builder.set_is_active(true);
  builder.set_balance(1000.5f);
  builder.set_config_name("default");
  std::vector<uint8_t> buffer = builder.Build();
  AccountFb account{buffer};

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

TEST(PositionFbTest, AllFieldsSetGet) {
  PositionFbBuilder builder;
  builder.set_position_id(101);
  builder.set_account_id(202);
  builder.set_volume(500);
  builder.set_instrument("AAPL");
  builder.set_open_price(123.45f);
  std::vector<uint8_t> buffer = builder.Build();
  PositionFb position{buffer};

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

TEST(AccountFbTest, PartialUpdate) {
  AccountFbBuilder builder;
  builder.set_account_id(1);
  builder.set_name("User1");
  builder.set_is_active(false);
  std::vector<uint8_t> buffer = builder.Build();
  AccountFb account{buffer};

  AccountFbBuilder patch_builder{account};
  patch_builder.set_is_active(true);
  patch_builder.set_name("User2");
  std::vector<uint8_t> patch_buffer = patch_builder.Build();
  AccountFb patch_account{patch_buffer};

  EXPECT_EQ(patch_account.account_id(), 1);
  EXPECT_EQ(patch_account.is_active(), true);
  EXPECT_EQ(patch_account.name(), "User2");
}

TEST(PositionFbTest, PartialUpdate) {
  PositionFbBuilder builder;
  builder.set_position_id(10);
  builder.set_account_id(20);
  builder.set_volume(30);
  builder.set_instrument("IBM");
  builder.set_open_price(99.99f);
  std::vector<uint8_t> buffer = builder.Build();
  PositionFb position{buffer};

  PositionFbBuilder patch_builder{position};
  patch_builder.set_open_price(101.01f);
  patch_builder.set_instrument("GOOG");
  std::vector<uint8_t> patch_buffer = patch_builder.Build();
  PositionFb patch_position{patch_buffer};

  EXPECT_EQ(patch_position.position_id(), 10);
  EXPECT_EQ(patch_position.account_id(), 20);
  EXPECT_EQ(patch_position.volume(), 30);
  EXPECT_EQ(patch_position.open_price(), 101.01f);
  EXPECT_EQ(patch_position.instrument(), "GOOG");
}

TEST(AccountFbTest, PatchWithUnknownFields) {
  AccountFbBuilder builder;
  builder.set_account_id(123456);
  builder.set_is_active(false);
  builder.set_name("Test User");
  std::vector<uint8_t> buffer = builder.Build();
  AccountFb account{buffer};

  {
    gendb::MessageBuilder patch_builder{account};
    patch_builder.AddStringField(6, "New Field#6");
    patch_builder.AddField<int32_t>(8, 321);
    std::vector<uint8_t> patch_buffer = patch_builder.Build();
    AccountFb patch_account{patch_buffer};

    // Known fields are accessible as usual.
    EXPECT_EQ(patch_account.account_id(), 123456);
    EXPECT_EQ(patch_account.is_active(), false);
    EXPECT_EQ(patch_account.name(), "Test User");

    // Unknown fields can be accessible by the base API.
    EXPECT_EQ(patch_account.ReadScalarField<int32_t>(8, 0), 321);
    EXPECT_EQ(patch_account.ReadStringField(6, ""), "New Field#6");
  }
}

TEST(AccountFbTest, PatchUnknownField) {
  AccountFbBuilder builder;
  builder.set_account_id(123456);
  builder.set_is_active(false);
  builder.set_name("Test User");
  builder.AddStringField(6, "Unknown Field#6");
  std::vector<uint8_t> buffer = builder.Build();
  AccountFb account{buffer};

  {
    gendb::MessageBuilder patch_builder{account};
    patch_builder.AddStringField(6, "Patched Field#6");
    std::vector<uint8_t> patch_buffer = patch_builder.Build();
    AccountFb patch_account{patch_buffer};

    // Known fields are accessible as usual.
    EXPECT_EQ(patch_account.account_id(), 123456);
    EXPECT_EQ(patch_account.is_active(), false);
    EXPECT_EQ(patch_account.name(), "Test User");

    // Unknown fields.
    EXPECT_EQ(patch_account.ReadStringField(6, ""), "Patched Field#6");
  }
}

TEST(AccountFbTest, ApplyPatch_ModifyFixedField) {
  AccountFbBuilder builder;
  builder.set_account_id(42);
  builder.set_age(30);
  builder.set_balance(100.0f);
  builder.set_is_active(true);
  std::vector<uint8_t> buffer = builder.Build();
  AccountFb account(buffer);

  auto patch = AccountFbPatchBuilder().set_balance(200.0f).BuildPatch();
  std::vector<uint8_t> patched_buffer;
  account.ApplyPatch(patch, patched_buffer);
  AccountFb patched(patched_buffer);
  EXPECT_EQ(patched.balance(), 200.0f);
  EXPECT_EQ(patched.account_id(), 42);
  EXPECT_EQ(patched.age(), 30);
  EXPECT_EQ(patched.is_active(), true);
}

TEST(AccountFbTest, ApplyPatch_ModifyNonFixedField) {
  AccountFbBuilder builder;
  builder.set_account_id(42);
  builder.set_name("OldName");
  std::vector<uint8_t> buffer = builder.Build();
  AccountFb account(buffer);

  auto patch = AccountFbPatchBuilder().set_name("NewName").BuildPatch();
  std::vector<uint8_t> patched_buffer;
  account.ApplyPatch(patch, patched_buffer);
  AccountFb patched(patched_buffer);
  EXPECT_EQ(patched.name(), "NewName");
  EXPECT_EQ(patched.account_id(), 42);
}

TEST(AccountFbTest, ApplyPatch_RemoveFixedField) {
  AccountFbBuilder builder;
  builder.set_account_id(42);
  builder.set_age(30);
  std::vector<uint8_t> buffer = builder.Build();
  AccountFb account(buffer);

  auto patch = AccountFbPatchBuilder().clear_age().BuildPatch();
  std::vector<uint8_t> patched_buffer;
  account.ApplyPatch(patch, patched_buffer);
  AccountFb patched(patched_buffer);
  EXPECT_FALSE(patched.has_age());
  EXPECT_EQ(patched.account_id(), 42);
}

TEST(AccountFbTest, ApplyPatch_RemoveNonFixedField) {
  AccountFbBuilder builder;
  builder.set_account_id(42);
  builder.set_name("OldName");
  std::vector<uint8_t> buffer = builder.Build();
  AccountFb account(buffer);

  auto patch = AccountFbPatchBuilder().clear_name().BuildPatch();
  std::vector<uint8_t> patched_buffer;
  account.ApplyPatch(patch, patched_buffer);
  AccountFb patched(patched_buffer);
  EXPECT_TRUE(patched.name().empty());
  EXPECT_EQ(patched.account_id(), 42);
}

TEST(AccountFbTest, ApplyPatch_ModifyAndRemoveFields) {
  AccountFbBuilder builder;
  builder.set_account_id(42);
  builder.set_age(30);
  builder.set_balance(100.0f);
  builder.set_name("OldName");
  std::vector<uint8_t> buffer = builder.Build();
  AccountFb account(buffer);

  auto patch = AccountFbPatchBuilder().set_balance(200.0f).clear_name().BuildPatch();
  std::vector<uint8_t> patched_buffer;
  account.ApplyPatch(patch, patched_buffer);
  AccountFb patched(patched_buffer);
  EXPECT_EQ(patched.balance(), 200.0f);
  EXPECT_TRUE(patched.name().empty());
  EXPECT_EQ(patched.account_id(), 42);
  EXPECT_EQ(patched.age(), 30);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
