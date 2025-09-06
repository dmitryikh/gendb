#include "gendb/message_patch.h"

#include "gendb/bits.h"
#include "gendb/message_builder.h"

namespace gendb {
bool ApplyPatchInplace(const MessagePatch& patch, std::span<uint8_t> message) {
  MessageBase patch_message{patch.buffer};
  MessageBase dst{message};
  bool res = true;
  ForEachSetField(patch.modified, [&](int field_id) {
    auto dst_field = dst.FieldRaw(field_id);
    auto src_field = patch_message.FieldRaw(field_id);
    if (dst_field.size() != src_field.size()) {
      res = false;
      return;
    }
    memcpy(dst_field.data(), src_field.data(), src_field.size());
  });
  return res;
}

void ApplyPatch(const MessagePatch& patch, std::span<const uint8_t> src,
                std::vector<uint8_t>& dst) {
  MessageBuilder builder(src);
  MessageBase patch_message{patch.buffer};
  ForEachSetField(patch.removed, [&](int field_id) { builder.ClearField(field_id); });
  ForEachSetField(patch.modified, [&](int field_id) {
    auto src_field = patch_message.FieldRaw(field_id);
    if (src_field.empty()) {
      return;
    }
    builder.AddFieldRaw(field_id, src_field);
  });
  dst = builder.Build();
}
}  // namespace gendb