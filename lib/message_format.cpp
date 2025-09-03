#include "message_format.h"

#include <absl/container/inlined_vector.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include <span>

#include "bits.h"
#include "math.h"

namespace gendb {

MessageBase::MessageBase() : _buffer(reinterpret_cast<uint8_t*>(kEmptyMessage.data())) {}

MessageBase::MessageBase(std::span<uint8_t> span) : _buffer(span.data()) {
  assert(span.size() >= 2);
  //   uint16_t vtable_offset = ReadScalarRaw<uint16_t>(_buffer);
  //   _vtable = _buffer + vtable_offset;
  // #ifndef NDEBUG
  //   const uint16_t vtable_size = ReadScalarRaw<uint16_t>(_vtable);
  //   assert(vtable_offset + (vtable_size + 2) * sizeof(uint16_t) <= span.size());
  // #endif
}

std::span<uint8_t> MessageBase::FieldRaw(int field_id) const {
  const uint16_t num_fields = FieldCount();
  if (field_id > num_fields) {
    return std::span<uint8_t>();
  }
  // Field_id is 1-based, vtable[0] is num_fields
  const uint16_t start = ReadScalarRaw<uint16_t>(_buffer + field_id * sizeof(uint16_t));
  const uint16_t end = ReadScalarRaw<uint16_t>(_buffer + (field_id + 1) * sizeof(uint16_t));
  return std::span<uint8_t>(_buffer + start, _buffer + end);
}

absl::InlinedVector<uint32_t, 2> MessageBase::GetFieldsMask() const {
  absl::InlinedVector<uint32_t, 2> mask;
  int n_fields = FieldCount();
  int n_words = DivRoundUp(n_fields, 32);
  mask.resize(n_words, 0);
  for (int i = 0; i < n_fields; ++i) {
    if (HasField(i + 1)) {
      mask[i / 32] |= (1u << (i % 32));
    }
  }
  return mask;
}

bool MessageBase::CanApplyPatchInplace(const MessagePatch& patch,
                                       std::span<const uint32_t> all_fixed_size_fields) const {
  // 1. Modified fields must be subset of all fixed size fields.
  if (!IsBitmaskSubset(all_fixed_size_fields, patch.modified)) return false;

  // Get currently set fields from the message.
  absl::InlinedVector<uint32_t, 2> current = GetFieldsMask();
  // 2. Modified fields must be subset of current fields.
  if (!IsBitmaskSubset(current, patch.modified)) return false;

  // 3. Removed fields must be subset of inverted current fields.
  if (!IsBitmaskSubsetInverted(current, patch.removed)) return false;
  return true;
}

bool MessageBase::ApplyPatchInplace(const MessagePatch& patch) {
  MessageBase patch_message{patch.buffer};
  bool res = true;
  ForEachSetField(patch.modified, [&](int field_id) {
    auto dst_field = FieldRaw(field_id);
    auto src_field = patch_message.FieldRaw(field_id);
    if (dst_field.size() != src_field.size()) {
      res = false;
      return;
    }
    memcpy(dst_field.data(), src_field.data(), src_field.size());
  });
  return res;
}

void MessageBase::ApplyPatch(const MessagePatch& patch, std::vector<uint8_t>& buffer) const {
  MessageBuilder builder(*this);
  MessageBase patch_message{patch.buffer};
  ForEachSetField(patch.removed, [&](int field_id) { builder.ClearField(field_id); });
  ForEachSetField(patch.modified, [&](int field_id) {
    auto src_field = patch_message.FieldRaw(field_id);
    if (src_field.empty()) {
      return;
    }
    builder.AddFieldRaw(field_id, src_field);
  });
  buffer = builder.Build();
}

MessageBuilder::MessageBuilder() {}

MessageBuilder::MessageBuilder(const MessageBase& src) {
  MergeFields(src);
}

void MessageBuilder::AddFieldRaw(int field_id, std::span<const uint8_t> data) {
  _fields[field_id] = std::string(data.begin(), data.end());
}

void MessageBuilder::ClearField(int field_id) {
  _fields.erase(field_id);
}

// Returns empty vector in case of failures.
std::vector<uint8_t> MessageBuilder::Build() {
  std::vector<uint8_t> message;
  int max_field_id = 0;
  int total_length = 0;
  for (const auto& [field_id, field_raw] : _fields) {
    total_length += field_raw.size();
    max_field_id = std::max(static_cast<int>(field_id), max_field_id);
  }
  const int vtable_length =
      /*num_fields*/ sizeof(uint16_t) + /*vtable*/ (max_field_id + 1) * sizeof(uint16_t);
  total_length += vtable_length;
  if (total_length > std::numeric_limits<uint16_t>::max()) {
    // Handle error: message too large
    return message;
  }
  if (max_field_id > std::numeric_limits<uint16_t>::max()) {
    // Handle error: message too large
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
}  // namespace gendb