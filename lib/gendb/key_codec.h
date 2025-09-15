#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>

#include "gendb/bytes.h"

namespace gendb::internal::key_codec {

// Low-level integer encoding/decoding (big-endian, sign-biased)
// WriteInteger for BytesView (no allocation, returns false if not enough space)
template <typename T>
  requires std::is_integral_v<T>
inline bool WriteInteger(T value, BytesView& out) {
  if (out.size() < sizeof(T)) return false;
  using U = std::make_unsigned_t<T>;
  U u;
  if constexpr (std::is_signed_v<T>) {
    u = static_cast<U>(value) ^ (U(1) << (sizeof(T) * 8 - 1));  // flip sign
  } else {
    u = value;
  }
  WriteScalarRaw<U, std::endian::big>(out.data(), u);
  out = out.subspan(sizeof(T));
  return true;
}

template <typename T>
  requires std::is_integral_v<T>
inline T ReadInteger(BytesConstView& in) {
  using U = std::make_unsigned_t<T>;

  if (in.size() < sizeof(T)) {
    assert(false);  // readInteger: not enough bytes
    return T{};
  }

  U u = ReadScalarRaw<U, std::endian::big>(in.data());
  in = in.subspan(sizeof(T));

  if constexpr (std::is_signed_v<T>) {
    u ^= (U(1) << (sizeof(T) * 8 - 1));
    return static_cast<T>(u);
  } else {
    return static_cast<T>(u);
  }
}

// String encoding/decoding (delimiter-based, no escaping)
inline void WriteString(std::string_view s, Bytes& out) {
  out.insert(out.end(), s.begin(), s.end());
  out.push_back(0x00);  // delimiter
}

inline std::string_view ReadStringView(BytesConstView& in) {
  // Find delimiter
  size_t len = 0;
  while (len < in.size() && in[len] != 0x00) ++len;
  if (len == in.size()) {
    assert(false);  // Unterminated string
    return {};
  }
  std::string_view sv(reinterpret_cast<const char*>(in.data()), len);
  in = in.subspan(len + 1);
  return sv;
}

// Field size calculators (for preallocating buffer)
template <typename T>
inline constexpr size_t FieldSize(const T&)
  requires std::is_integral_v<T>
{
  return sizeof(T);
}

template <typename T>
inline constexpr size_t FieldSize(const T&)
  requires std::is_enum_v<T>
{
  return sizeof(std::underlying_type_t<T>);
}

inline size_t FieldSize(const std::string& s) {
  return s.size() + 1;
}

inline size_t FieldSize(std::string_view s) {
  return s.size() + 1;
}

// Encode fields (no extra allocations, BytesView version)
template <typename T>
inline bool EncodeField(const T& v, BytesView out)
  requires std::is_integral_v<T>
{
  return WriteInteger(v, out);
}

template <typename T>
inline bool EncodeField(const T& v, BytesView out)
  requires std::is_enum_v<T>
{
  using U = std::underlying_type_t<T>;
  return WriteInteger(static_cast<U>(v), out);
}

inline bool EncodeField(std::string_view v, BytesView& out) {
  size_t needed = v.size() + 1;
  if (out.size() < needed) return false;
  std::memcpy(out.data(), v.data(), v.size());
  out.data()[v.size()] = 0x00;
  out = out.subspan(needed);
  return true;
}

inline bool EncodeField(const std::string& v, BytesView& out) {
  return EncodeField(std::string_view(v), out);
}

// Encode tuple to BytesView
template <typename Tuple, size_t... I>
bool EncodeTupleToViewImpl(const Tuple& t, BytesView& out, std::index_sequence<I...>) {
  bool ok = true;
  auto try_encode = [&](const auto& value) { ok = ok && EncodeField(value, out); };
  (try_encode(std::get<I>(t)), ...);
  return ok;
}

template <typename Tuple>
bool EncodeTupleToView(const Tuple& t, BytesView out) {
  return EncodeTupleToViewImpl(t, out, std::make_index_sequence<std::tuple_size_v<Tuple>>{});
}

// Decode fields (allocation-free with string_view)
template <typename T>
inline T DecodeField(BytesConstView& in) {
  static_assert(false);
}

template <typename T>
inline T DecodeField(BytesConstView& in)
  requires std::is_integral_v<T>
{
  return ReadInteger<T>(in);
}

template <typename T>
inline T DecodeField(BytesConstView& in)
  requires std::is_enum_v<T>
{
  using U = std::underlying_type_t<T>;
  return static_cast<T>(ReadInteger<U>(in));
}

template <>
inline std::string_view DecodeField<std::string_view>(BytesConstView& in) {
  return ReadStringView(in);
}

template <>
inline std::string DecodeField<std::string>(BytesConstView& in) {
  auto sv = ReadStringView(in);
  return std::string(sv);  // allocates
}

// Tuple encode/decode
template <typename Tuple, size_t... I>
Bytes EncodeTupleImpl(const Tuple& t, std::index_sequence<I...>) {
  size_t total = (FieldSize(std::get<I>(t)) + ...);
  Bytes out;
  out.resize(total);
  BytesView view(out.data(), out.size());

  (EncodeField(std::get<I>(t), view), ...);

  return out;
}

template <typename Tuple>
Bytes EncodeTuple(const Tuple& t) {
  return EncodeTupleImpl(t, std::make_index_sequence<std::tuple_size_v<Tuple>>{});
}

template <typename Tuple, size_t... I>
Tuple DecodeTupleImpl(BytesConstView& in, std::index_sequence<I...>) {
  return Tuple{DecodeField<std::tuple_element_t<I, Tuple>>(in)...};
}

template <typename... Ts>
std::tuple<Ts...> DecodeTuple(BytesConstView& in) {
  return DecodeTupleImpl<std::tuple<Ts...>>(in, std::index_sequence_for<Ts...>{});
}

}  // namespace gendb::internal::key_codec