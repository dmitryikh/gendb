#pragma once

#include <cstdint>
#include <string_view>

namespace gendb::benchmarks {

struct AccountDataset {
  uint64_t account_id;
  std::string_view trader_id;
  std::string_view name;
  std::string_view address;
  int32_t age;
  bool is_active;
  float balance;
  std::string_view config_name;
};

struct PositionDataset {
  int32_t position_id;
  int32_t account_id;
  int32_t volume;
  std::string_view instrument;
  float open_price;
  uint32_t direction;
};

inline constexpr AccountDataset kAccountFull{
    .account_id = 42,
    .trader_id = "TRADER-00042",
    .name = "Alice Example",
    .address = "42 Benchmark Avenue, Zurich",
    .age = 31,
    .is_active = true,
    .balance = 1024.5f,
    .config_name = "default-risk",
};

inline constexpr AccountDataset kAccountSparse{
    .account_id = 42,
    .trader_id = "",
    .name = "",
    .address = "",
    .age = 0,
    .is_active = true,
    .balance = 0.0f,
    .config_name = "",
};

inline constexpr PositionDataset kPositionFull{
    .position_id = 1001,
    .account_id = 42,
    .volume = 250,
    .instrument = "EURUSD",
    .open_price = 1.2345f,
    .direction = 1,
};

}  // namespace gendb::benchmarks
