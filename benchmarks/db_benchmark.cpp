// Database benchmarks through the generated API. Results: benchmarks/reference/db.md.
#include <benchmark/benchmark.h>

#include <filesystem>
#include <memory>
#include <string>

#include "account.fbs.h"
#include "database.h"

namespace {

using gendb::tests::Account;
using gendb::tests::AccountBuilder;
using gendb::tests::AccountPatchBuilder;
using gendb::tests::Db;
using gendb::tests::DbOptions;

constexpr uint64_t kRecords = 100'000;

std::vector<uint8_t> MakeAccount(uint64_t id) {
  AccountBuilder builder;
  builder.set_account_id(id)
      .set_name("Alice Example")
      .set_trader_id("TRADER-00042")
      .set_age(static_cast<int32_t>(id % 80))
      .set_balance(1024.5f)
      .set_is_active(true);
  return builder.Build();
}

void Fill(Db& db) {
  auto writer = db.CreateWriter();
  for (uint64_t id = 0; id < kRecords; ++id) (void)writer.PutAccount(id, MakeAccount(id));
  (void)writer.Commit();
}

Db& SharedDb() {
  static Db* db = [] {
    auto* created = new Db();
    Fill(*created);
    return created;
  }();
  return *db;
}

// Multiplicative hash: visits every record, in an order the caches cannot predict.
uint64_t Scatter(uint64_t i) {
  return (i * 0x9e3779b97f4a7c15ULL) % kRecords;
}

void SnapshotOpenClose(benchmark::State& state) {
  Db& db = SharedDb();
  for (auto _ : state) {
    auto snapshot = db.CreateSnapshot();
    benchmark::DoNotOptimize(&snapshot);
  }
}
BENCHMARK(SnapshotOpenClose)->ThreadRange(1, 8)->UseRealTime();

void SnapshotGet(benchmark::State& state) {
  Db& db = SharedDb();
  auto snapshot = db.CreateSnapshot();
  uint64_t i = state.thread_index() * 1000;
  for (auto _ : state) {
    Account account;
    (void)snapshot.GetAccount(Scatter(i++), account);
    benchmark::DoNotOptimize(account.age());
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(SnapshotGet)->ThreadRange(1, 8)->UseRealTime();

void IndexEqualScan(benchmark::State& state) {
  Db& db = SharedDb();
  auto snapshot = db.CreateSnapshot();
  int64_t rows = 0;
  int32_t age = 0;
  for (auto _ : state) {
    for (auto it = snapshot.GetAccountByAgeEqual(age++ % 80); it.Valid(); it.Next()) {
      benchmark::DoNotOptimize(it.Value().account_id());
      ++rows;
    }
  }
  state.SetItemsProcessed(rows);
}
BENCHMARK(IndexEqualScan);

void CollectionScan(benchmark::State& state) {
  Db& db = SharedDb();
  auto snapshot = db.CreateSnapshot();
  int64_t rows = 0;
  for (auto _ : state) {
    for (auto it = snapshot.ScanAccount(); it.Valid(); it.Next()) {
      benchmark::DoNotOptimize(it.Value().account_id());
      ++rows;
    }
  }
  state.SetItemsProcessed(rows);
}
BENCHMARK(CollectionScan);

void CommitOneScalarPatch(benchmark::State& state) {
  Db db;
  Fill(db);
  uint64_t i = 0;
  for (auto _ : state) {
    auto writer = db.CreateWriter();
    (void)writer.UpdateAccount(Scatter(i), AccountPatchBuilder().set_balance(float(i)).Build());
    (void)writer.Commit();
    ++i;
  }
}
BENCHMARK(CommitOneScalarPatch);

void CommitUnderSnapshot(benchmark::State& state) {
  Db db;
  Fill(db);
  auto snapshot = db.CreateSnapshot();  // Keeps every superseded version alive.
  uint64_t i = 0;
  for (auto _ : state) {
    auto writer = db.CreateWriter();
    (void)writer.UpdateAccount(Scatter(i), AccountPatchBuilder().set_balance(float(i)).Build());
    (void)writer.Commit();
    ++i;
  }
}
BENCHMARK(CommitUnderSnapshot);

// A persistent database; state.range(0) says whether each commit is fsynced.
void PersistentCommit(benchmark::State& state) {
  const std::string path = (std::filesystem::temp_directory_path() / "gendb_benchmark").string();
  std::filesystem::remove_all(path);
  {
    auto db = Db::Open({.path = path, .sync_commits = state.range(0) != 0});
    if (!db.ok()) {
      state.SkipWithError(db.status().ToString().c_str());
      return;
    }
    uint64_t i = 0;
    for (auto _ : state) {
      auto writer = (*db)->CreateWriter();
      (void)writer.PutAccount(i, MakeAccount(i));
      (void)writer.Commit();
      ++i;
    }
  }
  std::filesystem::remove_all(path);
}
BENCHMARK(PersistentCommit)->Arg(0)->Arg(1);

void Recover(benchmark::State& state) {
  const std::string path =
      (std::filesystem::temp_directory_path() / "gendb_benchmark_recover").string();
  std::filesystem::remove_all(path);
  {
    auto db = Db::Open({.path = path, .sync_commits = false});
    Fill(**db);
  }
  for (auto _ : state) {
    auto db = Db::Open({.path = path});
    benchmark::DoNotOptimize(db.ok());
  }
  state.SetItemsProcessed(state.iterations() * kRecords);
  std::filesystem::remove_all(path);
}
BENCHMARK(Recover)->Unit(benchmark::kMillisecond);

}  // namespace
