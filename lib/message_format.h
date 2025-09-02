#pragma once
#include <bit>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/inlined_vector.h"
#include "bits.h"
#include "message_patch.h"

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

// Message binary repr:
// [num_fields]{2b}
// [field_0_offset]{2b}
// [field_1_offset]{2b}
// ...
// [field num_fields - 1 offset]{2b}
// [message_size]{2b}
// [field_0_bytes] unaligned, size of `field_1_offset` - `field_0_offset`
// [field_1_bytes] unaligned, size of `field_2_offset` - `field_1_offset`, if size = 0 the field is
// not set

class MessageBase {
 public:
  MessageBase();

  MessageBase(std::span<uint8_t> span);

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

  template <size_t K, std::array<uint32_t, K> FixedSizeFields>
  bool CanApplyPatchInplace(const MessagePatch& patch) const {
    // Patch can be done in-place if:
    // 1. The modified fields are subset of the set original message fields.
    // 2. The modified fields are subset of the fixed size fields.
    // 3. The removed fields are subset of inverted set original message fields.

    // Get bitmasks for current, modified, and removed fields
    absl::InlinedVector<uint32_t, 2> current_mask = GetFieldsMask();

    // 1. Modified fields must be subset of current fields
    if (!IsBitmaskSubset(current_mask, patch.modified)) return false;
    // 2. Modified fields must be subset of fixed size fields
    if (!IsBitmaskSubset(std::span<const uint32_t>(FixedSizeFields), patch.modified)) return false;
    // 3. Removed fields must be subset of inverted current fields
    absl::InlinedVector<uint32_t, 2> inverted_mask(current_mask.size(), 0);
    for (size_t i = 0; i < current_mask.size(); ++i) {
      inverted_mask[i] = ~current_mask[i];
    }
    if (!IsBitmaskSubset(inverted_mask, patch.removed)) return false;
    return true;
  }

 private:
  uint8_t* _buffer;
  //   uint8_t* _vtable;
};

class MessageBuilder {
 public:
  MessageBuilder();
  MessageBuilder(const MessageBase& src);

  template <typename T>
    requires IsSupportedScalar<T>
  void AddField(int field_id, T value) {
    if constexpr (!kLittleEndianArch) {
      // Convert from host order to LittleEndian.
      value = std::byteswap(value);
    }
    AddFieldRaw(field_id,
                std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&value), sizeof(value)));
  }

  void AddStringField(int field_id, std::string_view value) {
    AddFieldRaw(field_id, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(value.data()),
                                                   value.size()));
  }

  void AddFieldRaw(int field_id, std::span<const uint8_t> data);

  // Returns empty vector in case of failures.
  std::vector<uint8_t> Build();

  void MergeFields(const MessageBase& rhs);

  bool NeedRebuild() const { return !_fields.empty(); }

  std::map<int, std::span<const uint8_t>> GetFieldData() const {
    std::map<int, std::span<const uint8_t>> field_data;
    for (const auto& [field_id, field_raw] : _fields) {
      field_data[field_id] = std::span<const uint8_t>(
          reinterpret_cast<const uint8_t*>(field_raw.data()), field_raw.size());
    }
    return field_data;
  }

 private:
  std::map</*field_id*/ int, /*field_raw*/ std::string> _fields;
};

// Empty message would look like: 2 bytes (vtable_offset) = 0
}  // namespace gendb