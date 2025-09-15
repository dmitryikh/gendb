#pragma once

#include <bit>
#include <cstdint>
#include <vector>

#include "absl/hash/hash.h"
#include "absl/types/span.h"

namespace gendb {

constexpr bool kLittleEndianArch = (std::endian::native == std::endian::little);

using Bytes = std::vector<uint8_t>;
using BytesView = std::span<uint8_t>;
using BytesConstView = std::span<const uint8_t>;

struct BytesHash {
  using is_transparent = void;  // Enables heterogeneous lookup

  size_t operator()(const Bytes& v) const { return absl::Hash<Bytes>{}(v); }
  size_t operator()(BytesConstView s) const { return absl::Hash<absl::Span<const uint8_t>>{}(s); }
};

struct BytesEqual {
  using is_transparent = void;

  bool operator()(const Bytes& a, const Bytes& b) const { return a == b; }
  bool operator()(const Bytes& a, BytesConstView b) const {
    return std::equal(a.begin(), a.end(), b.begin(), b.end());
  }
  bool operator()(BytesConstView a, const Bytes& b) const {
    return std::equal(a.begin(), a.end(), b.begin(), b.end());
  }
  bool operator()(BytesConstView a, BytesConstView b) const {
    return std::equal(a.begin(), a.end(), b.begin(), b.end());
  }
};

template <typename T, std::endian Endianness = std::endian::little>
T ReadScalarRaw(const void* buffer) {
  T value;
  std::memcpy(&value, buffer, sizeof(value));
  if constexpr (Endianness != std::endian::native) {
    // Convert from Endianness to host order.
    value = std::byteswap(value);
  }
  return value;
}

template <typename T, std::endian Endianness = std::endian::little>
void WriteScalarRaw(void* buffer, T value) {
  if constexpr (Endianness != std::endian::native) {
    // Convert from host order to Endianness.
    value = std::byteswap(value);
  }
  std::memcpy(buffer, &value, sizeof(value));
}

}  // namespace gendb
