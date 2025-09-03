#pragma once
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

#include "absl/container/inlined_vector.h"
#include "gendb/message_patch.h"

namespace gendb {

constexpr bool kLittleEndianArch = (std::endian::native == std::endian::little);

enum class FieldType : uint8_t {
  None = 0,
  UType = 1,
  Bool = 2,
  Byte = 3,
  UByte = 4,
  Short = 5,
  UShort = 6,
  Int = 7,
  UInt = 8,
  Long = 9,
  ULong = 10,
  Float = 11,
  Double = 12,
  String = 13,
  Vector = 14,
  Obj = 15,
  Union = 16,
  Array = 17,
  Vector64 = 18,
  MaxBaseType = 19
};

// Primary template: unsupported types return 0
template <FieldType T>
inline constexpr size_t FieldSize = 0;

// Specializations for supported types
template <>
inline constexpr size_t FieldSize<FieldType::Bool> = 1;
template <>
inline constexpr size_t FieldSize<FieldType::Byte> = 1;
template <>
inline constexpr size_t FieldSize<FieldType::UByte> = 1;
template <>
inline constexpr size_t FieldSize<FieldType::Short> = 2;
template <>
inline constexpr size_t FieldSize<FieldType::UShort> = 2;
template <>
inline constexpr size_t FieldSize<FieldType::Int> = 4;
template <>
inline constexpr size_t FieldSize<FieldType::UInt> = 4;
template <>
inline constexpr size_t FieldSize<FieldType::Long> = 8;
template <>
inline constexpr size_t FieldSize<FieldType::ULong> = 8;
template <>
inline constexpr size_t FieldSize<FieldType::Float> = 4;
template <>
inline constexpr size_t FieldSize<FieldType::Double> = 8;

static_assert(FieldSize<FieldType::Bool> == sizeof(bool));
static_assert(FieldSize<FieldType::Byte> == sizeof(int8_t));
static_assert(FieldSize<FieldType::UByte> == sizeof(uint8_t));
static_assert(FieldSize<FieldType::Short> == sizeof(int16_t));
static_assert(FieldSize<FieldType::UShort> == sizeof(uint16_t));
static_assert(FieldSize<FieldType::Int> == sizeof(int32_t));
static_assert(FieldSize<FieldType::UInt> == sizeof(uint32_t));
static_assert(FieldSize<FieldType::Long> == sizeof(int64_t));
static_assert(FieldSize<FieldType::ULong> == sizeof(uint64_t));
static_assert(FieldSize<FieldType::Float> == sizeof(float));
static_assert(FieldSize<FieldType::Double> == sizeof(double));

template <typename T>
inline constexpr bool IsSupportedScalar = false;

template <>
inline constexpr bool IsSupportedScalar<bool> = true;
template <>
inline constexpr bool IsSupportedScalar<int8_t> = true;
template <>
inline constexpr bool IsSupportedScalar<uint8_t> = true;
template <>
inline constexpr bool IsSupportedScalar<int16_t> = true;
template <>
inline constexpr bool IsSupportedScalar<uint16_t> = true;
template <>
inline constexpr bool IsSupportedScalar<int32_t> = true;
template <>
inline constexpr bool IsSupportedScalar<uint32_t> = true;
template <>
inline constexpr bool IsSupportedScalar<int64_t> = true;
template <>
inline constexpr bool IsSupportedScalar<uint64_t> = true;
template <>
inline constexpr bool IsSupportedScalar<float> = true;
template <>
inline constexpr bool IsSupportedScalar<double> = true;

constexpr inline int kMaxMessageSize = std::numeric_limits<uint16_t>::max();

template <typename T>
T ReadScalarRaw(void* buffer) {
  T value;
  std::memcpy(&value, buffer, sizeof(value));
  if constexpr (!kLittleEndianArch) {
    // Convert from little endian to host order
    value = std::byteswap(value);
  }
  return value;
}

template <typename T>
void WriteScalarRaw(void* buffer, T value) {
  if constexpr (!kLittleEndianArch) {
    // Convert from host order to little endian
    value = std::byteswap(value);
  }
  std::memcpy(buffer, &value, sizeof(value));
}

inline std::array<uint16_t, 1> kEmptyMessage = {0};

class MessageBase {
 public:
  MessageBase();
  MessageBase(std::span<uint8_t> span);
  // TODO: is there better way for const correctness?
  MessageBase(const std::vector<uint8_t>& buffer) : _buffer(const_cast<uint8_t*>(buffer.data())) {}

  template <typename T>
    requires IsSupportedScalar<T>
  T ReadScalarField(int field_id, T default_value) const {
    const std::span<uint8_t> field_raw = FieldRaw(field_id);
    return field_raw.empty() ? default_value : ReadScalarRaw<T>(field_raw.data());
  }

  std::string_view ReadStringField(int field_id, std::string_view default_value) const {
    const std::span<uint8_t> field_raw = FieldRaw(field_id);
    return field_raw.empty() ? default_value
                             : std::string_view(reinterpret_cast<const char*>(field_raw.data()),
                                                field_raw.size());
  }

  uint16_t FieldCount() const { return ReadScalarRaw<uint16_t>(_buffer); }

  template <typename T>
    requires IsSupportedScalar<T>
  bool SetScalarField(int field_id, T value) {
    const std::span<uint8_t> field_raw = FieldRaw(field_id);
    if (field_raw.empty()) {
      return false;
    }
    assert(field_raw.size() == sizeof(T));
    WriteScalarRaw<T>(field_raw.data(), value);
    return true;
  }
  std::span<uint8_t> FieldRaw(int field_id) const;
  bool HasField(int field_id) const { return !FieldRaw(field_id).empty(); }
  absl::InlinedVector<uint32_t, 2> GetFieldsMask() const;
  bool CanApplyPatchInplace(const MessagePatch& patch,
                            std::span<const uint32_t> all_fixed_size_fields) const;
  bool ApplyPatchInplace(const MessagePatch& patch);
  void ApplyPatch(const MessagePatch& patch, std::vector<uint8_t>& buffer) const;

 private:
  uint8_t* _buffer;
};
}  // namespace gendb
