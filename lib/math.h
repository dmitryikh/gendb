#pragma once

namespace gendb {
// Returns ceil(a / b) for integers
template <typename T>
constexpr T DivRoundUp(T a, T b) {
  return (a + b - 1) / b;
}
}  // namespace gendb
