#include "gendb/message_patch.h"

#include "gendb/bits.h"
#include "gendb/message_builder.h"

namespace gendb {
bool ApplyPatchInplace(const MessagePatch& patch, std::span<uint8_t> message,
                       const MessageLayout* layout) {
  MessageBase patch_message{patch.buffer, patch.layout};
  MessageBase dst{message, layout != nullptr ? layout : patch.layout};
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

void ApplyPatch(const MessagePatch& patch, std::span<const uint8_t> src, std::vector<uint8_t>& dst,
                const MessageLayout* layout) {
  const MessageLayout* effective_layout = layout != nullptr ? layout : patch.layout;
  MessageBase src_message{src, effective_layout};
  MessageBuilder builder(src_message);
  MessageBase patch_message{patch.buffer, patch.layout};
  ForEachSetField(patch.removed, [&](int field_id) { builder.ClearField(field_id); });
  ForEachSetField(patch.modified, [&](int field_id) {
    auto src_field = patch_message.FieldRaw(field_id);
    if (!patch_message.HasField(field_id)) {
      return;
    }
    builder.AddFieldRaw(field_id, src_field);
  });
  dst = builder.Build();
}
}  // namespace gendb
