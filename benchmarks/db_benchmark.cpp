// Database benchmarks through the generated API. Results: benchmarks/reference/db.txt.
#include <benchmark/benchmark.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "account.fbs.h"
#include "database.h"
#include "gendb/replication.h"
#include "lib/loopback_transport.h"

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
  for (uint64_t id = 0; id < kRecords; ++id) (void)writer.PutAccount(MakeAccount(id));
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

// Delivery to a subscriber that does nothing; state.range(0) picks the delivery.
void CommitWithSubscriber(benchmark::State& state) {
  Db db;
  Fill(db);
  gendb::SubscribeOptions options;
  options.delivery = state.range(0) != 0 ? gendb::Delivery::kQueued : gendb::Delivery::kInline;
  auto subscription = db.Subscribe({.on_account =
                                        [](const gendb::tests::AccountChange& change) {
                                          benchmark::DoNotOptimize(change.balance_changed());
                                        }},
                                   options);
  uint64_t i = 0;
  for (auto _ : state) {
    auto writer = db.CreateWriter();
    (void)writer.UpdateAccount(Scatter(i), AccountPatchBuilder().set_balance(float(i)).Build());
    (void)writer.Commit();
    ++i;
  }
  subscription.WaitUntilDelivered();
}
BENCHMARK(CommitWithSubscriber)->ArgName("queued")->Arg(0)->Arg(1);

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
      (void)writer.PutAccount(MakeAccount(i));
      (void)writer.Commit();
      ++i;
    }
  }
  std::filesystem::remove_all(path);
}
BENCHMARK(PersistentCommit)->Arg(0)->Arg(1);

// One changed field of a record, non-durably, as an Update or a whole Put. Reports what each commit
// adds to the log. state.range(0): size of the record's string; state.range(1): 1 for an Update.
void LoggedCommit(benchmark::State& state) {
  const std::string path =
      (std::filesystem::temp_directory_path() / "gendb_benchmark_logged").string();
  std::filesystem::remove_all(path);
  uint64_t log_bytes = 0;
  {
    auto db = Db::Open({.path = path, .sync_commits = false, .checkpoint_wal_bytes = 0});
    const std::string name(static_cast<size_t>(state.range(0)), 'n');
    const auto make = [&name](float balance) {
      AccountBuilder builder;
      builder.set_account_id(1).set_name(name).set_balance(balance);
      return builder.Build();
    };
    {
      auto writer = (*db)->CreateWriter();
      (void)writer.PutAccount(make(0.f));
      (void)writer.Commit();
    }
    const std::vector<uint8_t> whole = make(1.f);
    const uint64_t before = (*db)->engine().Stats().wal_bytes;
    uint64_t i = 0;
    for (auto _ : state) {
      auto writer = (*db)->CreateWriter();
      if (state.range(1) != 0) {
        (void)writer.UpdateAccount(1, AccountPatchBuilder().set_balance(float(i++)).Build());
      } else {
        (void)writer.PutAccount(whole);
      }
      (void)writer.Commit();
    }
    log_bytes = (*db)->engine().Stats().wal_bytes - before;
  }
  state.counters["log_bytes_per_commit"] =
      static_cast<double>(log_bytes) / static_cast<double>(state.iterations());
  std::filesystem::remove_all(path);
}
BENCHMARK(LoggedCommit)
    ->ArgNames({"string", "update"})
    ->Args({16, 0})
    ->Args({16, 1})
    ->Args({1024, 0})
    ->Args({1024, 1});

// Replaying a log of single-field updates.
void RecoverUpdates(benchmark::State& state) {
  const std::string path =
      (std::filesystem::temp_directory_path() / "gendb_benchmark_recover_updates").string();
  std::filesystem::remove_all(path);
  constexpr uint64_t kUpdates = 100'000;
  {
    auto db = Db::Open({.path = path, .sync_commits = false, .checkpoint_wal_bytes = 0});
    Fill(**db);
    (void)(*db)->engine().Checkpoint();
    for (uint64_t i = 0; i < kUpdates; ++i) {
      auto writer = (*db)->CreateWriter();
      (void)writer.UpdateAccount(Scatter(i), AccountPatchBuilder().set_balance(float(i)).Build());
      (void)writer.Commit();
    }
    state.counters["log_MB"] = static_cast<double>((*db)->engine().Stats().wal_bytes) / (1 << 20);
  }
  for (auto _ : state) {
    auto db = Db::Open({.path = path});
    benchmark::DoNotOptimize(db.ok());
  }
  std::filesystem::remove_all(path);
}
BENCHMARK(RecoverUpdates)->Unit(benchmark::kMillisecond);

void CheckpointWrite(benchmark::State& state) {
  const std::string path =
      (std::filesystem::temp_directory_path() / "gendb_benchmark_checkpoint").string();
  std::filesystem::remove_all(path);
  {
    auto db = Db::Open({.path = path, .sync_commits = false, .checkpoint_wal_bytes = 0});
    Fill(**db);
    uint64_t i = 0;
    for (auto _ : state) {
      {
        auto writer = (*db)->CreateWriter();
        (void)writer.UpdateAccount(0, AccountPatchBuilder().set_balance(float(i++)).Build());
        (void)writer.Commit();
      }
      (void)(*db)->engine().Checkpoint();
    }
    state.counters["bytes"] = static_cast<double>((*db)->engine().Stats().checkpoint_bytes);
  }
  state.SetItemsProcessed(state.iterations() * kRecords);
  std::filesystem::remove_all(path);
}
BENCHMARK(CheckpointWrite)->Unit(benchmark::kMillisecond);

// state.range(0) says whether the state is in a checkpoint rather than only in the log.
void Recover(benchmark::State& state) {
  const std::string path =
      (std::filesystem::temp_directory_path() / "gendb_benchmark_recover").string();
  std::filesystem::remove_all(path);
  {
    auto db = Db::Open({.path = path, .sync_commits = false});
    Fill(**db);
    if (state.range(0) != 0) (void)(*db)->engine().Checkpoint();
  }
  for (auto _ : state) {
    auto db = Db::Open({.path = path});
    benchmark::DoNotOptimize(db.ok());
  }
  state.SetItemsProcessed(state.iterations() * kRecords);
  std::filesystem::remove_all(path);
}
BENCHMARK(Recover)->ArgName("from_checkpoint")->Arg(0)->Arg(1)->Unit(benchmark::kMillisecond);

// A new memory-only replica, from connecting to holding all 100,000 records with indices.
void ReplicaSeed(benchmark::State& state) {
  Db& primary = SharedDb();
  gendb::testing::LoopbackServer server(primary.engine());
  // Fill does one commit, so the primary's LSN does not say how much data it has.
  const uint64_t lsn = primary.engine().Stats().lsn;
  for (auto _ : state) {
    Db replica_db;
    gendb::Replica replica(replica_db.engine(), server.connector());
    if (!replica.WaitForLsn(lsn, std::chrono::seconds(60))) {
      state.SkipWithError("Timed out");
      return;
    }
  }
  state.SetItemsProcessed(state.iterations() * kRecords);
}
BENCHMARK(ReplicaSeed)->Unit(benchmark::kMillisecond)->UseRealTime();

// Commits on the primary until each is applied by a following replica, with no network.
void ReplicaFollow(benchmark::State& state) {
  Db primary;
  Fill(primary);
  gendb::testing::LoopbackServer server(primary.engine());
  Db replica_db;
  gendb::Replica replica(replica_db.engine(), server.connector());
  const auto patience = std::chrono::seconds(60);
  (void)replica.WaitForLsn(primary.engine().Stats().lsn, patience);
  uint64_t i = 0;
  for (auto _ : state) {
    {
      auto writer = primary.CreateWriter();
      (void)writer.UpdateAccount(Scatter(i), AccountPatchBuilder().set_balance(float(i)).Build());
      (void)writer.Commit();
    }
    ++i;
    if (i % 64 == 0) (void)replica.WaitForLsn(primary.engine().Stats().lsn, patience);
  }
  (void)replica.WaitForLsn(primary.engine().Stats().lsn, patience);
}
BENCHMARK(ReplicaFollow)->UseRealTime();

}  // namespace
