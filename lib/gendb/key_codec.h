#pragma once

#include <bit>
#include <cassert>
#include <concepts>
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

// A string keeps its order and ends unambiguously, so that it can be followed by another field: a
// zero byte is written as 00 FF, and the end as 00 01, which sorts below any continuation.
inline constexpr uint8_t kStringEscape = 0xff;
inline constexpr uint8_t kStringEnd = 0x01;

inline size_t EncodedStringSize(std::string_view s) noexcept {
  size_t zeros = 0;
  for (const char c : s) zeros += c == 0 ? 1 : 0;
  return s.size() + zeros + 2;
}

inline void WriteString(std::string_view s, uint8_t* out) noexcept {
  for (const char c : s) {
    *out++ = static_cast<uint8_t>(c);
    if (c == 0) *out++ = kStringEscape;
  }
  *out++ = 0x00;
  *out = kStringEnd;
}

inline void WriteString(std::string_view s, Bytes& out) {
  const size_t at = out.size();
  out.resize(at + EncodedStringSize(s));
  WriteString(s, out.data() + at);
}

inline std::string ReadString(BytesConstView& in) {
  std::string out;
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] != 0x00) {
      out.push_back(static_cast<char>(in[i]));
      continue;
    }
    if (i + 1 == in.size()) break;
    if (in[i + 1] == kStringEnd) {
      in = in.subspan(i + 2);
      return out;
    }
    out.push_back('\0');
    ++i;
  }
  assert(false);  // Unterminated string
  in = {};
  return out;
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

template <std::floating_point T>
inline constexpr size_t FieldSize(const T&) {
  return sizeof(T);
}

inline size_t FieldSize(const std::string& s) {
  return EncodedStringSize(s);
}

inline size_t FieldSize(std::string_view s) {
  return EncodedStringSize(s);
}

// Encode fields (no extra allocations, BytesView version)
template <typename T>
inline bool EncodeField(const T& v, BytesView& out)
  requires std::is_integral_v<T>
{
  return WriteInteger(v, out);
}

template <typename T>
inline bool EncodeField(const T& v, BytesView& out)
  requires std::is_enum_v<T>
{
  using U = std::underlying_type_t<T>;
  return WriteInteger(static_cast<U>(v), out);
}

// Sign-magnitude to order-preserving: negatives reverse, and all sort below positives.
template <std::floating_point T>
inline bool EncodeField(const T& v, BytesView& out) {
  using U = std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>;
  const U bits = std::bit_cast<U>(v);
  const U sign = U(1) << (sizeof(U) * 8 - 1);
  return WriteInteger<U>((bits & sign) ? ~bits : (bits | sign), out);
}

inline bool EncodeField(std::string_view v, BytesView& out) {
  const size_t needed = EncodedStringSize(v);
  if (out.size() < needed) return false;
  WriteString(v, out.data());
  out = out.subspan(needed);
  return true;
}

inline bool EncodeField(const std::string& v, BytesView& out) {
  return EncodeField(std::string_view(v), out);
}

// Appends one field's encoding, for composite keys built in a string.
template <typename T>
inline void AppendField(const T& v, std::string& out) {
  const size_t offset = out.size();
  const size_t size = FieldSize(v);
  out.resize(offset + size);
  BytesView view(reinterpret_cast<uint8_t*>(out.data()) + offset, size);
  EncodeField(v, view);
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
inline T DecodeField(BytesConstView&) {
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
inline std::string DecodeField<std::string>(BytesConstView& in) {
  return ReadString(in);
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