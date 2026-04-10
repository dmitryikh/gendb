#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <benchmark/benchmark.h>
#include <flatbuffers/flatbuffers.h>

#include "account_generated.h"
#include "message_benchmark_common.h"
#include "position_generated.h"

namespace fbench = gendb::benchmarks;

namespace {

struct SampleStats {
  size_t serialized_bytes;
  size_t builder_bytes;
};

void SetCounters(benchmark::State& state, const SampleStats& stats) {
  state.counters["serialized_bytes"] = static_cast<double>(stats.serialized_bytes);
  state.counters["builder_bytes"] = static_cast<double>(stats.builder_bytes);
}

std::pair<std::vector<uint8_t>, SampleStats> BuildFlatbuffersAccount(
    const fbench::AccountDataset& data,
    bool sparse) {
  flatbuffers::FlatBufferBuilder builder;
  const auto trader_id =
      sparse ? flatbuffers::Offset<flatbuffers::String>() : builder.CreateString(data.trader_id);
  const auto name =
      sparse ? flatbuffers::Offset<flatbuffers::String>() : builder.CreateString(data.name);
  const auto address =
      sparse ? flatbuffers::Offset<flatbuffers::String>() : builder.CreateString(data.address);
  const auto config_name = sparse ? flatbuffers::Offset<flatbuffers::String>()
                                  : builder.CreateString(data.config_name);
  const auto account =
      gendb::tests::CreateAccount(builder, data.account_id, trader_id, name, address,
                                  sparse ? 0 : data.age, data.is_active,
                                  sparse ? 0.0f : data.balance, config_name);
  builder.Finish(account);
  const auto* buffer_ptr = builder.GetBufferPointer();
  return {std::vector<uint8_t>(buffer_ptr, buffer_ptr + builder.GetSize()),
          SampleStats{
              .serialized_bytes = builder.GetSize(),
              .builder_bytes = builder.GetSize(),
          }};
}

std::pair<std::vector<uint8_t>, SampleStats> BuildFlatbuffersPosition(const fbench::PositionDataset& data) {
  flatbuffers::FlatBufferBuilder builder;
  const auto instrument = builder.CreateString(data.instrument);
  const auto position =
      gendb::tests::CreatePosition(builder, data.position_id, data.account_id, data.volume,
                                   instrument, data.open_price,
                                   static_cast<gendb::tests::Direction>(data.direction));
  builder.Finish(position);
  const auto* buffer_ptr = builder.GetBufferPointer();
  return {std::vector<uint8_t>(buffer_ptr, buffer_ptr + builder.GetSize()),
          SampleStats{
              .serialized_bytes = builder.GetSize(),
              .builder_bytes = builder.GetSize(),
          }};
}

void ValidateFlatbuffersAccount(const fbench::AccountDataset& data, bool sparse) {
  const auto [bytes, stats] = BuildFlatbuffersAccount(data, sparse);
  static_cast<void>(stats);
  const auto* account = flatbuffers::GetRoot<gendb::tests::Account>(bytes.data());
  assert(account->account_id() == data.account_id);
  assert(account->is_active() == data.is_active);
  if (!sparse) {
    assert(account->trader_id()->str() == data.trader_id);
    assert(account->name()->str() == data.name);
    assert(account->address()->str() == data.address);
    assert(account->age() == data.age);
    assert(account->balance() == data.balance);
    assert(account->config_name()->str() == data.config_name);
  }
}

void ValidateFlatbuffersPosition(const fbench::PositionDataset& data) {
  const auto [bytes, stats] = BuildFlatbuffersPosition(data);
  static_cast<void>(stats);
  const auto* position = flatbuffers::GetRoot<gendb::tests::Position>(bytes.data());
  assert(position->position_id() == data.position_id);
  assert(position->account_id() == data.account_id);
  assert(position->volume() == data.volume);
  assert(position->instrument()->str() == data.instrument);
  assert(position->open_price() == data.open_price);
  assert(static_cast<uint32_t>(position->direction()) == data.direction);
}

const bool kValidated = []() {
  ValidateFlatbuffersPosition(fbench::kPositionFull);
  ValidateFlatbuffersAccount(fbench::kAccountFull, false);
  ValidateFlatbuffersAccount(fbench::kAccountSparse, true);
  return true;
}();

void BM_FlatBuffers_Position_Build(benchmark::State& state) {
  assert(kValidated);
  const auto sample = BuildFlatbuffersPosition(fbench::kPositionFull).second;
  SetCounters(state, sample);
  for (auto _ : state) {
    flatbuffers::FlatBufferBuilder builder;
    const auto instrument = builder.CreateString(fbench::kPositionFull.instrument);
    const auto position = gendb::tests::CreatePosition(
        builder, fbench::kPositionFull.position_id, fbench::kPositionFull.account_id,
        fbench::kPositionFull.volume, instrument, fbench::kPositionFull.open_price,
        static_cast<gendb::tests::Direction>(fbench::kPositionFull.direction));
    builder.Finish(position);
    benchmark::DoNotOptimize(builder.GetBufferPointer());
    benchmark::DoNotOptimize(builder.GetSize());
  }
}

void BM_FlatBuffers_Position_SerializeOnly(benchmark::State& state) {
  assert(kValidated);
  const auto sample = BuildFlatbuffersPosition(fbench::kPositionFull).second;
  SetCounters(state, sample);
  for (auto _ : state) {
    state.PauseTiming();
    flatbuffers::FlatBufferBuilder builder;
    const auto instrument = builder.CreateString(fbench::kPositionFull.instrument);
    const auto position = gendb::tests::CreatePosition(
        builder, fbench::kPositionFull.position_id, fbench::kPositionFull.account_id,
        fbench::kPositionFull.volume, instrument, fbench::kPositionFull.open_price,
        static_cast<gendb::tests::Direction>(fbench::kPositionFull.direction));
    state.ResumeTiming();
    builder.Finish(position);
    benchmark::DoNotOptimize(builder.GetBufferPointer());
  }
}

void BM_FlatBuffers_Position_View(benchmark::State& state) {
  assert(kValidated);
  const auto [bytes, stats] = BuildFlatbuffersPosition(fbench::kPositionFull);
  state.counters["serialized_bytes"] = static_cast<double>(stats.serialized_bytes);
  for (auto _ : state) {
    const auto* position = flatbuffers::GetRoot<gendb::tests::Position>(bytes.data());
    benchmark::DoNotOptimize(position);
  }
}

void BM_FlatBuffers_Position_ReadHotFields(benchmark::State& state) {
  assert(kValidated);
  const auto [bytes, stats] = BuildFlatbuffersPosition(fbench::kPositionFull);
  const auto* position = flatbuffers::GetRoot<gendb::tests::Position>(bytes.data());
  state.counters["serialized_bytes"] = static_cast<double>(stats.serialized_bytes);
  for (auto _ : state) {
    benchmark::DoNotOptimize(position->position_id());
    benchmark::DoNotOptimize(position->account_id());
    benchmark::DoNotOptimize(position->volume());
    benchmark::DoNotOptimize(position->instrument()->c_str());
    benchmark::DoNotOptimize(position->instrument()->size());
    benchmark::DoNotOptimize(position->open_price());
  }
}

void BM_FlatBuffers_Position_ReadFixedHotFields(benchmark::State& state) {
  assert(kValidated);
  const auto [bytes, stats] = BuildFlatbuffersPosition(fbench::kPositionFull);
  const auto* position = flatbuffers::GetRoot<gendb::tests::Position>(bytes.data());
  state.counters["serialized_bytes"] = static_cast<double>(stats.serialized_bytes);
  for (auto _ : state) {
    benchmark::DoNotOptimize(position->position_id());
    benchmark::DoNotOptimize(position->account_id());
    benchmark::DoNotOptimize(position->volume());
    benchmark::DoNotOptimize(position->open_price());
  }
}

void BM_FlatBuffers_Position_ReadSparseHotFields(benchmark::State& state) {
  assert(kValidated);
  const auto [bytes, stats] = BuildFlatbuffersPosition(fbench::kPositionFull);
  const auto* position = flatbuffers::GetRoot<gendb::tests::Position>(bytes.data());
  state.counters["serialized_bytes"] = static_cast<double>(stats.serialized_bytes);
  for (auto _ : state) {
    benchmark::DoNotOptimize(position->instrument()->c_str());
    benchmark::DoNotOptimize(position->instrument()->size());
  }
}

void BM_FlatBuffers_Position_EndToEnd(benchmark::State& state) {
  assert(kValidated);
  const auto sample = BuildFlatbuffersPosition(fbench::kPositionFull).second;
  SetCounters(state, sample);
  for (auto _ : state) {
    flatbuffers::FlatBufferBuilder builder;
    const auto instrument = builder.CreateString(fbench::kPositionFull.instrument);
    const auto position_offset = gendb::tests::CreatePosition(
        builder, fbench::kPositionFull.position_id, fbench::kPositionFull.account_id,
        fbench::kPositionFull.volume, instrument, fbench::kPositionFull.open_price,
        static_cast<gendb::tests::Direction>(fbench::kPositionFull.direction));
    builder.Finish(position_offset);
    const auto* position = flatbuffers::GetRoot<gendb::tests::Position>(builder.GetBufferPointer());
    benchmark::DoNotOptimize(position->position_id());
    benchmark::DoNotOptimize(position->instrument()->size());
    benchmark::DoNotOptimize(position->open_price());
  }
}

void BM_FlatBuffers_AccountFull_Build(benchmark::State& state) {
  assert(kValidated);
  const auto sample = BuildFlatbuffersAccount(fbench::kAccountFull, false).second;
  SetCounters(state, sample);
  for (auto _ : state) {
    flatbuffers::FlatBufferBuilder builder;
    const auto trader_id = builder.CreateString(fbench::kAccountFull.trader_id);
    const auto name = builder.CreateString(fbench::kAccountFull.name);
    const auto address = builder.CreateString(fbench::kAccountFull.address);
    const auto config_name = builder.CreateString(fbench::kAccountFull.config_name);
    const auto account = gendb::tests::CreateAccount(
        builder, fbench::kAccountFull.account_id, trader_id, name, address,
        fbench::kAccountFull.age, fbench::kAccountFull.is_active, fbench::kAccountFull.balance,
        config_name);
    builder.Finish(account);
    benchmark::DoNotOptimize(builder.GetBufferPointer());
    benchmark::DoNotOptimize(builder.GetSize());
  }
}

void BM_FlatBuffers_AccountFull_SerializeOnly(benchmark::State& state) {
  assert(kValidated);
  const auto sample = BuildFlatbuffersAccount(fbench::kAccountFull, false).second;
  SetCounters(state, sample);
  for (auto _ : state) {
    state.PauseTiming();
    flatbuffers::FlatBufferBuilder builder;
    const auto trader_id = builder.CreateString(fbench::kAccountFull.trader_id);
    const auto name = builder.CreateString(fbench::kAccountFull.name);
    const auto address = builder.CreateString(fbench::kAccountFull.address);
    const auto config_name = builder.CreateString(fbench::kAccountFull.config_name);
    const auto account = gendb::tests::CreateAccount(
        builder, fbench::kAccountFull.account_id, trader_id, name, address,
        fbench::kAccountFull.age, fbench::kAccountFull.is_active, fbench::kAccountFull.balance,
        config_name);
    state.ResumeTiming();
    builder.Finish(account);
    benchmark::DoNotOptimize(builder.GetBufferPointer());
  }
}

void BM_FlatBuffers_AccountFull_View(benchmark::State& state) {
  assert(kValidated);
  const auto [bytes, stats] = BuildFlatbuffersAccount(fbench::kAccountFull, false);
  state.counters["serialized_bytes"] = static_cast<double>(stats.serialized_bytes);
  for (auto _ : state) {
    const auto* account = flatbuffers::GetRoot<gendb::tests::Account>(bytes.data());
    benchmark::DoNotOptimize(account);
  }
}

void BM_FlatBuffers_AccountFull_ReadHotFields(benchmark::State& state) {
  assert(kValidated);
  const auto [bytes, stats] = BuildFlatbuffersAccount(fbench::kAccountFull, false);
  const auto* account = flatbuffers::GetRoot<gendb::tests::Account>(bytes.data());
  state.counters["serialized_bytes"] = static_cast<double>(stats.serialized_bytes);
  for (auto _ : state) {
    benchmark::DoNotOptimize(account->account_id());
    benchmark::DoNotOptimize(account->name()->c_str());
    benchmark::DoNotOptimize(account->name()->size());
    benchmark::DoNotOptimize(account->age());
    benchmark::DoNotOptimize(account->is_active());
    benchmark::DoNotOptimize(account->balance());
  }
}

void BM_FlatBuffers_AccountFull_ReadFixedHotFields(benchmark::State& state) {
  assert(kValidated);
  const auto [bytes, stats] = BuildFlatbuffersAccount(fbench::kAccountFull, false);
  const auto* account = flatbuffers::GetRoot<gendb::tests::Account>(bytes.data());
  state.counters["serialized_bytes"] = static_cast<double>(stats.serialized_bytes);
  for (auto _ : state) {
    benchmark::DoNotOptimize(account->account_id());
    benchmark::DoNotOptimize(account->age());
    benchmark::DoNotOptimize(account->is_active());
    benchmark::DoNotOptimize(account->balance());
  }
}

void BM_FlatBuffers_AccountFull_ReadSparseHotFields(benchmark::State& state) {
  assert(kValidated);
  const auto [bytes, stats] = BuildFlatbuffersAccount(fbench::kAccountFull, false);
  const auto* account = flatbuffers::GetRoot<gendb::tests::Account>(bytes.data());
  state.counters["serialized_bytes"] = static_cast<double>(stats.serialized_bytes);
  for (auto _ : state) {
    benchmark::DoNotOptimize(account->name()->c_str());
    benchmark::DoNotOptimize(account->name()->size());
    benchmark::DoNotOptimize(account->address()->c_str());
    benchmark::DoNotOptimize(account->address()->size());
    benchmark::DoNotOptimize(account->config_name()->c_str());
    benchmark::DoNotOptimize(account->config_name()->size());
  }
}

void BM_FlatBuffers_AccountFull_ReadSameFieldRepeatedly(benchmark::State& state) {
  assert(kValidated);
  const auto [bytes, stats] = BuildFlatbuffersAccount(fbench::kAccountFull, false);
  const auto* account = flatbuffers::GetRoot<gendb::tests::Account>(bytes.data());
  state.counters["serialized_bytes"] = static_cast<double>(stats.serialized_bytes);
  for (auto _ : state) {
    benchmark::DoNotOptimize(account->name()->c_str());
    benchmark::DoNotOptimize(account->name()->size());
    benchmark::DoNotOptimize(account->name()->c_str());
    benchmark::DoNotOptimize(account->name()->size());
    benchmark::DoNotOptimize(account->name()->c_str());
    benchmark::DoNotOptimize(account->name()->size());
  }
}

void BM_FlatBuffers_AccountFull_EndToEnd(benchmark::State& state) {
  assert(kValidated);
  const auto sample = BuildFlatbuffersAccount(fbench::kAccountFull, false).second;
  SetCounters(state, sample);
  for (auto _ : state) {
    flatbuffers::FlatBufferBuilder builder;
    const auto trader_id = builder.CreateString(fbench::kAccountFull.trader_id);
    const auto name = builder.CreateString(fbench::kAccountFull.name);
    const auto address = builder.CreateString(fbench::kAccountFull.address);
    const auto config_name = builder.CreateString(fbench::kAccountFull.config_name);
    const auto account_offset = gendb::tests::CreateAccount(
        builder, fbench::kAccountFull.account_id, trader_id, name, address,
        fbench::kAccountFull.age, fbench::kAccountFull.is_active, fbench::kAccountFull.balance,
        config_name);
    builder.Finish(account_offset);
    const auto* account = flatbuffers::GetRoot<gendb::tests::Account>(builder.GetBufferPointer());
    benchmark::DoNotOptimize(account->account_id());
    benchmark::DoNotOptimize(account->name()->size());
    benchmark::DoNotOptimize(account->balance());
  }
}

void BM_FlatBuffers_AccountSparse_Build(benchmark::State& state) {
  assert(kValidated);
  const auto sample = BuildFlatbuffersAccount(fbench::kAccountSparse, true).second;
  SetCounters(state, sample);
  for (auto _ : state) {
    flatbuffers::FlatBufferBuilder builder;
    const auto account = gendb::tests::CreateAccount(builder, fbench::kAccountSparse.account_id, 0,
                                                      0, 0, 0,
                                                      fbench::kAccountSparse.is_active, 0.0f, 0);
    builder.Finish(account);
    benchmark::DoNotOptimize(builder.GetBufferPointer());
    benchmark::DoNotOptimize(builder.GetSize());
  }
}

void BM_FlatBuffers_AccountSparse_SerializeOnly(benchmark::State& state) {
  assert(kValidated);
  const auto sample = BuildFlatbuffersAccount(fbench::kAccountSparse, true).second;
  SetCounters(state, sample);
  for (auto _ : state) {
    state.PauseTiming();
    flatbuffers::FlatBufferBuilder builder;
    const auto account = gendb::tests::CreateAccount(builder, fbench::kAccountSparse.account_id, 0,
                                                      0, 0, 0,
                                                      fbench::kAccountSparse.is_active, 0.0f, 0);
    state.ResumeTiming();
    builder.Finish(account);
    benchmark::DoNotOptimize(builder.GetBufferPointer());
  }
}

void BM_FlatBuffers_AccountSparse_View(benchmark::State& state) {
  assert(kValidated);
  const auto [bytes, stats] = BuildFlatbuffersAccount(fbench::kAccountSparse, true);
  state.counters["serialized_bytes"] = static_cast<double>(stats.serialized_bytes);
  for (auto _ : state) {
    const auto* account = flatbuffers::GetRoot<gendb::tests::Account>(bytes.data());
    benchmark::DoNotOptimize(account);
  }
}

void BM_FlatBuffers_AccountSparse_ReadHotFields(benchmark::State& state) {
  assert(kValidated);
  const auto [bytes, stats] = BuildFlatbuffersAccount(fbench::kAccountSparse, true);
  const auto* account = flatbuffers::GetRoot<gendb::tests::Account>(bytes.data());
  state.counters["serialized_bytes"] = static_cast<double>(stats.serialized_bytes);
  for (auto _ : state) {
    benchmark::DoNotOptimize(account->account_id());
    benchmark::DoNotOptimize(account->is_active());
  }
}

void BM_FlatBuffers_AccountSparse_EndToEnd(benchmark::State& state) {
  assert(kValidated);
  const auto sample = BuildFlatbuffersAccount(fbench::kAccountSparse, true).second;
  SetCounters(state, sample);
  for (auto _ : state) {
    flatbuffers::FlatBufferBuilder builder;
    const auto account_offset = gendb::tests::CreateAccount(
        builder, fbench::kAccountSparse.account_id, 0, 0, 0, 0,
        fbench::kAccountSparse.is_active, 0.0f, 0);
    builder.Finish(account_offset);
    const auto* account = flatbuffers::GetRoot<gendb::tests::Account>(builder.GetBufferPointer());
    benchmark::DoNotOptimize(account->account_id());
    benchmark::DoNotOptimize(account->is_active());
  }
}

BENCHMARK(BM_FlatBuffers_Position_Build);
BENCHMARK(BM_FlatBuffers_Position_SerializeOnly);
BENCHMARK(BM_FlatBuffers_Position_View);
BENCHMARK(BM_FlatBuffers_Position_ReadHotFields);
BENCHMARK(BM_FlatBuffers_Position_ReadFixedHotFields);
BENCHMARK(BM_FlatBuffers_Position_ReadSparseHotFields);
BENCHMARK(BM_FlatBuffers_Position_EndToEnd);
BENCHMARK(BM_FlatBuffers_AccountFull_Build);
BENCHMARK(BM_FlatBuffers_AccountFull_SerializeOnly);
BENCHMARK(BM_FlatBuffers_AccountFull_View);
BENCHMARK(BM_FlatBuffers_AccountFull_ReadHotFields);
BENCHMARK(BM_FlatBuffers_AccountFull_ReadFixedHotFields);
BENCHMARK(BM_FlatBuffers_AccountFull_ReadSparseHotFields);
BENCHMARK(BM_FlatBuffers_AccountFull_ReadSameFieldRepeatedly);
BENCHMARK(BM_FlatBuffers_AccountFull_EndToEnd);
BENCHMARK(BM_FlatBuffers_AccountSparse_Build);
BENCHMARK(BM_FlatBuffers_AccountSparse_SerializeOnly);
BENCHMARK(BM_FlatBuffers_AccountSparse_View);
BENCHMARK(BM_FlatBuffers_AccountSparse_ReadHotFields);
BENCHMARK(BM_FlatBuffers_AccountSparse_EndToEnd);

}  // namespace
