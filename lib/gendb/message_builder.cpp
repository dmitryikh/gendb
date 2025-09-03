#include "gendb/message_builder.h"

#include <algorithm>
#include <cstring>
namespace gendb {
MessageBuilder::MessageBuilder() = default;
MessageBuilder::MessageBuilder(const MessageBase& src) {
  MergeFields(src);
}
void MessageBuilder::AddFieldRaw(int field_id, std::span<const uint8_t> data) {
  _fields[field_id] = std::string(data.begin(), data.end());
}
void MessageBuilder::ClearField(int field_id) {
  _fields.erase(field_id);
}
std::vector<uint8_t> MessageBuilder::Build() {
  std::vector<uint8_t> message;
  int max_field_id = 0;
  size_t total_length = 0;
  for (const auto& [field_id, field_raw] : _fields) {
    total_length += field_raw.size();
    max_field_id = std::max(static_cast<int>(field_id), max_field_id);
  }
  const size_t vtable_length = sizeof(uint16_t) + (max_field_id + 1) * sizeof(uint16_t);
  total_length += vtable_length;
  if (total_length > std::numeric_limits<uint16_t>::max()) {
    return message;
  }
  if (max_field_id > std::numeric_limits<uint16_t>::max()) {
    return message;
  }
  message.resize(total_length);
  uint8_t* offset_cursor = message.data() + vtable_length;
  WriteScalarRaw<uint16_t>(message.data(), max_field_id);
  uint8_t* vtable_cursor = message.data() + sizeof(uint16_t);
  for (int field_id = 1; field_id <= max_field_id; field_id++) {
    WriteScalarRaw<uint16_t>(vtable_cursor, offset_cursor - message.data());
    vtable_cursor += sizeof(uint16_t);
    auto it = _fields.find(field_id);
    if (it == _fields.end()) {
      continue;
    }
    memcpy(offset_cursor, it->second.data(), it->second.size());
    offset_cursor += it->second.size();
  }
  WriteScalarRaw<uint16_t>(vtable_cursor, total_length);
  return message;
}
void MessageBuilder::MergeFields(const MessageBase& rhs) {
  for (int i = 1; i <= rhs.FieldCount(); i++) {
    std::span<const uint8_t> field = rhs.FieldRaw(i);
    if (!field.empty()) {
      AddFieldRaw(i, field);
    }
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
}  // namespace gendb
