// Message format benchmarks, with standard FlatBuffers tables as the baseline.
// Methodology: benchmarks/README.md.
#include <benchmark/benchmark.h>

#include <concepts>
#include <stdexcept>
#include <string>
#include <string_view>

#include "account.fbs.h"
#include "gendb/message.h"
#include "message_benchmark_common.h"
#include "message_benchmark_generated.h"
#include "position.fbs.h"

namespace {
namespace data = gendb::benchmarks;
namespace fb = gendb::benchmarks::flat;

enum class Case { Position, AccountFull, AccountSparse, WideFull, WideSparse };
constexpr bool IsWide(Case c) {
  return c == Case::WideFull || c == Case::WideSparse;
}
constexpr bool IsAccount(Case c) {
  return c == Case::AccountFull || c == Case::AccountSparse;
}
constexpr bool WidePresent(Case c, uint32_t id) {
  return c == Case::WideFull || id == 1 || id == 2 || id == 8 || id == 16 || id == 24 || id == 32 ||
         id == 48 || id == 64;
}
constexpr const char* CaseName(Case c) {
  switch (c) {
    case Case::Position:
      return "Position";
    case Case::AccountFull:
      return "AccountFull";
    case Case::AccountSparse:
      return "AccountSparse";
    case Case::WideFull:
      return "WideFull64";
    case Case::WideSparse:
      return "WideSparse8of64";
  }
  return "";
}

template <Case C, typename Builder>
void Fill(Builder& b) {
  if constexpr (C == Case::Position) {
    const auto& d = data::kPositionFull;
    b.template AddField<int32_t>(1, d.position_id);
    b.template AddField<int32_t>(2, d.account_id);
    b.template AddField<int32_t>(3, d.volume);
    b.AddStringField(4, d.instrument);
    b.template AddField<float>(5, d.open_price);
    b.template AddField<uint32_t>(6, d.direction);
  } else if constexpr (IsAccount(C)) {
    const auto& d = data::kAccountFull;
    b.template AddField<uint64_t>(1, d.account_id);
    if constexpr (C == Case::AccountFull) {
      b.AddStringField(2, d.trader_id);
      b.AddStringField(3, d.name);
      b.AddStringField(4, d.address);
      b.template AddField<int32_t>(5, d.age);
      b.template AddField<float>(7, d.balance);
      b.AddStringField(8, d.config_name);
    }
    b.template AddField<bool>(6, d.is_active);
  } else {
    for (uint32_t id = 1; id <= 64; ++id) {
      if (WidePresent(C, id)) b.template AddField<uint64_t>(id, 1000 + id);
    }
  }
}

template <Case C>
struct Message {
  static constexpr const char* Name = "Message";
  using Buffer = gendb::Bytes;
  using Builder = gendb::MessageBuilder;
  using View = gendb::MessageView;
  static Builder MakeBuilder() {
    Builder b;
    Fill<C>(b);
    return b;
  }
  static Buffer Serialize(Builder& b) { return b.Build(); }
  static View Open(const Buffer& bytes) {
    // Init in place: no std::optional and no copy of the view.
    View view(View::Uninitialized{});
    if (!view.Init(bytes)) throw std::runtime_error("invalid message");
    return view;
  }
  template <uint32_t Id, typename T>
  static T Get(const View& v) {
    return v.template Get<Id, T>();
  }
  static void Read(const View& v) {
    if constexpr (C == Case::Position) {
      benchmark::DoNotOptimize((Get<1, int32_t>(v)));
      benchmark::DoNotOptimize((Get<2, int32_t>(v)));
      benchmark::DoNotOptimize((Get<3, int32_t>(v)));
      benchmark::DoNotOptimize(v.template GetString<4>());
      benchmark::DoNotOptimize((Get<5, float>(v)));
      benchmark::DoNotOptimize((Get<6, uint32_t>(v)));
    } else if constexpr (IsAccount(C)) {
      benchmark::DoNotOptimize((Get<1, uint64_t>(v)));
      benchmark::DoNotOptimize((Get<6, bool>(v)));
      if constexpr (C == Case::AccountFull) {
        benchmark::DoNotOptimize(v.template GetString<2>());
        benchmark::DoNotOptimize(v.template GetString<3>());
        benchmark::DoNotOptimize(v.template GetString<4>());
        benchmark::DoNotOptimize((Get<5, int32_t>(v)));
        benchmark::DoNotOptimize((Get<7, float>(v)));
        benchmark::DoNotOptimize(v.template GetString<8>());
      }
    } else {
      benchmark::DoNotOptimize((Get<1, uint64_t>(v)));
      benchmark::DoNotOptimize((Get<2, uint64_t>(v)));
      benchmark::DoNotOptimize((Get<32, uint64_t>(v)));
      benchmark::DoNotOptimize((Get<64, uint64_t>(v)));
    }
  }
  static void Check(const View& v) {
    if (!v.Validate()) throw std::runtime_error("invalid fixture");
    if constexpr (C == Case::Position) {
      if (Get<1, int32_t>(v) != 1001 || v.template GetString<4>() != "EURUSD" ||
          Get<3, int32_t>(v) != 250 || Get<5, float>(v) != 1.2345f)
        throw std::runtime_error("invalid Position");
    } else if constexpr (IsAccount(C)) {
      if (Get<1, uint64_t>(v) != 42 || !Get<6, bool>(v) ||
          (C == Case::AccountFull && v.template GetString<3>() != "Alice Example") ||
          (C == Case::AccountSparse && v.FieldCount() != 2))
        throw std::runtime_error("invalid Account");
    } else if (Get<32, uint64_t>(v) != 1032 || Get<64, uint64_t>(v) != 1064 ||
               v.FieldCount() != (C == Case::WideFull ? 64u : 8u)) {
      throw std::runtime_error("invalid Wide");
    }
  }
};

#define WIDE_FIELDS(X)                                                                          \
  X(1)                                                                                          \
  X(2)                                                                                          \
  X(3)                                                                                          \
  X(4)                                                                                          \
  X(5)                                                                                          \
  X(6) X(7) X(8) X(9) X(10) X(11) X(12) X(13) X(14) X(15) X(16) X(17) X(18) X(19) X(20) X(21)   \
      X(22) X(23) X(24) X(25) X(26) X(27) X(28) X(29) X(30) X(31) X(32) X(33) X(34) X(35) X(36) \
          X(37) X(38) X(39) X(40) X(41) X(42) X(43) X(44) X(45) X(46) X(47) X(48) X(49) X(50)   \
              X(51) X(52) X(53) X(54) X(55) X(56) X(57) X(58) X(59) X(60) X(61) X(62) X(63) X(64)

template <Case C>
struct FlatBuffers {
  static constexpr const char* Name = "FlatBuffers";
  using Buffer = flatbuffers::DetachedBuffer;
  using Table = std::conditional_t<C == Case::Position, fb::Position,
                                   std::conditional_t<IsAccount(C), fb::Account, fb::Wide>>;
  struct WideSource {};
  using Builder =
      std::conditional_t<C == Case::Position, data::PositionDataset,
                         std::conditional_t<IsAccount(C), data::AccountDataset, WideSource>>;
  using View = const Table*;

  static Builder MakeBuilder() {
    if constexpr (C == Case::Position)
      return data::kPositionFull;
    else if constexpr (IsAccount(C))
      return C == Case::AccountSparse ? data::kAccountSparse : data::kAccountFull;
    else
      return {};
  }
  static Buffer Serialize(Builder& source) {
    flatbuffers::FlatBufferBuilder b;
    flatbuffers::Offset<Table> root;
    if constexpr (C == Case::Position) {
      auto instrument = b.CreateString(source.instrument.data(), source.instrument.size());
      fb::PositionBuilder position(b);
      position.add_position_id(source.position_id);
      position.add_account_id(source.account_id);
      position.add_volume(source.volume);
      position.add_instrument(instrument);
      position.add_open_price(source.open_price);
      position.add_direction(source.direction);
      root = position.Finish();
    } else if constexpr (IsAccount(C)) {
      flatbuffers::Offset<flatbuffers::String> trader, name, address, config;
      if constexpr (C == Case::AccountFull) {
        trader = b.CreateString(source.trader_id.data(), source.trader_id.size());
        name = b.CreateString(source.name.data(), source.name.size());
        address = b.CreateString(source.address.data(), source.address.size());
        config = b.CreateString(source.config_name.data(), source.config_name.size());
      }
      fb::AccountBuilder account(b);
      account.add_account_id(source.account_id);
      account.add_is_active(source.is_active);
      if constexpr (C == Case::AccountFull) {
        account.add_trader_id(trader);
        account.add_name(name);
        account.add_address(address);
        account.add_age(source.age);
        account.add_balance(source.balance);
        account.add_config_name(config);
      }
      root = account.Finish();
    } else {
      fb::WideBuilder wide(b);
#define ADD_FB_WIDE(N) \
  if constexpr (WidePresent(C, N)) wide.add_f##N(1000 + N);
      WIDE_FIELDS(ADD_FB_WIDE)
#undef ADD_FB_WIDE
      root = wide.Finish();
    }
    b.Finish(root);
    return b.Release();
  }
  static View Open(const Buffer& bytes) { return flatbuffers::GetRoot<Table>(bytes.data()); }
  static void CheckBuffer(const Buffer& bytes) {
    flatbuffers::Verifier verifier(bytes.data(), bytes.size());
    if (!verifier.VerifyBuffer<Table>(nullptr)) throw std::runtime_error("invalid FlatBuffer");
  }
  static std::string_view String(const flatbuffers::String* s) {
    return s ? std::string_view(s->c_str(), s->size()) : std::string_view{};
  }
  static void Read(View v) {
    if constexpr (C == Case::Position) {
      benchmark::DoNotOptimize(v->position_id().value_or(0));
      benchmark::DoNotOptimize(v->account_id().value_or(0));
      benchmark::DoNotOptimize(v->volume().value_or(0));
      benchmark::DoNotOptimize(String(v->instrument()));
      benchmark::DoNotOptimize(v->open_price().value_or(0));
      benchmark::DoNotOptimize(v->direction().value_or(0));
    } else if constexpr (IsAccount(C)) {
      benchmark::DoNotOptimize(v->account_id().value_or(0));
      benchmark::DoNotOptimize(v->is_active().value_or(false));
      if constexpr (C == Case::AccountFull) {
        benchmark::DoNotOptimize(String(v->trader_id()));
        benchmark::DoNotOptimize(String(v->name()));
        benchmark::DoNotOptimize(String(v->address()));
        benchmark::DoNotOptimize(v->age().value_or(0));
        benchmark::DoNotOptimize(v->balance().value_or(0));
        benchmark::DoNotOptimize(String(v->config_name()));
      }
    } else {
      benchmark::DoNotOptimize(v->f1().value_or(0));
      benchmark::DoNotOptimize(v->f2().value_or(0));
      benchmark::DoNotOptimize(v->f32().value_or(0));
      benchmark::DoNotOptimize(v->f64().value_or(0));
    }
  }
  static void Check(View v) {
    if constexpr (C == Case::Position) {
      if (v->position_id().value_or(0) != 1001 || String(v->instrument()) != "EURUSD" ||
          v->volume().value_or(0) != 250)
        throw std::runtime_error("invalid FB Position");
    } else if constexpr (IsAccount(C)) {
      if (v->account_id().value_or(0) != 42 || !v->is_active().value_or(false) ||
          (C == Case::AccountFull && String(v->name()) != "Alice Example") ||
          (C == Case::AccountSparse &&
           (v->age().has_value() || v->balance().has_value() || v->name())))
        throw std::runtime_error("invalid FB Account");
    } else if (v->f32().value_or(0) != 1032 || v->f64().value_or(0) != 1064 ||
               (C == Case::WideSparse && v->f3().has_value())) {
      throw std::runtime_error("invalid FB Wide");
    }
  }
  static bool Patch(Buffer& bytes) {
    auto* v = flatbuffers::GetMutableRoot<Table>(bytes.data());
    if constexpr (C == Case::Position)
      return v->mutate_volume(251);
    else if constexpr (IsAccount(C))
      return v->mutate_is_active(false);
    else
      return v->mutate_f32(9999);
  }
  static Buffer Rebuild(const Buffer& bytes, std::string_view value) {
    std::unique_ptr<typename Table::NativeTableType> native(Open(bytes)->UnPack());
    if constexpr (C == Case::Position)
      native->instrument.assign(value);
    else if constexpr (C == Case::AccountFull)
      native->name.assign(value);
    else if constexpr (C == Case::AccountSparse)
      native->trader_id.assign(value);
    flatbuffers::FlatBufferBuilder b;
    auto root = Table::Pack(b, native.get());
    b.Finish(root);
    return b.Release();
  }
  static void CheckMutations() {
    auto source = MakeBuilder();
    auto bytes = Serialize(source);
    if (!Patch(bytes)) throw std::runtime_error("FB scalar mutation rejected");
    CheckBuffer(bytes);
    auto v = Open(bytes);
    if constexpr (C == Case::Position) {
      if (v->volume().value_or(0) != 251) throw std::runtime_error("FB scalar mutation lost");
    } else if constexpr (IsAccount(C)) {
      if (!v->is_active().has_value() || v->is_active().value_or(true))
        throw std::runtime_error("FB explicit false mutation lost");
    } else if (v->f32().value_or(0) != 9999)
      throw std::runtime_error("FB scalar mutation lost");
    if constexpr (!IsWide(C)) {
      constexpr std::string_view replacement = "replacement with a different length";
      auto rebuilt = Rebuild(bytes, replacement);
      CheckBuffer(rebuilt);
      auto updated = Open(rebuilt);
      if constexpr (C == Case::Position) {
        if (String(updated->instrument()) != replacement || updated->volume().value_or(0) != 251)
          throw std::runtime_error("FB rebuild lost field");
      } else {
        const auto* changed = C == Case::AccountFull ? updated->name() : updated->trader_id();
        if (String(changed) != replacement || !updated->is_active().has_value() ||
            updated->is_active().value_or(true) ||
            (C == Case::AccountSparse &&
             (updated->age().has_value() || updated->balance().has_value())))
          throw std::runtime_error("FB rebuild changed field presence");
      }
    }
  }
};

enum class Operation { Build, Serialize, Open, ReadHot, ReadCold, EndToEnd };
constexpr const char* OpName(Operation op) {
  switch (op) {
    case Operation::Build:
      return "Build";
    case Operation::Serialize:
      return "SerializeOnly";
    case Operation::Open:
      return "Open";
    case Operation::ReadHot:
      return "ReadHot";
    case Operation::ReadCold:
      return "ReadCold";
    case Operation::EndToEnd:
      return "EndToEnd";
  }
  return "";
}

template <typename Adapter, Operation Op>
void Run(benchmark::State& state) {
  auto builder = Adapter::MakeBuilder();
  const auto bytes = Adapter::Serialize(builder);
  auto view = Adapter::Open(bytes);
  if constexpr (requires { Adapter::CheckBuffer(bytes); }) Adapter::CheckBuffer(bytes);
  if constexpr (requires { Adapter::CheckMutations(); }) Adapter::CheckMutations();
  Adapter::Check(view);  // Runs in Release too, before timing.
  state.counters["serialized_bytes"] = static_cast<double>(bytes.size());
  state.counters["view_bytes"] = sizeof(typename Adapter::View);
  for (auto _ : state) {
    benchmark::ClobberMemory();
    if constexpr (Op == Operation::Build || Op == Operation::EndToEnd) {
      auto fresh = Adapter::MakeBuilder();
      auto output = Adapter::Serialize(fresh);
      if constexpr (Op == Operation::EndToEnd) {
        auto opened = Adapter::Open(output);
        Adapter::Read(opened);
      }
      benchmark::DoNotOptimize(output.data());
      benchmark::DoNotOptimize(output.size());
    } else if constexpr (Op == Operation::Serialize) {
      auto output = Adapter::Serialize(builder);
      benchmark::DoNotOptimize(output.data());
      benchmark::DoNotOptimize(output.size());
    } else if constexpr (Op == Operation::Open || Op == Operation::ReadCold) {
      auto opened = Adapter::Open(bytes);
      if constexpr (Op == Operation::ReadCold) Adapter::Read(opened);
      benchmark::DoNotOptimize(&opened);
    } else
      Adapter::Read(view);
  }
}

template <Case C, typename Adapter, Operation Op>
void RegisterOne() {
  const auto name = std::string(CaseName(C)) + "/" + Adapter::Name + "/" + OpName(Op);
  benchmark::RegisterBenchmark(name.c_str(), &Run<Adapter, Op>);
}

template <Case C, typename Adapter>
void PatchSameSize(benchmark::State& state) {
  auto builder = Adapter::MakeBuilder();
  auto bytes = Adapter::Serialize(builder);
  Adapter::Check(Adapter::Open(bytes));
  state.counters["serialized_bytes"] = static_cast<double>(bytes.size());
  if constexpr (std::same_as<Adapter, Message<C>>) {
    // Typed in-place setter: O(1) structural checks on open, no full verification
    // (measured separately as Verify).
    for (auto _ : state) {
      benchmark::ClobberMemory();
      auto message = gendb::MutableView::Open(bytes);
      bool ok = message.has_value();
      if constexpr (C == Case::Position)
        ok = ok && message->template Set<3, int32_t>(251);
      else if constexpr (IsAccount(C))
        ok = ok && message->template Set<6, bool>(false);
      else
        ok = ok && message->template Set<32, uint64_t>(9999);
      if (!ok) throw std::runtime_error("patch failed");
      benchmark::DoNotOptimize(bytes.data());
    }
  } else {
    for (auto _ : state) {
      benchmark::ClobberMemory();
      if (!Adapter::Patch(bytes)) throw std::runtime_error("FlatBuffers mutation failed");
      benchmark::DoNotOptimize(bytes.data());
    }
  }
}

template <Case C, typename Adapter>
void RebuildVariable(benchmark::State& state) {
  auto builder = Adapter::MakeBuilder();
  const auto bytes = Adapter::Serialize(builder);
  Adapter::Check(Adapter::Open(bytes));
  state.counters["serialized_bytes"] = static_cast<double>(bytes.size());
  constexpr uint32_t id = C == Case::Position ? 4 : C == Case::AccountFull ? 3 : 2;
  constexpr std::string_view value = "replacement with a different length";
  for (auto _ : state) {
    benchmark::ClobberMemory();
    if constexpr (std::same_as<Adapter, Message<C>>) {
      gendb::MessagePatch patch;
      patch.SetString(id, value);
      auto output = gendb::Rebuild(Adapter::Open(bytes), patch);
      benchmark::DoNotOptimize(output.data());
      benchmark::DoNotOptimize(output.size());
    } else {
      auto output = Adapter::Rebuild(bytes, value);
      benchmark::DoNotOptimize(output.data());
      benchmark::DoNotOptimize(output.size());
    }
  }
}

// Full verification of untrusted bytes (excluded from Open/Read timings).
template <Case C>
void VerifyFull(benchmark::State& state) {
  auto builder = Message<C>::MakeBuilder();
  const auto bytes = Message<C>::Serialize(builder);
  Message<C>::Check(Message<C>::Open(bytes));
  state.counters["serialized_bytes"] = static_cast<double>(bytes.size());
  for (auto _ : state) {
    benchmark::ClobberMemory();
    if (!gendb::MessageView::Verify(bytes)) throw std::runtime_error("verification failed");
  }
}

template <Case C, typename Adapter>
void RegisterAdapter() {
  RegisterOne<C, Adapter, Operation::Build>();
  RegisterOne<C, Adapter, Operation::Serialize>();
  RegisterOne<C, Adapter, Operation::Open>();
  RegisterOne<C, Adapter, Operation::ReadHot>();
  RegisterOne<C, Adapter, Operation::ReadCold>();
  RegisterOne<C, Adapter, Operation::EndToEnd>();
  const auto prefix = std::string(CaseName(C)) + "/" + Adapter::Name;
  benchmark::RegisterBenchmark((prefix + "/PatchSameSize").c_str(), &PatchSameSize<C, Adapter>);
  if constexpr (!IsWide(C))
    benchmark::RegisterBenchmark((prefix + "/RebuildVariable").c_str(),
                                 &RebuildVariable<C, Adapter>);
}

template <Case C>
void RegisterCase() {
  RegisterAdapter<C, Message<C>>();
  RegisterAdapter<C, FlatBuffers<C>>();
  const auto prefix = std::string(CaseName(C)) + "/Message";
  benchmark::RegisterBenchmark((prefix + "/Verify").c_str(), &VerifyFull<C>);
}

const bool kRegistered = [] {
  RegisterCase<Case::Position>();
  RegisterCase<Case::AccountFull>();
  RegisterCase<Case::AccountSparse>();
  RegisterCase<Case::WideFull>();
  RegisterCase<Case::WideSparse>();
  return true;
}();

// Generated classes: the API database code uses, on top of the core reads and builds above.
void GeneratedPositionBuild(benchmark::State& state) {
  const auto& d = data::kPositionFull;
  for (auto _ : state) {
    gendb::tests::PositionBuilder builder;
    builder.set_position_id(d.position_id)
        .set_account_id(d.account_id)
        .set_volume(d.volume)
        .set_instrument(d.instrument)
        .set_open_price(d.open_price)
        .set_direction(static_cast<gendb::tests::Direction>(d.direction));
    auto bytes = builder.Build();
    benchmark::DoNotOptimize(bytes.data());
  }
}
BENCHMARK(GeneratedPositionBuild)->Name("Position/Generated/Build");

void GeneratedPositionReadCold(benchmark::State& state) {
  const auto& d = data::kPositionFull;
  gendb::tests::PositionBuilder builder;
  builder.set_position_id(d.position_id)
      .set_account_id(d.account_id)
      .set_volume(d.volume)
      .set_instrument(d.instrument)
      .set_open_price(d.open_price)
      .set_direction(static_cast<gendb::tests::Direction>(d.direction));
  const auto bytes = builder.Build();
  for (auto _ : state) {
    benchmark::ClobberMemory();
    gendb::tests::Position position{bytes};
    benchmark::DoNotOptimize(position.position_id());
    benchmark::DoNotOptimize(position.account_id());
    benchmark::DoNotOptimize(position.volume());
    benchmark::DoNotOptimize(position.instrument());
    benchmark::DoNotOptimize(position.open_price());
    benchmark::DoNotOptimize(position.direction());
  }
}
BENCHMARK(GeneratedPositionReadCold)->Name("Position/Generated/ReadCold");

}  // namespace
