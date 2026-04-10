#include "gendb/message_base.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <span>

#include "gendb/bits.h"
namespace gendb {

MessageBase::MessageBase(const MessageLayout* layout)
    : _buffer(const_cast<uint8_t*>(kEmptyMessage.data())),
      _buffer_size(kEmptyMessage.size()),
      _layout(layout) {}

MessageBase::MessageBase(std::span<uint8_t> buffer, const MessageLayout* layout)
    : _buffer(buffer.data()), _buffer_size(buffer.size()), _layout(layout) {
  assert(buffer.size() >= internal::kMessageHeaderSize);
}
MessageBase::MessageBase(std::span<const uint8_t> buffer, const MessageLayout* layout)
    : MessageBase(std::span<uint8_t>(const_cast<uint8_t*>(buffer.data()), buffer.size()), layout) {}

MessageBase::MessageBase(std::vector<uint8_t>& buffer, const MessageLayout* layout)
    : MessageBase(std::span<uint8_t>(buffer.data(), buffer.size()), layout) {}

namespace {

uint32_t ReadBitmapWord(const uint8_t* bitmap, size_t word_index) {
  return ReadScalarRaw<uint32_t>(bitmap + word_index * sizeof(uint32_t));
}

size_t WidthIndex(FixedStorageWidth width) { return FixedStorageClassIndex(width); }

}  // namespace

uint8_t MessageBase::FixedWordCount() const {
  return static_cast<uint8_t>(FixedWordCount(FixedStorageWidth::Byte1) +
                              FixedWordCount(FixedStorageWidth::Byte4) +
                              FixedWordCount(FixedStorageWidth::Byte8));
}

uint8_t MessageBase::FixedWordCount(FixedStorageWidth width) const {
  return HeaderPtr()[2 + WidthIndex(width)];
}

uint16_t MessageBase::SparseCount() const { return ReadScalarRaw<uint16_t>(HeaderPtr() + 6); }

uint16_t MessageBase::FixedPayloadSize(FixedStorageWidth width) const {
  return ReadScalarRaw<uint16_t>(HeaderPtr() + 8 + WidthIndex(width) * sizeof(uint16_t));
}

uint16_t MessageBase::SparseDirSize() const { return ReadScalarRaw<uint16_t>(HeaderPtr() + 14); }

uint32_t MessageBase::TotalSize() const { return ReadScalarRaw<uint32_t>(HeaderPtr() + 16); }

size_t MessageBase::FixedBitmapSize(FixedStorageWidth width) const {
  return FixedWordCount(width) * sizeof(uint32_t);
}

const uint8_t* MessageBase::FixedBitmapPtr(FixedStorageWidth width) const {
  const uint8_t* ptr = HeaderPtr() + internal::kMessageHeaderSize;
  if (width == FixedStorageWidth::Byte1) {
    return ptr;
  }
  ptr += FixedBitmapSize(FixedStorageWidth::Byte1);
  if (width == FixedStorageWidth::Byte4) {
    return ptr;
  }
  return ptr + FixedBitmapSize(FixedStorageWidth::Byte4);
}

uint8_t* MessageBase::FixedBitmapPtr(FixedStorageWidth width) {
  return const_cast<uint8_t*>(static_cast<const MessageBase*>(this)->FixedBitmapPtr(width));
}

const uint8_t* MessageBase::FixedPayloadPtr(FixedStorageWidth width) const {
  const uint8_t* ptr = HeaderPtr() + internal::kMessageHeaderSize +
                       FixedBitmapSize(FixedStorageWidth::Byte1) +
                       FixedBitmapSize(FixedStorageWidth::Byte4) +
                       FixedBitmapSize(FixedStorageWidth::Byte8);
  if (width == FixedStorageWidth::Byte1) {
    return ptr;
  }
  ptr += FixedPayloadSize(FixedStorageWidth::Byte1);
  if (width == FixedStorageWidth::Byte4) {
    return ptr;
  }
  return ptr + FixedPayloadSize(FixedStorageWidth::Byte4);
}

uint8_t* MessageBase::FixedPayloadPtr(FixedStorageWidth width) {
  return const_cast<uint8_t*>(static_cast<const MessageBase*>(this)->FixedPayloadPtr(width));
}

const uint8_t* MessageBase::SparseDirPtr() const {
  return FixedPayloadPtr(FixedStorageWidth::Byte8) + FixedPayloadSize(FixedStorageWidth::Byte8);
}

uint8_t* MessageBase::SparseDirPtr() { return FixedPayloadPtr(FixedStorageWidth::Byte8) + FixedPayloadSize(FixedStorageWidth::Byte8); }

const uint8_t* MessageBase::SparsePayloadPtr() const { return SparseDirPtr() + SparseDirSize(); }

uint8_t* MessageBase::SparsePayloadPtr() { return SparseDirPtr() + SparseDirSize(); }

const FixedFieldDescriptor* MessageBase::FindFixedField(int field_id) const {
  if (_layout == nullptr) {
    return nullptr;
  }
  for (const auto& field : _layout->fixed_fields) {
    if (field.field_id == field_id) {
      return &field;
    }
  }
  return nullptr;
}

bool MessageBase::IsFixedBitSet(const FixedFieldDescriptor& field) const {
  return field.word_index_within_class < FixedWordCount(field.storage_width) &&
         (ReadBitmapWord(FixedBitmapPtr(field.storage_width), field.word_index_within_class) &
          (1u << (field.bit_index_within_class % 32))) != 0;
}

std::optional<std::span<const uint8_t>> MessageBase::LookupFixedField(int field_id) const {
  const FixedFieldDescriptor* field = FindFixedField(field_id);
  if (field == nullptr) {
    return std::nullopt;
  }

  return LookupFixedFieldByDescriptorRuntime(*field);
}

std::optional<std::span<const uint8_t>> MessageBase::LookupFixedFieldByDescriptorRuntime(
    const FixedFieldDescriptor& field) const {
  if (!IsFixedBitSet(field)) {
    return std::nullopt;
  }

  uint32_t present_before = 0;
  const uint8_t* bitmap = FixedBitmapPtr(field.storage_width);
  for (size_t word = 0; word < field.word_index_within_class; ++word) {
    present_before += std::popcount(ReadBitmapWord(bitmap, word));
  }
  present_before += std::popcount(ReadBitmapWord(bitmap, field.word_index_within_class) &
                                  field.mask_before_within_word);

  const uint32_t payload_offset = present_before * static_cast<uint32_t>(field.storage_width);
  return std::span<const uint8_t>(FixedPayloadPtr(field.storage_width) + payload_offset,
                                  static_cast<size_t>(field.storage_width));
}

std::optional<std::span<uint8_t>> MessageBase::LookupFixedFieldMutable(int field_id) {
  auto field = static_cast<const MessageBase*>(this)->LookupFixedField(field_id);
  if (!field.has_value()) {
    return std::nullopt;
  }
  return std::span<uint8_t>(const_cast<uint8_t*>(field->data()), field->size());
}

std::optional<MessageBase::SparseEntry> MessageBase::DecodeSparseEntry(size_t index) const {
  if (index >= SparseCount()) {
    return std::nullopt;
  }
  const uint8_t* ptr = SparseDirPtr();
  const uint8_t* end = SparsePayloadPtr();
  SparseEntry entry;
  uint32_t field_id = 0;
  uint32_t payload_offset = 0;
  for (size_t i = 0; i <= index; ++i) {
    uint32_t field_delta = 0;
    uint32_t offset_delta = 0;
    uint32_t payload_len = 0;
    ptr = internal::ReadVarint(ptr, end, field_delta);
    if (ptr == nullptr) {
      return std::nullopt;
    }
    ptr = internal::ReadVarint(ptr, end, offset_delta);
    if (ptr == nullptr) {
      return std::nullopt;
    }
    ptr = internal::ReadVarint(ptr, end, payload_len);
    if (ptr == nullptr) {
      return std::nullopt;
    }
    field_id += field_delta;
    payload_offset += offset_delta;
    entry = SparseEntry{
        .field_id = static_cast<int>(field_id),
        .payload_offset = payload_offset,
        .payload_len = payload_len,
    };
  }
  return entry;
}

const absl::InlinedVector<MessageBase::SparseEntry, 8>& MessageBase::DecodedSparseEntries() const {
  if (_sparse_index_built) {
    return _decoded_sparse_entries;
  }

  _decoded_sparse_entries.clear();
  _decoded_sparse_entries.reserve(SparseCount());

  const uint8_t* ptr = SparseDirPtr();
  const uint8_t* end = SparsePayloadPtr();
  uint32_t field_id = 0;
  uint32_t payload_offset = 0;
  for (size_t i = 0; i < SparseCount(); ++i) {
    uint32_t field_delta = 0;
    uint32_t offset_delta = 0;
    uint32_t payload_len = 0;
    ptr = internal::ReadVarint(ptr, end, field_delta);
    if (ptr == nullptr) {
      break;
    }
    ptr = internal::ReadVarint(ptr, end, offset_delta);
    if (ptr == nullptr) {
      break;
    }
    ptr = internal::ReadVarint(ptr, end, payload_len);
    if (ptr == nullptr) {
      break;
    }
    field_id += field_delta;
    payload_offset += offset_delta;
    _decoded_sparse_entries.push_back(SparseEntry{
        .field_id = static_cast<int>(field_id),
        .payload_offset = payload_offset,
        .payload_len = payload_len,
    });
  }
  _sparse_index_built = true;
  return _decoded_sparse_entries;
}

std::optional<std::span<const uint8_t>> MessageBase::LookupSparseField(int field_id) const {
  const auto& entries = DecodedSparseEntries();
  const auto it = std::lower_bound(
      entries.begin(), entries.end(), field_id,
      [](const SparseEntry& entry, int needle) { return entry.field_id < needle; });
  if (it == entries.end() || it->field_id != field_id) {
    return std::nullopt;
  }
  return std::span<const uint8_t>(SparsePayloadPtr() + it->payload_offset, it->payload_len);
}

std::optional<std::span<uint8_t>> MessageBase::LookupSparseFieldMutable(int field_id) {
  auto field = static_cast<const MessageBase*>(this)->LookupSparseField(field_id);
  if (!field.has_value()) {
    return std::nullopt;
  }
  return std::span<uint8_t>(const_cast<uint8_t*>(field->data()), field->size());
}

std::optional<std::span<const uint8_t>> MessageBase::LookupField(int field_id) const {
  if (auto field = LookupFixedField(field_id); field.has_value()) {
    return field;
  }
  return LookupSparseField(field_id);
}

std::optional<std::span<uint8_t>> MessageBase::LookupMutableField(int field_id) {
  if (auto field = LookupFixedFieldMutable(field_id); field.has_value()) {
    return field;
  }
  return LookupSparseFieldMutable(field_id);
}

std::span<const uint8_t> MessageBase::FieldRaw(int field_id) const {
  auto field = LookupField(field_id);
  return field.value_or(std::span<const uint8_t>{});
}

std::span<uint8_t> MessageBase::FieldRaw(int field_id) {
  auto field = LookupMutableField(field_id);
  return field.value_or(std::span<uint8_t>{});
}

uint16_t MessageBase::FieldCount() const {
  uint32_t count = SparseCount();
  for (FixedStorageWidth width :
       {FixedStorageWidth::Byte1, FixedStorageWidth::Byte4, FixedStorageWidth::Byte8}) {
    for (size_t i = 0; i < FixedWordCount(width); ++i) {
      count += std::popcount(ReadBitmapWord(FixedBitmapPtr(width), i));
    }
  }
  return static_cast<uint16_t>(count);
}

absl::InlinedVector<int, 8> MessageBase::PresentFieldIds() const {
  absl::InlinedVector<int, 8> field_ids;
  field_ids.reserve(FieldCount());

  if (_layout != nullptr) {
    for (const auto& descriptor : _layout->fixed_fields) {
      if (IsFixedBitSet(descriptor)) {
        field_ids.push_back(descriptor.field_id);
      }
    }
  }

  for (const auto& entry : DecodedSparseEntries()) {
    field_ids.push_back(entry.field_id);
  }
  return field_ids;
}

absl::InlinedVector<uint32_t, 2> MessageBase::GetFieldsMask() const {
  using WordType = uint32_t;
  absl::InlinedVector<WordType, 2> mask;
  for (int field_id : PresentFieldIds()) {
    if (field_id <= 0) {
      continue;
    }
    const size_t word_idx = static_cast<size_t>(field_id - 1) / 32;
    if (word_idx >= mask.size()) {
      mask.resize(word_idx + 1, 0);
    }
    mask[word_idx] |= (1u << ((field_id - 1) % 32));
  }
  return mask;
}

}  // namespace gendb
