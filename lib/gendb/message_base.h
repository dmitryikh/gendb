#pragma once
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "absl/container/inlined_vector.h"
#include "gendb/bytes.h"

namespace gendb {

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

enum class FixedStorageWidth : uint8_t {
  Byte1 = 1,
  Byte4 = 4,
  Byte8 = 8,
};

constexpr inline size_t kFixedStorageClassCount = 3;

constexpr inline size_t FixedStorageClassIndex(FixedStorageWidth width) {
  switch (width) {
    case FixedStorageWidth::Byte1:
      return 0;
    case FixedStorageWidth::Byte4:
      return 1;
    case FixedStorageWidth::Byte8:
      return 2;
  }
  return 0;
}

struct FixedFieldDescriptor {
  int field_id = 0;
  uint16_t logical_size = 0;
  FixedStorageWidth storage_width = FixedStorageWidth::Byte1;
  uint16_t bit_index_within_class = 0;
  uint16_t ordinal_within_class = 0;
  uint32_t mask_before_within_word = 0;
  uint16_t word_index_within_class = 0;
};

struct MessageLayout {
  constexpr MessageLayout() = default;

  template <size_t N>
  constexpr explicit MessageLayout(
      const std::array<FixedFieldDescriptor, N>& fixed_fields,
      const std::array<uint8_t, kFixedStorageClassCount>& fixed_word_counts = {})
      : fixed_fields(fixed_fields.data(), fixed_fields.size()), fixed_word_counts(fixed_word_counts) {}

  std::span<const FixedFieldDescriptor> fixed_fields;
  std::array<uint8_t, kFixedStorageClassCount> fixed_word_counts{};
};

namespace internal {

inline constexpr uint8_t kMessageFormatVersion = 2;
inline constexpr size_t kMessageHeaderSize = 20;

inline size_t VarintSize(uint32_t value) {
  size_t size = 1;
  while (value >= 0x80) {
    value >>= 7;
    ++size;
  }
  return size;
}

inline void AppendVarint(Bytes& dst, uint32_t value) {
  while (value >= 0x80) {
    dst.push_back(static_cast<uint8_t>((value & 0x7F) | 0x80));
    value >>= 7;
  }
  dst.push_back(static_cast<uint8_t>(value));
}

inline const uint8_t* ReadVarint(const uint8_t* ptr, const uint8_t* end, uint32_t& value) {
  value = 0;
  uint32_t shift = 0;
  while (ptr < end && shift < 35) {
    const uint8_t byte = *ptr++;
    value |= static_cast<uint32_t>(byte & 0x7F) << shift;
    if ((byte & 0x80) == 0) {
      return ptr;
    }
    shift += 7;
  }
  return nullptr;
}

}  // namespace internal

inline constexpr std::array<uint8_t, internal::kMessageHeaderSize> kEmptyMessage = {
    internal::kMessageFormatVersion, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20, 0, 0, 0};

class MessageBase {
 public:
  explicit MessageBase(const MessageLayout* layout = nullptr);
  MessageBase(std::span<uint8_t> buffer, const MessageLayout* layout = nullptr);
  // TODO: is there better way for const correctness?
  MessageBase(std::span<const uint8_t> buffer, const MessageLayout* layout = nullptr);
  MessageBase(std::vector<uint8_t>& buffer, const MessageLayout* layout = nullptr);

  template <typename T>
    requires IsSupportedScalar<T>
  T ReadScalarField(int field_id, T default_value) const {
    auto field_raw = LookupField(field_id);
    return !field_raw.has_value() ? default_value : ReadScalarRaw<T>(field_raw->data());
  }

  std::string_view ReadStringField(int field_id, std::string_view default_value) const {
    auto field_raw = LookupField(field_id);
    return !field_raw.has_value()
               ? default_value
               : std::string_view(reinterpret_cast<const char*>(field_raw->data()),
                                  field_raw->size());
  }

  uint16_t FieldCount() const;

  template <typename T>
    requires IsSupportedScalar<T>
  bool SetScalarField(int field_id, T value) {
    auto field_raw = LookupMutableField(field_id);
    if (!field_raw.has_value()) {
      return false;
    }
    assert(field_raw->size() >= sizeof(T));
    WriteScalarRaw<T>(field_raw->data(), value);
    if (field_raw->size() > sizeof(T)) {
      memset(field_raw->data() + sizeof(T), 0, field_raw->size() - sizeof(T));
    }
    return true;
  }
  std::span<const uint8_t> FieldRaw(int field_id) const;
  std::span<uint8_t> FieldRaw(int field_id);
  bool HasField(int field_id) const { return LookupField(field_id).has_value(); }
  absl::InlinedVector<uint32_t, 2> GetFieldsMask() const;
  absl::InlinedVector<int, 8> PresentFieldIds() const;
  const MessageLayout* Layout() const { return _layout; }

 protected:
  template <FixedFieldDescriptor Descriptor, typename T>
    requires IsSupportedScalar<T>
  T ReadFixedScalarFieldByDescriptor(T default_value) const {
    if (_layout == nullptr) {
      return default_value;
    }
    auto field_raw = LookupFixedFieldByDescriptor<Descriptor>();
    return !field_raw.has_value() ? default_value : ReadScalarRaw<T>(field_raw->data());
  }

  std::string_view ReadSparseKnownStringField(int field_id, std::string_view default_value) const {
    auto field_raw = LookupSparseField(field_id);
    return !field_raw.has_value()
               ? default_value
               : std::string_view(reinterpret_cast<const char*>(field_raw->data()),
                                  field_raw->size());
  }

 private:
  struct SparseEntry {
    int field_id = 0;
    uint32_t payload_offset = 0;
    uint32_t payload_len = 0;
  };

  struct CachedSparseField {
    int field_id = 0;
    std::span<const uint8_t> value;
  };

  template <FixedStorageWidth Width>
  uint8_t FixedWordCount() const {
    return HeaderPtr()[2 + FixedStorageClassIndex(Width)];
  }

  template <FixedStorageWidth Width>
  size_t FixedBitmapSize() const {
    return FixedWordCount<Width>() * sizeof(uint32_t);
  }

  template <FixedStorageWidth Width>
  const uint8_t* FixedBitmapPtr() const {
    const uint8_t* ptr = HeaderPtr() + internal::kMessageHeaderSize;
    if constexpr (Width == FixedStorageWidth::Byte1) {
      return ptr;
    } else if constexpr (Width == FixedStorageWidth::Byte4) {
      return ptr + FixedBitmapSize<FixedStorageWidth::Byte1>();
    } else {
      return ptr + FixedBitmapSize<FixedStorageWidth::Byte1>() +
             FixedBitmapSize<FixedStorageWidth::Byte4>();
    }
  }

  template <FixedStorageWidth Width>
  const uint8_t* FixedPayloadPtr() const {
    const uint8_t* ptr = HeaderPtr() + internal::kMessageHeaderSize +
                         FixedBitmapSize<FixedStorageWidth::Byte1>() +
                         FixedBitmapSize<FixedStorageWidth::Byte4>() +
                         FixedBitmapSize<FixedStorageWidth::Byte8>();
    if constexpr (Width == FixedStorageWidth::Byte1) {
      return ptr;
    } else if constexpr (Width == FixedStorageWidth::Byte4) {
      return ptr + FixedPayloadSize(FixedStorageWidth::Byte1);
    } else {
      return ptr + FixedPayloadSize(FixedStorageWidth::Byte1) +
             FixedPayloadSize(FixedStorageWidth::Byte4);
    }
  }

  const FixedFieldDescriptor* FindFixedField(int field_id) const;
  std::optional<std::span<const uint8_t>> LookupField(int field_id) const;
  std::optional<std::span<uint8_t>> LookupMutableField(int field_id);
  std::optional<std::span<const uint8_t>> LookupFixedField(int field_id) const;
  template <FixedFieldDescriptor Descriptor>
  std::optional<std::span<const uint8_t>> LookupFixedFieldByDescriptor() const {
    if constexpr (Descriptor.storage_width == FixedStorageWidth::Byte1) {
      if (Descriptor.word_index_within_class >= FixedWordCount<FixedStorageWidth::Byte1>()) {
        return std::nullopt;
      }
      const uint8_t* bitmap = FixedBitmapPtr<FixedStorageWidth::Byte1>();
      const uint32_t current_word =
          ReadScalarRaw<uint32_t>(bitmap + Descriptor.word_index_within_class * sizeof(uint32_t));
      const uint32_t presence_mask = 1u << (Descriptor.bit_index_within_class % 32);
      if ((current_word & presence_mask) == 0) {
        return std::nullopt;
      }
      uint32_t present_before = 0;
      if constexpr (Descriptor.word_index_within_class > 0) {
        for (size_t word = 0; word < Descriptor.word_index_within_class; ++word) {
          present_before +=
              std::popcount(ReadScalarRaw<uint32_t>(bitmap + word * sizeof(uint32_t)));
        }
      }
      present_before += std::popcount(current_word & Descriptor.mask_before_within_word);
      const uint32_t payload_offset = present_before;
      return std::span<const uint8_t>(FixedPayloadPtr<FixedStorageWidth::Byte1>() + payload_offset,
                                      static_cast<size_t>(Descriptor.storage_width));
    } else if constexpr (Descriptor.storage_width == FixedStorageWidth::Byte4) {
      if (Descriptor.word_index_within_class >= FixedWordCount<FixedStorageWidth::Byte4>()) {
        return std::nullopt;
      }
      const uint8_t* bitmap = FixedBitmapPtr<FixedStorageWidth::Byte4>();
      const uint32_t current_word =
          ReadScalarRaw<uint32_t>(bitmap + Descriptor.word_index_within_class * sizeof(uint32_t));
      const uint32_t presence_mask = 1u << (Descriptor.bit_index_within_class % 32);
      if ((current_word & presence_mask) == 0) {
        return std::nullopt;
      }
      uint32_t present_before = 0;
      if constexpr (Descriptor.word_index_within_class > 0) {
        for (size_t word = 0; word < Descriptor.word_index_within_class; ++word) {
          present_before +=
              std::popcount(ReadScalarRaw<uint32_t>(bitmap + word * sizeof(uint32_t)));
        }
      }
      present_before += std::popcount(current_word & Descriptor.mask_before_within_word);
      const uint32_t payload_offset = present_before * 4u;
      return std::span<const uint8_t>(FixedPayloadPtr<FixedStorageWidth::Byte4>() + payload_offset,
                                      static_cast<size_t>(Descriptor.storage_width));
    } else {
      if (Descriptor.word_index_within_class >= FixedWordCount<FixedStorageWidth::Byte8>()) {
        return std::nullopt;
      }
      const uint8_t* bitmap = FixedBitmapPtr<FixedStorageWidth::Byte8>();
      const uint32_t current_word =
          ReadScalarRaw<uint32_t>(bitmap + Descriptor.word_index_within_class * sizeof(uint32_t));
      const uint32_t presence_mask = 1u << (Descriptor.bit_index_within_class % 32);
      if ((current_word & presence_mask) == 0) {
        return std::nullopt;
      }
      uint32_t present_before = 0;
      if constexpr (Descriptor.word_index_within_class > 0) {
        for (size_t word = 0; word < Descriptor.word_index_within_class; ++word) {
          present_before +=
              std::popcount(ReadScalarRaw<uint32_t>(bitmap + word * sizeof(uint32_t)));
        }
      }
      present_before += std::popcount(current_word & Descriptor.mask_before_within_word);
      const uint32_t payload_offset = present_before * 8u;
      return std::span<const uint8_t>(FixedPayloadPtr<FixedStorageWidth::Byte8>() + payload_offset,
                                      static_cast<size_t>(Descriptor.storage_width));
    }
  }
  std::optional<std::span<const uint8_t>> LookupFixedFieldByDescriptorRuntime(
      const FixedFieldDescriptor& field) const;
  std::optional<std::span<uint8_t>> LookupFixedFieldMutable(int field_id);
  std::optional<std::span<const uint8_t>> LookupSparseField(int field_id) const;
  std::optional<std::span<uint8_t>> LookupSparseFieldMutable(int field_id);
  std::optional<std::span<const uint8_t>> LookupSparseFieldCached(int field_id) const;
  void CacheSparseField(int field_id, std::span<const uint8_t> value) const;
  std::optional<SparseEntry> DecodeSparseEntry(size_t index) const;
  const absl::InlinedVector<SparseEntry, 8>& DecodedSparseEntries() const;
  const uint8_t* HeaderPtr() const { return _buffer; }
  uint8_t* HeaderPtr() { return _buffer; }
  uint8_t FixedWordCount() const;
  uint8_t FixedWordCount(FixedStorageWidth width) const;
  uint16_t SparseCount() const;
  uint16_t FixedPayloadSize(FixedStorageWidth width) const;
  uint16_t SparseDirSize() const;
  uint32_t TotalSize() const;
  size_t FixedBitmapSize(FixedStorageWidth width) const;
  const uint8_t* FixedBitmapPtr(FixedStorageWidth width) const;
  uint8_t* FixedBitmapPtr(FixedStorageWidth width);
  const uint8_t* FixedPayloadPtr(FixedStorageWidth width) const;
  uint8_t* FixedPayloadPtr(FixedStorageWidth width);
  const uint8_t* SparseDirPtr() const;
  uint8_t* SparseDirPtr();
  const uint8_t* SparsePayloadPtr() const;
  uint8_t* SparsePayloadPtr();
  bool IsFixedBitSet(const FixedFieldDescriptor& field) const;

  uint8_t* _buffer;
  size_t _buffer_size;
  const MessageLayout* _layout;
  mutable bool _sparse_index_built = false;
  mutable absl::InlinedVector<SparseEntry, 8> _decoded_sparse_entries;
  mutable absl::InlinedVector<CachedSparseField, 4> _cached_sparse_fields;
};
}  // namespace gendb
