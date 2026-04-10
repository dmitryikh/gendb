#include <cassert>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

#include <benchmark/benchmark.h>

#include "account.fbs.h"
#include "message_benchmark_common.h"
#include "position.fbs.h"

namespace gbench = gendb::benchmarks;

namespace {

using gendb::tests::Account;
using gendb::tests::AccountBuilder;
using gendb::tests::Position;
using gendb::tests::PositionBuilder;

struct SampleStats {
  size_t serialized_bytes;
  size_t builder_bytes;
};

void SetCounters(benchmark::State& state, const SampleStats& stats) {
  state.counters["serialized_bytes"] = static_cast<double>(stats.serialized_bytes);
  state.counters["builder_bytes"] = static_cast<double>(stats.builder_bytes);
}

std::pair<AccountBuilder, SampleStats> MakeGendbAccountBuilder(
    const gbench::AccountDataset& data,
    bool sparse) {
  AccountBuilder builder;
  builder.set_account_id(data.account_id);
  if (!sparse) {
    builder.set_trader_id(data.trader_id);
    builder.set_name(data.name);
    builder.set_address(data.address);
    builder.set_age(data.age);
    builder.set_balance(data.balance);
    builder.set_config_name(data.config_name);
  }
  builder.set_is_active(data.is_active);
  auto bytes = builder.Build();
  return {std::move(builder),
          SampleStats{
              .serialized_bytes = bytes.size(),
              .builder_bytes = builder.OwnedBytesEstimate() + bytes.size(),
          }};
}

std::pair<PositionBuilder, SampleStats> MakeGendbPositionBuilder(const gbench::PositionDataset& data) {
  PositionBuilder builder;
  builder.set_position_id(data.position_id);
  builder.set_account_id(data.account_id);
  builder.set_volume(data.volume);
  builder.set_instrument(data.instrument);
  builder.set_open_price(data.open_price);
  builder.set_direction(static_cast<gendb::tests::Direction>(data.direction));
  auto bytes = builder.Build();
  return {std::move(builder),
          SampleStats{
              .serialized_bytes = bytes.size(),
              .builder_bytes = builder.OwnedBytesEstimate() + bytes.size(),
          }};
}

std::vector<uint8_t> BuildGendbAccount(const gbench::AccountDataset& data, bool sparse) {
  auto [builder, stats] = MakeGendbAccountBuilder(data, sparse);
  static_cast<void>(stats);
  return builder.Build();
}

std::vector<uint8_t> BuildGendbPosition(const gbench::PositionDataset& data) {
  auto [builder, stats] = MakeGendbPositionBuilder(data);
  static_cast<void>(stats);
  return builder.Build();
}

void ValidateGendbAccount(const gbench::AccountDataset& data, bool sparse) {
  const auto bytes = BuildGendbAccount(data, sparse);
  const Account account(bytes);
  assert(account.account_id() == data.account_id);
  assert(account.is_active() == data.is_active);
  if (!sparse) {
    assert(account.trader_id() == data.trader_id);
    assert(account.name() == data.name);
    assert(account.address() == data.address);
    assert(account.age() == data.age);
    assert(account.balance() == data.balance);
    assert(account.config_name() == data.config_name);
  }
}

void ValidateGendbPosition(const gbench::PositionDataset& data) {
  const auto bytes = BuildGendbPosition(data);
  const Position position(bytes);
  assert(position.position_id() == data.position_id);
  assert(position.account_id() == data.account_id);
  assert(position.volume() == data.volume);
  assert(position.instrument() == data.instrument);
  assert(position.open_price() == data.open_price);
  assert(static_cast<uint32_t>(position.direction()) == data.direction);
}

const bool kValidated = []() {
  ValidateGendbPosition(gbench::kPositionFull);
  ValidateGendbAccount(gbench::kAccountFull, false);
  ValidateGendbAccount(gbench::kAccountSparse, true);
  return true;
}();

void BM_Gendb_Position_Build(benchmark::State& state) {
  assert(kValidated);
  const auto sample = MakeGendbPositionBuilder(gbench::kPositionFull).second;
  SetCounters(state, sample);
  for (auto _ : state) {
    PositionBuilder builder;
    builder.set_position_id(gbench::kPositionFull.position_id);
    builder.set_account_id(gbench::kPositionFull.account_id);
    builder.set_volume(gbench::kPositionFull.volume);
    builder.set_instrument(gbench::kPositionFull.instrument);
    builder.set_open_price(gbench::kPositionFull.open_price);
    builder.set_direction(static_cast<gendb::tests::Direction>(gbench::kPositionFull.direction));
    auto bytes = builder.Build();
    benchmark::DoNotOptimize(bytes);
  }
}

void BM_Gendb_Position_SerializeOnly(benchmark::State& state) {
  assert(kValidated);
  const auto sample = MakeGendbPositionBuilder(gbench::kPositionFull).second;
  SetCounters(state, sample);
  for (auto _ : state) {
    state.PauseTiming();
    PositionBuilder builder;
    builder.set_position_id(gbench::kPositionFull.position_id);
    builder.set_account_id(gbench::kPositionFull.account_id);
    builder.set_volume(gbench::kPositionFull.volume);
    builder.set_instrument(gbench::kPositionFull.instrument);
    builder.set_open_price(gbench::kPositionFull.open_price);
    builder.set_direction(static_cast<gendb::tests::Direction>(gbench::kPositionFull.direction));
    state.ResumeTiming();
    auto bytes = builder.Build();
    benchmark::DoNotOptimize(bytes);
  }
}

void BM_Gendb_Position_View(benchmark::State& state) {
  assert(kValidated);
  const auto bytes = BuildGendbPosition(gbench::kPositionFull);
  state.counters["serialized_bytes"] = static_cast<double>(bytes.size());
  for (auto _ : state) {
    Position position{std::span<const uint8_t>(bytes)};
    benchmark::DoNotOptimize(&position);
  }
}

void BM_Gendb_Position_ReadHotFields(benchmark::State& state) {
  assert(kValidated);
  const auto bytes = BuildGendbPosition(gbench::kPositionFull);
  const Position position(bytes);
  state.counters["serialized_bytes"] = static_cast<double>(bytes.size());
  for (auto _ : state) {
    benchmark::DoNotOptimize(position.position_id());
    benchmark::DoNotOptimize(position.account_id());
    benchmark::DoNotOptimize(position.volume());
    benchmark::DoNotOptimize(position.instrument().data());
    benchmark::DoNotOptimize(position.instrument().size());
    benchmark::DoNotOptimize(position.open_price());
  }
}

void BM_Gendb_Position_ReadFixedHotFields(benchmark::State& state) {
  assert(kValidated);
  const auto bytes = BuildGendbPosition(gbench::kPositionFull);
  const Position position(bytes);
  state.counters["serialized_bytes"] = static_cast<double>(bytes.size());
  for (auto _ : state) {
    benchmark::DoNotOptimize(position.position_id());
    benchmark::DoNotOptimize(position.account_id());
    benchmark::DoNotOptimize(position.volume());
    benchmark::DoNotOptimize(position.open_price());
  }
}

void BM_Gendb_Position_ReadSparseHotFields(benchmark::State& state) {
  assert(kValidated);
  const auto bytes = BuildGendbPosition(gbench::kPositionFull);
  const Position position(bytes);
  state.counters["serialized_bytes"] = static_cast<double>(bytes.size());
  for (auto _ : state) {
    benchmark::DoNotOptimize(position.instrument().data());
    benchmark::DoNotOptimize(position.instrument().size());
  }
}

void BM_Gendb_Position_EndToEnd(benchmark::State& state) {
  assert(kValidated);
  const auto sample = MakeGendbPositionBuilder(gbench::kPositionFull).second;
  SetCounters(state, sample);
  for (auto _ : state) {
    PositionBuilder builder;
    builder.set_position_id(gbench::kPositionFull.position_id);
    builder.set_account_id(gbench::kPositionFull.account_id);
    builder.set_volume(gbench::kPositionFull.volume);
    builder.set_instrument(gbench::kPositionFull.instrument);
    builder.set_open_price(gbench::kPositionFull.open_price);
    builder.set_direction(static_cast<gendb::tests::Direction>(gbench::kPositionFull.direction));
    auto bytes = builder.Build();
    Position position(bytes);
    benchmark::DoNotOptimize(position.position_id());
    benchmark::DoNotOptimize(position.instrument().size());
    benchmark::DoNotOptimize(position.open_price());
  }
}

void BM_Gendb_AccountFull_Build(benchmark::State& state) {
  assert(kValidated);
  const auto sample = MakeGendbAccountBuilder(gbench::kAccountFull, false).second;
  SetCounters(state, sample);
  for (auto _ : state) {
    AccountBuilder builder;
    builder.set_account_id(gbench::kAccountFull.account_id);
    builder.set_trader_id(gbench::kAccountFull.trader_id);
    builder.set_name(gbench::kAccountFull.name);
    builder.set_address(gbench::kAccountFull.address);
    builder.set_age(gbench::kAccountFull.age);
    builder.set_is_active(gbench::kAccountFull.is_active);
    builder.set_balance(gbench::kAccountFull.balance);
    builder.set_config_name(gbench::kAccountFull.config_name);
    auto bytes = builder.Build();
    benchmark::DoNotOptimize(bytes);
  }
}

void BM_Gendb_AccountFull_SerializeOnly(benchmark::State& state) {
  assert(kValidated);
  const auto sample = MakeGendbAccountBuilder(gbench::kAccountFull, false).second;
  SetCounters(state, sample);
  for (auto _ : state) {
    state.PauseTiming();
    AccountBuilder builder;
    builder.set_account_id(gbench::kAccountFull.account_id);
    builder.set_trader_id(gbench::kAccountFull.trader_id);
    builder.set_name(gbench::kAccountFull.name);
    builder.set_address(gbench::kAccountFull.address);
    builder.set_age(gbench::kAccountFull.age);
    builder.set_is_active(gbench::kAccountFull.is_active);
    builder.set_balance(gbench::kAccountFull.balance);
    builder.set_config_name(gbench::kAccountFull.config_name);
    state.ResumeTiming();
    auto bytes = builder.Build();
    benchmark::DoNotOptimize(bytes);
  }
}

void BM_Gendb_AccountFull_View(benchmark::State& state) {
  assert(kValidated);
  const auto bytes = BuildGendbAccount(gbench::kAccountFull, false);
  state.counters["serialized_bytes"] = static_cast<double>(bytes.size());
  for (auto _ : state) {
    Account account{std::span<const uint8_t>(bytes)};
    benchmark::DoNotOptimize(&account);
  }
}

void BM_Gendb_AccountFull_ReadHotFields(benchmark::State& state) {
  assert(kValidated);
  const auto bytes = BuildGendbAccount(gbench::kAccountFull, false);
  const Account account(bytes);
  state.counters["serialized_bytes"] = static_cast<double>(bytes.size());
  for (auto _ : state) {
    benchmark::DoNotOptimize(account.account_id());
    benchmark::DoNotOptimize(account.name().data());
    benchmark::DoNotOptimize(account.name().size());
    benchmark::DoNotOptimize(account.age());
    benchmark::DoNotOptimize(account.is_active());
    benchmark::DoNotOptimize(account.balance());
  }
}

void BM_Gendb_AccountFull_ReadFixedHotFields(benchmark::State& state) {
  assert(kValidated);
  const auto bytes = BuildGendbAccount(gbench::kAccountFull, false);
  const Account account(bytes);
  state.counters["serialized_bytes"] = static_cast<double>(bytes.size());
  for (auto _ : state) {
    benchmark::DoNotOptimize(account.account_id());
    benchmark::DoNotOptimize(account.age());
    benchmark::DoNotOptimize(account.is_active());
    benchmark::DoNotOptimize(account.balance());
  }
}

void BM_Gendb_AccountFull_ReadSparseHotFields(benchmark::State& state) {
  assert(kValidated);
  const auto bytes = BuildGendbAccount(gbench::kAccountFull, false);
  const Account account(bytes);
  state.counters["serialized_bytes"] = static_cast<double>(bytes.size());
  for (auto _ : state) {
    benchmark::DoNotOptimize(account.name().data());
    benchmark::DoNotOptimize(account.name().size());
    benchmark::DoNotOptimize(account.address().data());
    benchmark::DoNotOptimize(account.address().size());
    benchmark::DoNotOptimize(account.config_name().data());
    benchmark::DoNotOptimize(account.config_name().size());
  }
}

void BM_Gendb_AccountFull_ReadSameFieldRepeatedly(benchmark::State& state) {
  assert(kValidated);
  const auto bytes = BuildGendbAccount(gbench::kAccountFull, false);
  const Account account(bytes);
  state.counters["serialized_bytes"] = static_cast<double>(bytes.size());
  for (auto _ : state) {
    benchmark::DoNotOptimize(account.name().data());
    benchmark::DoNotOptimize(account.name().size());
    benchmark::DoNotOptimize(account.name().data());
    benchmark::DoNotOptimize(account.name().size());
    benchmark::DoNotOptimize(account.name().data());
    benchmark::DoNotOptimize(account.name().size());
  }
}

void BM_Gendb_AccountFull_EndToEnd(benchmark::State& state) {
  assert(kValidated);
  const auto sample = MakeGendbAccountBuilder(gbench::kAccountFull, false).second;
  SetCounters(state, sample);
  for (auto _ : state) {
    AccountBuilder builder;
    builder.set_account_id(gbench::kAccountFull.account_id);
    builder.set_trader_id(gbench::kAccountFull.trader_id);
    builder.set_name(gbench::kAccountFull.name);
    builder.set_address(gbench::kAccountFull.address);
    builder.set_age(gbench::kAccountFull.age);
    builder.set_is_active(gbench::kAccountFull.is_active);
    builder.set_balance(gbench::kAccountFull.balance);
    builder.set_config_name(gbench::kAccountFull.config_name);
    auto bytes = builder.Build();
    Account account(bytes);
    benchmark::DoNotOptimize(account.account_id());
    benchmark::DoNotOptimize(account.name().size());
    benchmark::DoNotOptimize(account.balance());
  }
}

void BM_Gendb_AccountSparse_Build(benchmark::State& state) {
  assert(kValidated);
  const auto sample = MakeGendbAccountBuilder(gbench::kAccountSparse, true).second;
  SetCounters(state, sample);
  for (auto _ : state) {
    AccountBuilder builder;
    builder.set_account_id(gbench::kAccountSparse.account_id);
    builder.set_is_active(gbench::kAccountSparse.is_active);
    auto bytes = builder.Build();
    benchmark::DoNotOptimize(bytes);
  }
}

void BM_Gendb_AccountSparse_SerializeOnly(benchmark::State& state) {
  assert(kValidated);
  const auto sample = MakeGendbAccountBuilder(gbench::kAccountSparse, true).second;
  SetCounters(state, sample);
  for (auto _ : state) {
    state.PauseTiming();
    AccountBuilder builder;
    builder.set_account_id(gbench::kAccountSparse.account_id);
    builder.set_is_active(gbench::kAccountSparse.is_active);
    state.ResumeTiming();
    auto bytes = builder.Build();
    benchmark::DoNotOptimize(bytes);
  }
}

void BM_Gendb_AccountSparse_View(benchmark::State& state) {
  assert(kValidated);
  const auto bytes = BuildGendbAccount(gbench::kAccountSparse, true);
  state.counters["serialized_bytes"] = static_cast<double>(bytes.size());
  for (auto _ : state) {
    Account account{std::span<const uint8_t>(bytes)};
    benchmark::DoNotOptimize(&account);
  }
}

void BM_Gendb_AccountSparse_ReadHotFields(benchmark::State& state) {
  assert(kValidated);
  const auto bytes = BuildGendbAccount(gbench::kAccountSparse, true);
  const Account account(bytes);
  state.counters["serialized_bytes"] = static_cast<double>(bytes.size());
  for (auto _ : state) {
    benchmark::DoNotOptimize(account.account_id());
    benchmark::DoNotOptimize(account.is_active());
  }
}

void BM_Gendb_AccountSparse_EndToEnd(benchmark::State& state) {
  assert(kValidated);
  const auto sample = MakeGendbAccountBuilder(gbench::kAccountSparse, true).second;
  SetCounters(state, sample);
  for (auto _ : state) {
    AccountBuilder builder;
    builder.set_account_id(gbench::kAccountSparse.account_id);
    builder.set_is_active(gbench::kAccountSparse.is_active);
    auto bytes = builder.Build();
    Account account(bytes);
    benchmark::DoNotOptimize(account.account_id());
    benchmark::DoNotOptimize(account.is_active());
  }
}

BENCHMARK(BM_Gendb_Position_Build);
BENCHMARK(BM_Gendb_Position_SerializeOnly);
BENCHMARK(BM_Gendb_Position_View);
BENCHMARK(BM_Gendb_Position_ReadHotFields);
BENCHMARK(BM_Gendb_Position_ReadFixedHotFields);
BENCHMARK(BM_Gendb_Position_ReadSparseHotFields);
BENCHMARK(BM_Gendb_Position_EndToEnd);
BENCHMARK(BM_Gendb_AccountFull_Build);
BENCHMARK(BM_Gendb_AccountFull_SerializeOnly);
BENCHMARK(BM_Gendb_AccountFull_View);
BENCHMARK(BM_Gendb_AccountFull_ReadHotFields);
BENCHMARK(BM_Gendb_AccountFull_ReadFixedHotFields);
BENCHMARK(BM_Gendb_AccountFull_ReadSparseHotFields);
BENCHMARK(BM_Gendb_AccountFull_ReadSameFieldRepeatedly);
BENCHMARK(BM_Gendb_AccountFull_EndToEnd);
BENCHMARK(BM_Gendb_AccountSparse_Build);
BENCHMARK(BM_Gendb_AccountSparse_SerializeOnly);
BENCHMARK(BM_Gendb_AccountSparse_View);
BENCHMARK(BM_Gendb_AccountSparse_ReadHotFields);
BENCHMARK(BM_Gendb_AccountSparse_EndToEnd);

}  // namespace
