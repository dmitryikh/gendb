#include "gendb/message_builder.h"

#include <algorithm>
#include <cstring>
#include <limits>
namespace gendb {
namespace {

void SetBitmapBit(Bytes& bitmap, uint16_t bit_index) {
  const size_t word_index = bit_index / 32;
  uint32_t word = ReadScalarRaw<uint32_t>(bitmap.data() + word_index * sizeof(uint32_t));
  word |= (1u << (bit_index % 32));
  WriteScalarRaw<uint32_t>(bitmap.data() + word_index * sizeof(uint32_t), word);
}

size_t WidthIndex(FixedStorageWidth width) { return FixedStorageClassIndex(width); }

}  // namespace

MessageBuilder::MessageBuilder(const MessageLayout* layout) : _layout(layout) {}
MessageBuilder::MessageBuilder(const MessageBase& src) : _layout(src.Layout()) {
  MergeFields(src);
}

const FixedFieldDescriptor* MessageBuilder::FindFixedField(int field_id) const {
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

void MessageBuilder::AddFieldRaw(int field_id, std::span<const uint8_t> data) {
  _fields[field_id] = std::string(data.begin(), data.end());
}
void MessageBuilder::ClearField(int field_id) {
  _fields.erase(field_id);
}
std::vector<uint8_t> MessageBuilder::Build() {
  std::array<Bytes, kFixedStorageClassCount> fixed_bitmaps;
  std::array<Bytes, kFixedStorageClassCount> fixed_payloads;
  Bytes sparse_dir;
  Bytes sparse_payload;

  if (_layout != nullptr) {
    for (size_t i = 0; i < kFixedStorageClassCount; ++i) {
      fixed_bitmaps[i].resize(_layout->fixed_word_counts[i] * sizeof(uint32_t), 0);
    }
  }

  if (_layout != nullptr) {
    for (const auto& descriptor : _layout->fixed_fields) {
      auto it = _fields.find(descriptor.field_id);
      if (it == _fields.end()) {
        continue;
      }
      const size_t stored_width = static_cast<size_t>(descriptor.storage_width);
      if (it->second.size() != descriptor.logical_size && it->second.size() != stored_width) {
        continue;
      }
      const size_t class_index = WidthIndex(descriptor.storage_width);
      SetBitmapBit(fixed_bitmaps[class_index], descriptor.bit_index_within_class);
      const size_t payload_offset = fixed_payloads[class_index].size();
      fixed_payloads[class_index].resize(payload_offset + stored_width, 0);
      memcpy(fixed_payloads[class_index].data() + payload_offset, it->second.data(),
             std::min(it->second.size(), stored_width));
    }
  }

  uint32_t prev_field_id = 0;
  uint32_t prev_payload_offset = 0;
  uint16_t sparse_count = 0;
  for (const auto& [field_id, field_raw] : _fields) {
    const FixedFieldDescriptor* descriptor = FindFixedField(field_id);
    if (descriptor != nullptr &&
        (field_raw.size() == descriptor->logical_size ||
         field_raw.size() == static_cast<size_t>(descriptor->storage_width))) {
      continue;
    }
    const uint32_t payload_offset = sparse_payload.size();
    sparse_payload.insert(sparse_payload.end(), field_raw.begin(), field_raw.end());
    internal::AppendVarint(sparse_dir, static_cast<uint32_t>(field_id) - prev_field_id);
    internal::AppendVarint(sparse_dir, payload_offset - prev_payload_offset);
    internal::AppendVarint(sparse_dir, field_raw.size());
    prev_field_id = field_id;
    prev_payload_offset = payload_offset;
    ++sparse_count;
  }

  const size_t total_length = internal::kMessageHeaderSize + fixed_bitmaps[0].size() +
                              fixed_bitmaps[1].size() + fixed_bitmaps[2].size() +
                              fixed_payloads[0].size() + fixed_payloads[1].size() +
                              fixed_payloads[2].size() + sparse_dir.size() + sparse_payload.size();
  if (sparse_count > std::numeric_limits<uint16_t>::max() ||
      fixed_payloads[0].size() > std::numeric_limits<uint16_t>::max() ||
      fixed_payloads[1].size() > std::numeric_limits<uint16_t>::max() ||
      fixed_payloads[2].size() > std::numeric_limits<uint16_t>::max() ||
      sparse_dir.size() > std::numeric_limits<uint16_t>::max() ||
      total_length > std::numeric_limits<uint32_t>::max()) {
    return {};
  }
  std::vector<uint8_t> message(total_length);
  if (message.empty()) {
    return message;
  }

  uint8_t* cursor = message.data();
  cursor[0] = internal::kMessageFormatVersion;
  cursor[1] = 0;
  cursor[2] = _layout == nullptr ? 0 : _layout->fixed_word_counts[0];
  cursor[3] = _layout == nullptr ? 0 : _layout->fixed_word_counts[1];
  cursor[4] = _layout == nullptr ? 0 : _layout->fixed_word_counts[2];
  cursor[5] = 0;
  WriteScalarRaw<uint16_t>(cursor + 6, sparse_count);
  WriteScalarRaw<uint16_t>(cursor + 8, static_cast<uint16_t>(fixed_payloads[0].size()));
  WriteScalarRaw<uint16_t>(cursor + 10, static_cast<uint16_t>(fixed_payloads[1].size()));
  WriteScalarRaw<uint16_t>(cursor + 12, static_cast<uint16_t>(fixed_payloads[2].size()));
  WriteScalarRaw<uint16_t>(cursor + 14, static_cast<uint16_t>(sparse_dir.size()));
  WriteScalarRaw<uint32_t>(cursor + 16, static_cast<uint32_t>(total_length));

  cursor += internal::kMessageHeaderSize;
  for (const auto& fixed_bitmap : fixed_bitmaps) {
    if (!fixed_bitmap.empty()) {
      memcpy(cursor, fixed_bitmap.data(), fixed_bitmap.size());
      cursor += fixed_bitmap.size();
    }
  }
  for (const auto& fixed_payload : fixed_payloads) {
    if (!fixed_payload.empty()) {
      memcpy(cursor, fixed_payload.data(), fixed_payload.size());
      cursor += fixed_payload.size();
    }
  }
  if (!sparse_dir.empty()) {
    memcpy(cursor, sparse_dir.data(), sparse_dir.size());
    cursor += sparse_dir.size();
  }
  if (!sparse_payload.empty()) {
    memcpy(cursor, sparse_payload.data(), sparse_payload.size());
  }
  return message;
}
void MessageBuilder::MergeFields(const MessageBase& rhs) {
  for (int field_id : rhs.PresentFieldIds()) {
    std::span<const uint8_t> field = rhs.FieldRaw(field_id);
    AddFieldRaw(field_id, field);
  }
}

std::map<int, std::span<const uint8_t>> MessageBuilder::GetFieldData() const {
  std::map<int, std::span<const uint8_t>> field_data;
  for (const auto& [field_id, field_raw] : _fields) {
    field_data[field_id] = std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(field_raw.data()), field_raw.size());
  }
  return field_data;
}

size_t MessageBuilder::OwnedBytesEstimate() const {
  size_t bytes = sizeof(*this);
  bytes += sizeof(std::pair<const int, std::string>) * _fields.size();
  for (const auto& [field_id, field_raw] : _fields) {
    static_cast<void>(field_id);
    bytes += field_raw.capacity();
  }
  return bytes;
}
}  // namespace gendb
