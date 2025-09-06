#include "gendb/message_base.h"

#include <cassert>
#include <cstring>
#include <span>

#include "gendb/bits.h"
#include "gendb/math.h"
#include "gendb/message_builder.h"
namespace gendb {
MessageBase::MessageBase() : _buffer(reinterpret_cast<uint8_t*>(kEmptyMessage.data())) {}

MessageBase::MessageBase(std::span<uint8_t> buffer) : _buffer(buffer.data()) {
  assert(buffer.size() >= 2);
}
MessageBase::MessageBase(std::span<const uint8_t> buffer)
    : MessageBase(std::span<uint8_t>(const_cast<uint8_t*>(buffer.data()), buffer.size())) {}

MessageBase::MessageBase(std::vector<uint8_t>& buffer)
    : MessageBase(std::span<uint8_t>(buffer.data(), buffer.size())) {}

std::span<const uint8_t> MessageBase::FieldRaw(int field_id) const {
  const uint16_t num_fields = FieldCount();
  if (field_id > num_fields) {
    return {};
  }
  const auto start = ReadScalarRaw<uint16_t>(_buffer + field_id * sizeof(uint16_t));
  const auto end = ReadScalarRaw<uint16_t>(_buffer + (field_id + 1) * sizeof(uint16_t));
  return {_buffer + start, _buffer + end};
}

std::span<uint8_t> MessageBase::FieldRaw(int field_id) {
  std::span<const uint8_t> const_raw = static_cast<const MessageBase*>(this)->FieldRaw(field_id);
  return {const_cast<uint8_t*>(const_raw.data()), const_raw.size()};
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

}  // namespace gendb
