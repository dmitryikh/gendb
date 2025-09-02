#pragma once

#include <absl/container/inlined_vector.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>

#include "math.h"

namespace gendb {

using Bitmask = absl::InlinedVector<uint32_t, 2>;

template <typename T>
constexpr T constexpr_max(std::initializer_list<T> list) {
  T max = *list.begin();
  for (auto it = list.begin(); it != list.end(); ++it) {
    if (*it > max) max = *it;
  }
  return max;
}

template <size_t N, typename ValueType>
  requires(std::is_integral_v<ValueType> || std::is_enum_v<ValueType>)
constexpr std::array<uint32_t, N> MakeConstexprFieldBitmask(
    std::initializer_list<ValueType> field_ids) {
  std::array<uint32_t, N> mask = {};
  for (size_t field_id : field_ids) {
    mask[(field_id - 1) / 32] |= (1u << ((field_id - 1) % 32));
  }
  return mask;
}

template <typename ValueType>
  requires(std::is_integral_v<ValueType> || std::is_enum_v<ValueType>)
Bitmask MakeFieldBitmask(const std::span<const ValueType> field_ids) {
  Bitmask mask;
  if (field_ids.empty()) return mask;

  const ValueType max_field_id = *std::max_element(field_ids.begin(), field_ids.end());
  mask.resize(DivRoundUp(static_cast<int>(max_field_id), 32));
  for (size_t field_id : field_ids) {
    mask[(field_id - 1) / 32] |= (1u << ((field_id - 1) % 32));
  }
  return mask;
}

template <typename ValueType>
  requires(std::is_integral_v<ValueType> || std::is_enum_v<ValueType>)
Bitmask MakeFieldBitmask(std::initializer_list<ValueType> field_ids) {
  return MakeFieldBitmask(std::span<const ValueType>(field_ids));
}

inline bool IsBitmaskSubset(std::span<const uint32_t> a, std::span<const uint32_t> b) {
  std::size_t min_size = std::min(a.size(), b.size());
  for (std::size_t i = 0; i < min_size; ++i) {
    if ((a[i] & b[i]) != b[i]) return false;
  }
  // If b is longer, all trailing words must be zero
  for (std::size_t i = a.size(); i < b.size(); ++i) {
    if (b[i] != 0) return false;
  }
  return true;
}
}  // namespace gendb