// Message format benchmarks.
// Methodology: benchmarks/README.md.
#include <benchmark/benchmark.h>

#include <concepts>
#include <stdexcept>
#include <string>
#include <string_view>

#include "account.gendb.h"
#include "gendb/message.h"
#include "message_benchmark_common.h"
#include "order.gendb.h"
#include "position.gendb.h"

namespace {
namespace data = gendb::benchmarks;

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

// clang-format off
#define WIDE_FIELDS(X) \
  X(1) X(2) X(3) X(4) X(5) X(6) X(7) X(8) X(9) X(10) X(11) X(12) X(13) X(14) X(15) X(16) \
  X(17) X(18) X(19) X(20) X(21) X(22) X(23) X(24) X(25) X(26) X(27) X(28) X(29) X(30) X(31) X(32) \
  X(33) X(34) X(35) X(36) X(37) X(38) X(39) X(40) X(41) X(42) X(43) X(44) X(45) X(46) X(47) X(48) \
  X(49) X(50) X(51) X(52) X(53) X(54) X(55) X(56) X(57) X(58) X(59) X(60) X(61) X(62) X(63) X(64)
// clang-format on

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
      if (!Adapter::Patch(bytes)) throw std::runtime_error("mutation failed");
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

// Nested messages and repeated fields. Order/Lines has that many messages, each with a string, an
// int and a double. Order/Prices has that many doubles.
namespace nested {
using namespace gendb::tests;

gendb::Bytes OrderWithLines(int count) {
  OrderBuilder order;
  order.set_order_id(1);
  OrderLineBuilder line;
  for (int i = 0; i < count; ++i) {
    line.Reset();
    line.set_sku("SKU-12345").set_quantity(i).set_price(1.5);
    order.add_lines(line);
  }
  return order.Build();
}

void LinesBuild(benchmark::State& state) {
  for (auto _ : state) {
    auto bytes = OrderWithLines(static_cast<int>(state.range(0)));
    benchmark::DoNotOptimize(bytes.data());
  }
  state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(LinesBuild)->Name("Order/Lines/Build")->Arg(10)->Arg(1000);

void LinesRead(benchmark::State& state) {
  const auto bytes = OrderWithLines(static_cast<int>(state.range(0)));
  for (auto _ : state) {
    benchmark::ClobberMemory();
    int64_t sum = 0;
    for (const OrderLine& line : Order{bytes}.lines()) sum += line.quantity();
    benchmark::DoNotOptimize(sum);
  }
  state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(LinesRead)->Name("Order/Lines/ReadOneFieldOfEach")->Arg(10)->Arg(1000);

void LinesVerify(benchmark::State& state) {
  const auto bytes = OrderWithLines(static_cast<int>(state.range(0)));
  for (auto _ : state) {
    benchmark::ClobberMemory();
    benchmark::DoNotOptimize(Order::Verify(bytes));
  }
  state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(LinesVerify)->Name("Order/Lines/Verify")->Arg(10)->Arg(1000);

void PricesBuild(benchmark::State& state) {
  const std::vector<double> prices(static_cast<size_t>(state.range(0)), 1.5);
  for (auto _ : state) {
    OrderBuilder order;
    order.set_order_id(1).set_prices(prices);
    auto bytes = order.Build();
    benchmark::DoNotOptimize(bytes.data());
  }
  state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(PricesBuild)->Name("Order/Prices/Build")->Arg(10)->Arg(1000);

void PricesRead(benchmark::State& state) {
  const std::vector<double> prices(static_cast<size_t>(state.range(0)), 1.5);
  OrderBuilder order;
  order.set_prices(prices);
  const auto bytes = order.Build();
  for (auto _ : state) {
    benchmark::ClobberMemory();
    double sum = 0;
    for (const double price : Order{bytes}.prices()) sum += price;
    benchmark::DoNotOptimize(sum);
  }
  state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(PricesRead)->Name("Order/Prices/ReadAll")->Arg(10)->Arg(1000);

void NestedRead(benchmark::State& state) {
  const auto bytes =
      OrderBuilder()
          .set_customer(CustomerBuilder().set_name("Ann").set_address(AddressBuilder().set_zip(7)))
          .Build();
  for (auto _ : state) {
    benchmark::ClobberMemory();
    benchmark::DoNotOptimize(Order{bytes}.customer().address().zip());
  }
}
BENCHMARK(NestedRead)->Name("Order/Nested/ReadThreeLevelsDown");
}  // namespace nested

}  // namespace
