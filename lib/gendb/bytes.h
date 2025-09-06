#pragma once

#include <cstdint>
#include <vector>

#include "absl/hash/hash.h"
#include "absl/types/span.h"

namespace gendb {

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

}  // namespace gendb
