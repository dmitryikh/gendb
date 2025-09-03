#pragma once

#include <absl/container/inlined_vector.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>

#include "gendb/math.h"

namespace gendb {
// Iterates over all set bits in the bitmask and calls functor(field_id) for each.
template <typename Functor>
void ForEachSetField(std::span<const uint32_t> bitmask, Functor&& func) {
  for (size_t word_idx = 0; word_idx < bitmask.size(); ++word_idx) {
    uint32_t word = bitmask[word_idx];
    while (word) {
      int bit = std::countr_zero(word);
      int field_id = static_cast<int>(word_idx * 32 + bit + 1);  // 1-based field ids
      func(field_id);
      word &= ~(1u << bit);
    }
  }
}

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

template <typename ValueType>
  requires(std::is_integral_v<ValueType> || std::is_enum_v<ValueType>)
void SetFieldBit(Bitmask& bitmask, ValueType field_id) {
  const size_t word_id = (field_id - 1) / 32;
  if (word_id >= bitmask.size()) {
    bitmask.resize(word_id + 1);
  }
  if (field_id > 0) {
    bitmask[word_id] |= (1u << ((field_id - 1) % 32));
  }
}

template <typename ValueType>
  requires(std::is_integral_v<ValueType> || std::is_enum_v<ValueType>)
void UnsetFieldBit(Bitmask& bitmask, ValueType field_id) {
  const size_t word_id = (field_id - 1) / 32;
  if (word_id < bitmask.size()) {
    bitmask[word_id] &= ~(1u << ((field_id - 1) % 32));
  }
}

// Returns true, if b is subset of a.
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

// Returns true, if b is subset of ~a.
inline bool IsBitmaskSubsetInverted(std::span<const uint32_t> a, std::span<const uint32_t> b) {
  std::size_t min_size = std::min(a.size(), b.size());
  for (std::size_t i = 0; i < min_size; ++i) {
    if ((~a[i] & b[i]) != b[i]) return false;
  }
  // If b is longer, all trailing words must be zero
  for (std::size_t i = a.size(); i < b.size(); ++i) {
    if (b[i] != 0) return false;
  }
  return true;
}
}  // namespace gendb