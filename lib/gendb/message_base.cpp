#include "gendb/message_base.h"

#include <cassert>
#include <cstring>

#include "gendb/bits.h"
#include "gendb/math.h"
#include "gendb/message_builder.h"
namespace gendb {
MessageBase::MessageBase() : _buffer(reinterpret_cast<uint8_t*>(kEmptyMessage.data())) {}

MessageBase::MessageBase(std::span<uint8_t> span) : _buffer(span.data()) {
  assert(span.size() >= 2);
}

std::span<uint8_t> MessageBase::FieldRaw(int field_id) const {
  const uint16_t num_fields = FieldCount();
  if (field_id > num_fields) {
    return {};
  }
  const auto start = ReadScalarRaw<uint16_t>(_buffer + field_id * sizeof(uint16_t));
  const auto end = ReadScalarRaw<uint16_t>(_buffer + (field_id + 1) * sizeof(uint16_t));
  return {_buffer + start, _buffer + end};
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
  if (!IsBitmaskSubset(all_fixed_size_fields, patch.modified)) return false;
  absl::InlinedVector<uint32_t, 2> current = GetFieldsMask();
  if (!IsBitmaskSubset(current, patch.modified)) return false;
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
}  // namespace gendb
