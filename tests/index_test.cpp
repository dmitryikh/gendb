// What the generated index queries can do: composite and unique indices, open ranges, reverse order
// and limits, over snapshots and over transactions with uncommitted changes.
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "account.gendb.h"
#include "database.h"
#include "position.gendb.h"

namespace {

using namespace gendb::tests;
using ::testing::ElementsAre;
using ::testing::UnorderedElementsAre;

void PutPosition(ScopedWrite& writer, int32_t id, int32_t account, std::string_view instrument) {
  ASSERT_TRUE(writer
                  .PutPosition(PositionBuilder()
                                   .set_position_id(id)
                                   .set_account_id(account)
                                   .set_instrument(instrument)
                                   .Build())
                  .ok());
}

template <typename Iterator>
std::vector<int32_t> PositionIds(Iterator it) {
  std::vector<int32_t> ids;
  for (; it.Valid(); it.Next()) ids.push_back(it.Value().position_id());
  return ids;
}

template <typename Iterator>
std::vector<int32_t> Ages(Iterator it) {
  std::vector<int32_t> ages;
  for (; it.Valid(); it.Next()) ages.push_back(it.Value().age());
  return ages;
}

class CompositeIndexTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto writer = db.CreateWriter();
    PutPosition(writer, 1, 10, "EURUSD");
    PutPosition(writer, 2, 10, "GBPUSD");
    PutPosition(writer, 3, 10, "GBPUSD");
    PutPosition(writer, 4, 20, "EURUSD");
    PutPosition(writer, 5, 20, "XAUUSD");
    PutPosition(writer, 6, 30, "AAPL");
    ASSERT_TRUE(writer.Commit().ok());
  }
  Db db;
};

TEST_F(CompositeIndexTest, EqualOnAllFieldsAndOnAPrefix) {
  auto snapshot = db.CreateSnapshot();
  EXPECT_THAT(PositionIds(snapshot.GetPositionByAccountAndInstrumentEqual(10, "GBPUSD")),
              ElementsAre(2, 3));
  // Ordered by the rest of the index.
  EXPECT_THAT(PositionIds(snapshot.GetPositionByAccountAndInstrumentEqual(10)),
              ElementsAre(1, 2, 3));
  EXPECT_TRUE(PositionIds(snapshot.GetPositionByAccountAndInstrumentEqual(10, "XAUUSD")).empty());
}

TEST_F(CompositeIndexTest, RangeOverEachFieldGivenTheOnesBeforeIt) {
  auto snapshot = db.CreateSnapshot();
  EXPECT_THAT(PositionIds(snapshot.GetPositionByAccountAndInstrumentRange(10, "F", "Z")),
              ElementsAre(2, 3));
  EXPECT_THAT(PositionIds(snapshot.GetPositionByAccountAndInstrumentRange(20, std::nullopt, "F")),
              ElementsAre(4));
  EXPECT_THAT(PositionIds(snapshot.GetPositionByAccountAndInstrumentRange(20, "F", std::nullopt)),
              ElementsAre(5));
  // A range over the first field runs through all of the second.
  EXPECT_THAT(PositionIds(snapshot.GetPositionByAccountAndInstrumentRange(10, 21)),
              ElementsAre(1, 2, 3, 4, 5));
  EXPECT_THAT(
      PositionIds(snapshot.GetPositionByAccountAndInstrumentRange(std::nullopt, std::nullopt)),
      ElementsAre(1, 2, 3, 4, 5, 6));
}

// With a plain terminator, a zero byte in a string would make these collide.
TEST_F(CompositeIndexTest, ZeroBytesInStringsStayApart) {
  using namespace std::string_literals;
  const std::string with_zero = "a\0b"s;
  {
    auto writer = db.CreateWriter();
    PutPosition(writer, 7, 40, with_zero);
    PutPosition(writer, 8, 40, "a");
    PutPosition(writer, 9, 40, "a\xff");
    ASSERT_TRUE(writer.Commit().ok());
  }
  auto snapshot = db.CreateSnapshot();
  EXPECT_THAT(PositionIds(snapshot.GetPositionByAccountAndInstrumentEqual(40, with_zero)),
              ElementsAre(7));
  EXPECT_THAT(PositionIds(snapshot.GetPositionByAccountAndInstrumentEqual(40, "a")),
              ElementsAre(8));
  EXPECT_THAT(PositionIds(snapshot.GetPositionByAccountAndInstrumentEqual(40)),
              ElementsAre(8, 7, 9));
}

TEST_F(CompositeIndexTest, ARecordWithoutAllFieldsIsNotIndexed) {
  {
    auto writer = db.CreateWriter();
    ASSERT_TRUE(
        writer.PutPosition(PositionBuilder().set_position_id(7).set_account_id(10).Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  EXPECT_THAT(PositionIds(db.CreateSnapshot().GetPositionByAccountAndInstrumentEqual(10)),
              ElementsAre(1, 2, 3));
}

class OrderedQueryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto writer = db.CreateWriter();
    for (int32_t i = 1; i <= 9; ++i) {
      ASSERT_TRUE(
          writer.PutAccount(AccountBuilder().set_account_id(uint64_t(i)).set_age(i * 10).Build())
              .ok());
    }
    ASSERT_TRUE(writer.Commit().ok());
  }
  Db db;
};

TEST_F(OrderedQueryTest, OpenRanges) {
  auto snapshot = db.CreateSnapshot();
  EXPECT_THAT(Ages(snapshot.GetAccountByAgeRange(std::nullopt, 30)), ElementsAre(10, 20));
  EXPECT_THAT(Ages(snapshot.GetAccountByAgeRange(80, std::nullopt)), ElementsAre(80, 90));
  EXPECT_THAT(Ages(snapshot.GetAccountByAgeRange(std::nullopt, std::nullopt)).size(), 9u);
}

TEST_F(OrderedQueryTest, ReverseOrderAndLimit) {
  auto snapshot = db.CreateSnapshot();
  EXPECT_THAT(Ages(snapshot.GetAccountByAgeRange(30, 60, {.reverse = true})),
              ElementsAre(50, 40, 30));
  EXPECT_THAT(Ages(snapshot.GetAccountByAgeRange(std::nullopt, std::nullopt,
                                                 {.reverse = true, .limit = 2})),
              ElementsAre(90, 80));
  EXPECT_THAT(Ages(snapshot.GetAccountByAgeRange(30, std::nullopt, {.limit = 2})),
              ElementsAre(30, 40));
  EXPECT_TRUE(Ages(snapshot.GetAccountByAgeRange(35, 40, {.reverse = true})).empty());
  EXPECT_TRUE(Ages(snapshot.GetAccountByAgeRange(1000, std::nullopt, {.reverse = true})).empty());
}

// A record that left a value and came back has two entries with one key, of which an old snapshot
// sees the first, and a new one the second. Stepping backwards must find the right one.
TEST_F(OrderedQueryTest, ReverseFindsTheVersionASnapshotCanSee) {
  auto old_snapshot = db.CreateSnapshot();
  {
    auto writer = db.CreateWriter();
    ASSERT_TRUE(writer.UpdateAccount(5, AccountPatchBuilder().set_age(55).Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
    ASSERT_TRUE(writer.UpdateAccount(5, AccountPatchBuilder().set_age(50).Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  auto new_snapshot = db.CreateSnapshot();
  const std::vector<int32_t> expected = {60, 50, 40};
  EXPECT_EQ(Ages(old_snapshot.GetAccountByAgeRange(40, 61, {.reverse = true})), expected);
  EXPECT_EQ(Ages(new_snapshot.GetAccountByAgeRange(40, 61, {.reverse = true})), expected);
  // Both directions agree.
  EXPECT_THAT(Ages(new_snapshot.GetAccountByAgeRange(40, 61)), ElementsAre(40, 50, 60));
}

TEST_F(OrderedQueryTest, ATransactionSeesItsChangesInBothDirections) {
  auto writer = db.CreateWriter();
  ASSERT_TRUE(writer.UpdateAccount(5, AccountPatchBuilder().set_age(45).Build()).ok());
  ASSERT_TRUE(writer.DeleteAccount(4).ok());
  ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(100).set_age(35).Build()).ok());
  EXPECT_THAT(Ages(writer.GetAccountByAgeRange(30, 61)), ElementsAre(30, 35, 45, 60));
  EXPECT_THAT(Ages(writer.GetAccountByAgeRange(30, 61, {.reverse = true})),
              ElementsAre(60, 45, 35, 30));
  EXPECT_THAT(Ages(writer.GetAccountByAgeRange(30, 61, {.reverse = true, .limit = 2})),
              ElementsAre(60, 45));
}

TEST(WriterScanTest, SeesCommittedAndUncommittedChanges) {
  Db db;
  auto writer = db.CreateWriter();
  for (uint64_t id = 1; id <= 4; ++id) {
    ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(id).set_age(1).Build()).ok());
  }
  ASSERT_TRUE(writer.Commit().ok());

  ASSERT_TRUE(writer.UpdateAccount(1, AccountPatchBuilder().set_age(2).Build()).ok());
  ASSERT_TRUE(writer.DeleteAccount(2).ok());
  ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(5).set_age(5).Build()).ok());
  ASSERT_TRUE(writer.PutPosition(PositionBuilder().set_position_id(9).Build()).ok());
  ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(6).Build()).ok());
  ASSERT_TRUE(writer.DeleteAccount(6).ok());

  std::vector<std::pair<uint64_t, int32_t>> seen;
  for (auto it = writer.ScanAccount(); it.Valid(); it.Next()) {
    seen.emplace_back(it.Value().account_id(), it.Value().age());
  }
  EXPECT_THAT(seen, UnorderedElementsAre(std::pair(uint64_t{1}, 2), std::pair(uint64_t{3}, 1),
                                         std::pair(uint64_t{4}, 1), std::pair(uint64_t{5}, 5)));
}

class UniqueIndexTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto writer = db.CreateWriter();
    ASSERT_TRUE(writer.PutAccount(Named(1, "a@x")).ok());
    ASSERT_TRUE(writer.PutAccount(Named(2, "b@x")).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  static std::vector<uint8_t> Named(uint64_t id, std::string_view address) {
    return AccountBuilder().set_account_id(id).set_address(address).Build();
  }
  Db db;
};

TEST_F(UniqueIndexTest, FindsTheOneRecord) {
  auto snapshot = db.CreateSnapshot();
  Account account;
  ASSERT_TRUE(snapshot.GetAccountByAddress("b@x", account).ok());
  EXPECT_EQ(account.account_id(), 2u);
  EXPECT_EQ(snapshot.GetAccountByAddress("c@x", account).code(), absl::StatusCode::kNotFound);
}

TEST_F(UniqueIndexTest, RejectsASecondRecordWithTheValue) {
  auto writer = db.CreateWriter();
  EXPECT_EQ(writer.PutAccount(Named(3, "a@x")).code(), absl::StatusCode::kAlreadyExists);
  EXPECT_EQ(writer.UpdateAccount(2, AccountPatchBuilder().set_address("a@x").Build()).code(),
            absl::StatusCode::kAlreadyExists);
  // Nothing about these failed writes stays behind.
  ASSERT_TRUE(writer.Commit().ok());
  Account account;
  auto snapshot = db.CreateSnapshot();
  EXPECT_FALSE(snapshot.GetAccount(3, account).ok());
  ASSERT_TRUE(snapshot.GetAccount(2, account).ok());
  EXPECT_EQ(account.address(), "b@x");
}

TEST_F(UniqueIndexTest, ARecordCanKeepItsOwnValue) {
  auto writer = db.CreateWriter();
  EXPECT_TRUE(writer.PutAccount(Named(1, "a@x")).ok());
  EXPECT_TRUE(writer.UpdateAccount(1, AccountPatchBuilder().set_age(3).Build()).ok());
  EXPECT_TRUE(writer.Commit().ok());
}

TEST_F(UniqueIndexTest, SeesTheTransactionsOwnChanges) {
  auto writer = db.CreateWriter();
  // Taken in this transaction, not yet committed.
  ASSERT_TRUE(writer.PutAccount(Named(3, "c@x")).ok());
  EXPECT_EQ(writer.PutAccount(Named(4, "c@x")).code(), absl::StatusCode::kAlreadyExists);
  // Freed in this transaction, not yet committed.
  ASSERT_TRUE(writer.DeleteAccount(1).ok());
  EXPECT_TRUE(writer.PutAccount(Named(5, "a@x")).ok());
  ASSERT_TRUE(writer.UpdateAccount(2, AccountPatchBuilder().set_address("d@x").Build()).ok());
  EXPECT_TRUE(writer.PutAccount(Named(6, "b@x")).ok());
  ASSERT_TRUE(writer.Commit().ok());

  Account account;
  auto snapshot = db.CreateSnapshot();
  ASSERT_TRUE(snapshot.GetAccountByAddress("a@x", account).ok());
  EXPECT_EQ(account.account_id(), 5u);
  ASSERT_TRUE(snapshot.GetAccountByAddress("b@x", account).ok());
  EXPECT_EQ(account.account_id(), 6u);
}

TEST_F(UniqueIndexTest, RecordsWithoutTheFieldDoNotCollide) {
  auto writer = db.CreateWriter();
  EXPECT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(10).Build()).ok());
  EXPECT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(11).Build()).ok());
  EXPECT_TRUE(writer.Commit().ok());
}

TEST(PutTest, ThePrimaryKeyComesFromTheMessage) {
  Db db;
  auto writer = db.CreateWriter();
  EXPECT_EQ(writer.PutAccount(AccountBuilder().set_name("no key").Build()).code(),
            absl::StatusCode::kInvalidArgument);
  const std::vector<uint8_t> garbage = {0xde, 0xad, 0xbe, 0xef};
  EXPECT_EQ(writer.PutAccount(garbage).code(), absl::StatusCode::kInvalidArgument);
  ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(7).Build()).ok());
  EXPECT_EQ(writer.UpdateAccount(7, AccountPatchBuilder().set_account_id(8).Build()).code(),
            absl::StatusCode::kInvalidArgument);
  ASSERT_TRUE(writer.Commit().ok());
  Account account;
  EXPECT_TRUE(db.CreateSnapshot().GetAccount(7, account).ok());
}

}  // namespace
