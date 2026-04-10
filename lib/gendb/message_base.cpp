#include "gendb/message_base.h"

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

}  // namespace

uint8_t MessageBase::FixedWordCount() const { return HeaderPtr()[2]; }

uint16_t MessageBase::SparseCount() const { return ReadScalarRaw<uint16_t>(HeaderPtr() + 4); }

uint16_t MessageBase::FixedPayloadSize() const { return ReadScalarRaw<uint16_t>(HeaderPtr() + 6); }

uint16_t MessageBase::SparseDirSize() const { return ReadScalarRaw<uint16_t>(HeaderPtr() + 8); }

uint32_t MessageBase::TotalSize() const { return ReadScalarRaw<uint32_t>(HeaderPtr() + 10); }

size_t MessageBase::FixedBitmapSize() const { return FixedWordCount() * sizeof(uint32_t); }

const uint8_t* MessageBase::FixedBitmapPtr() const { return HeaderPtr() + internal::kMessageHeaderSize; }

uint8_t* MessageBase::FixedBitmapPtr() { return HeaderPtr() + internal::kMessageHeaderSize; }

const uint8_t* MessageBase::FixedPayloadPtr() const { return FixedBitmapPtr() + FixedBitmapSize(); }

uint8_t* MessageBase::FixedPayloadPtr() { return FixedBitmapPtr() + FixedBitmapSize(); }

const uint8_t* MessageBase::SparseDirPtr() const { return FixedPayloadPtr() + FixedPayloadSize(); }

uint8_t* MessageBase::SparseDirPtr() { return FixedPayloadPtr() + FixedPayloadSize(); }

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

std::optional<std::span<const uint8_t>> MessageBase::LookupFixedField(int field_id) const {
  const FixedFieldDescriptor* field = FindFixedField(field_id);
  if (field == nullptr) {
    return std::nullopt;
  }

  const uint32_t word = field->bit_index / 32;
  const uint32_t bit = field->bit_index % 32;
  if (word >= FixedWordCount() || (ReadBitmapWord(FixedBitmapPtr(), word) & (1u << bit)) == 0) {
    return std::nullopt;
  }

  uint32_t payload_offset = 0;
  for (const auto& descriptor : _layout->fixed_fields) {
    if (descriptor.bit_index == field->bit_index) {
      break;
    }
    const uint32_t descriptor_word = descriptor.bit_index / 32;
    const uint32_t descriptor_bit = descriptor.bit_index % 32;
    if (descriptor_word < FixedWordCount() &&
        (ReadBitmapWord(FixedBitmapPtr(), descriptor_word) & (1u << descriptor_bit)) != 0) {
      payload_offset += descriptor.size;
    }
  }
  return std::span<const uint8_t>(FixedPayloadPtr() + payload_offset, field->size);
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

std::optional<std::span<const uint8_t>> MessageBase::LookupSparseField(int field_id) const {
  size_t left = 0;
  size_t right = SparseCount();
  while (left < right) {
    const size_t mid = left + (right - left) / 2;
    auto entry = DecodeSparseEntry(mid);
    if (!entry.has_value()) {
      return std::nullopt;
    }
    if (entry->field_id == field_id) {
      return std::span<const uint8_t>(SparsePayloadPtr() + entry->payload_offset, entry->payload_len);
    }
    if (entry->field_id < field_id) {
      left = mid + 1;
    } else {
      right = mid;
    }
  }
  return std::nullopt;
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
  for (size_t i = 0; i < FixedWordCount(); ++i) {
    count += std::popcount(ReadBitmapWord(FixedBitmapPtr(), i));
  }
  return static_cast<uint16_t>(count);
}

absl::InlinedVector<int, 8> MessageBase::PresentFieldIds() const {
  absl::InlinedVector<int, 8> field_ids;
  field_ids.reserve(FieldCount());

  if (_layout != nullptr) {
    for (const auto& descriptor : _layout->fixed_fields) {
      const uint32_t word = descriptor.bit_index / 32;
      const uint32_t bit = descriptor.bit_index % 32;
      if (word < FixedWordCount() && (ReadBitmapWord(FixedBitmapPtr(), word) & (1u << bit)) != 0) {
        field_ids.push_back(descriptor.field_id);
      }
    }
  }

  for (size_t i = 0; i < SparseCount(); ++i) {
    auto entry = DecodeSparseEntry(i);
    if (entry.has_value()) {
      field_ids.push_back(entry->field_id);
    }
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
