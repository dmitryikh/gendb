// What the generated index queries can do: composite and unique indices, open ranges, reverse order
// and limits, over snapshots and over transactions with uncommitted changes.
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <random>
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

// What a transaction does to indices is kept apart from them until it commits, in a form that
// depends on how it is used, so this does everything at random against a plain model, and compares
// all it can see.
class IndexModelTest : public ::testing::TestWithParam<uint32_t> {
 protected:
  struct Row {
    int32_t age;
    std::optional<std::string> address;
    std::string name;
  };
  using Model = std::map<uint64_t, Row>;

  // The age and the ID of each record in an index's order, the unique index by value, and all
  // records.
  template <typename Reader>
  void CheckAgainst(Reader& reader, const Model& model, const std::string& where) {
    SCOPED_TRACE(where);
    std::multimap<std::pair<int32_t, std::string>, uint64_t> expected;
    std::vector<std::pair<int32_t, uint64_t>> by_age;
    for (const auto& [id, row] : model) by_age.emplace_back(row.age, id);
    std::vector<std::pair<int32_t, uint64_t>> seen;
    for (auto it = reader.GetAccountByAgeRange(std::nullopt, std::nullopt); it.Valid(); it.Next()) {
      const Account account = it.Value();
      seen.emplace_back(account.age(), account.account_id());
    }
    std::vector<std::pair<int32_t, uint64_t>> sorted_seen = seen;
    std::sort(by_age.begin(), by_age.end());
    std::sort(sorted_seen.begin(), sorted_seen.end());
    ASSERT_EQ(sorted_seen, by_age);
    ASSERT_TRUE(std::is_sorted(seen.begin(), seen.end(),
                               [](const auto& a, const auto& b) { return a.first < b.first; }));
    // Backwards, the same.
    std::vector<std::pair<int32_t, uint64_t>> reversed;
    for (auto it = reader.GetAccountByAgeRange(std::nullopt, std::nullopt, {.reverse = true});
         it.Valid(); it.Next()) {
      reversed.emplace_back(it.Value().age(), it.Value().account_id());
    }
    std::sort(reversed.begin(), reversed.end());
    ASSERT_EQ(reversed, by_age);

    size_t with_address = 0;
    for (const auto& [id, row] : model) {
      if (!row.address.has_value()) continue;
      ++with_address;
      Account found;
      ASSERT_TRUE(reader.GetAccountByAddress(*row.address, found).ok()) << *row.address;
      ASSERT_EQ(found.account_id(), id);
      ASSERT_EQ(found.name(), row.name);
    }
    size_t in_index = 0;
    for (auto it = reader.GetAccountByAddressRange(std::nullopt, std::nullopt); it.Valid();
         it.Next()) {
      ++in_index;
    }
    ASSERT_EQ(in_index, with_address);

    size_t records = 0;
    for (auto it = reader.ScanAccount(); it.Valid(); it.Next()) {
      const Account account = it.Value();
      const auto row = model.find(account.account_id());
      ASSERT_NE(row, model.end());
      ASSERT_EQ(account.age(), row->second.age);
      ASSERT_EQ(account.name(), row->second.name);
      ++records;
    }
    ASSERT_EQ(records, model.size());
  }
};

TEST_P(IndexModelTest, TransactionsAgreeWithAModel) {
  std::mt19937 rng(GetParam());
  Db db;
  Model committed;
  // Snapshots keep what they saw, for as long as they live.
  struct Kept {
    Snapshot snapshot;
    Model model;
  };
  std::vector<std::unique_ptr<Kept>> kept;

  const auto address_of = [&rng] { return "address " + std::to_string(rng() % 60); };
  const auto taken = [](const Model& model, const std::string& address, uint64_t by) {
    for (const auto& [id, row] : model) {
      if (id != by && row.address == address) return true;
    }
    return false;
  };

  for (int txn = 0; txn < 120; ++txn) {
    Model model = committed;
    auto writer = db.CreateWriter();
    // Reading in order partway through changes how the rest of the transaction keeps its changes.
    const int read_in_order_at = rng() % 2 == 0 ? static_cast<int>(rng() % 12) : -1;
    const int steps = 1 + static_cast<int>(rng() % 12);
    for (int step = 0; step < steps; ++step) {
      if (step == read_in_order_at) {
        CheckAgainst(writer, model, "in a transaction");
        if (::testing::Test::HasFatalFailure()) return;
      }
      const uint64_t id = 1 + rng() % 40;
      const bool exists = model.contains(id);
      switch (rng() % 5) {
        case 0:
        case 1: {  // Put.
          Row row{static_cast<int32_t>(rng() % 8), std::nullopt,
                  "name " + std::to_string(rng() % 1000)};
          if (rng() % 2 == 0) row.address = address_of();
          AccountBuilder builder;
          builder.set_account_id(id).set_age(row.age).set_name(row.name);
          if (row.address.has_value()) builder.set_address(*row.address);
          const absl::Status status = writer.PutAccount(builder.Build());
          if (row.address.has_value() && taken(model, *row.address, id)) {
            ASSERT_TRUE(absl::IsAlreadyExists(status)) << status;
          } else {
            ASSERT_TRUE(status.ok()) << status;
            model[id] = row;
          }
          break;
        }
        case 2: {  // A patch of what no index has.
          const std::string name = "patched " + std::to_string(rng() % 1000);
          const absl::Status status =
              writer.UpdateAccount(id, AccountPatchBuilder().set_name(name).Build());
          ASSERT_EQ(status.ok(), exists) << status;
          if (exists) model[id].name = name;
          break;
        }
        case 3: {  // A patch of what indices have.
          const int32_t age = static_cast<int32_t>(rng() % 8);
          const std::string address = address_of();
          const absl::Status status = writer.UpdateAccount(
              id, AccountPatchBuilder().set_age(age).set_address(address).Build());
          if (!exists) {
            ASSERT_TRUE(absl::IsNotFound(status)) << status;
          } else if (taken(model, address, id)) {
            ASSERT_TRUE(absl::IsAlreadyExists(status)) << status;
          } else {
            ASSERT_TRUE(status.ok()) << status;
            model[id].age = age;
            model[id].address = address;
          }
          break;
        }
        case 4: {  // Delete.
          const absl::Status status = writer.DeleteAccount(id);
          ASSERT_EQ(status.ok(), exists) << status;
          model.erase(id);
          break;
        }
      }
    }
    CheckAgainst(writer, model, "at the end of a transaction");
    if (::testing::Test::HasFatalFailure()) return;
    if (rng() % 4 == 0) {
      writer.Rollback();
    } else {
      ASSERT_TRUE(writer.Commit().ok());
      committed = std::move(model);
    }
    auto snapshot = db.CreateSnapshot();
    CheckAgainst(snapshot, committed, "after the transaction");
    if (::testing::Test::HasFatalFailure()) return;
    if (rng() % 8 == 0) {
      kept.push_back(std::make_unique<Kept>(Kept{db.CreateSnapshot(), committed}));
    }
    if (kept.size() > 3) kept.erase(kept.begin());
    for (const auto& each : kept) {
      CheckAgainst(each->snapshot, each->model, "in an old snapshot");
      if (::testing::Test::HasFatalFailure()) return;
    }
  }
}

INSTANTIATE_TEST_SUITE_P(Seeds, IndexModelTest, ::testing::Range(1u, 13u));

// An index that a patch cannot reach is left alone, and one that it can is not.
TEST(IndexPatchTest, OnlyPatchesOfIndexedFieldsMoveARecord) {
  Db db;
  auto writer = db.CreateWriter();
  ASSERT_TRUE(
      writer.PutAccount(AccountBuilder().set_account_id(1).set_age(30).set_address("a").Build())
          .ok());
  ASSERT_TRUE(
      writer.PutAccount(AccountBuilder().set_account_id(2).set_age(40).set_address("b").Build())
          .ok());
  ASSERT_TRUE(writer.Commit().ok());

  // Would collide if it were looked at, and is not looked at, as it stays what it is.
  ASSERT_TRUE(writer.UpdateAccount(2, AccountPatchBuilder().set_name("x").Build()).ok());
  ASSERT_TRUE(writer.Commit().ok());
  auto snapshot = db.CreateSnapshot();
  ASSERT_TRUE(snapshot.GetAccountByAgeEqual(40).Valid());
  EXPECT_EQ(snapshot.GetAccountByAgeEqual(40).Value().name(), "x");

  EXPECT_TRUE(absl::IsAlreadyExists(
      writer.UpdateAccount(2, AccountPatchBuilder().set_address("a").Build())));
  ASSERT_TRUE(writer.UpdateAccount(2, AccountPatchBuilder().set_age(50).Build()).ok());
  ASSERT_TRUE(writer.Commit().ok());
  auto later = db.CreateSnapshot();
  EXPECT_FALSE(later.GetAccountByAgeEqual(40).Valid());
  EXPECT_TRUE(later.GetAccountByAgeEqual(50).Valid());
  // The older one still finds it where it was.
  EXPECT_TRUE(snapshot.GetAccountByAgeEqual(40).Valid());
  EXPECT_EQ(snapshot.GetAccountByAgeEqual(40).Value().age(), 40);
}

// An index entry goes straight to its record, so this comes back for the record that is there now.
TEST(IndexPatchTest, ARecordDeletedAndWrittenAgainIsFoundAnew) {
  Db db;
  auto writer = db.CreateWriter();
  ASSERT_TRUE(
      writer.PutAccount(AccountBuilder().set_account_id(1).set_age(30).set_name("first").Build())
          .ok());
  ASSERT_TRUE(writer.Commit().ok());
  auto old = db.CreateSnapshot();
  ASSERT_TRUE(writer.DeleteAccount(1).ok());
  ASSERT_TRUE(writer.Commit().ok());
  ASSERT_TRUE(
      writer.PutAccount(AccountBuilder().set_account_id(1).set_age(30).set_name("second").Build())
          .ok());
  ASSERT_TRUE(writer.Commit().ok());
  auto now = db.CreateSnapshot();
  EXPECT_EQ(now.GetAccountByAgeEqual(30).Value().name(), "second");
  EXPECT_EQ(old.GetAccountByAgeEqual(30).Value().name(), "first");
  // And after everything old has gone, which takes commits with nothing pinning it.
  for (uint64_t id = 2; id < 6; ++id) {
    ASSERT_TRUE(writer.PutAccount(AccountBuilder().set_account_id(id).Build()).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  EXPECT_EQ(db.CreateSnapshot().GetAccountByAgeEqual(30).Value().name(), "second");
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
