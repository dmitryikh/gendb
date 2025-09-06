#pragma once
#include <atomic>

#include "absl/container/inlined_vector.h"
#include "gendb/bits.h"
#include "gendb/message_base.h"

namespace gendb {
struct MessagePatch {
  absl::InlinedVector<uint32_t, 2> modified;
  absl::InlinedVector<uint32_t, 2> removed;

  // Buffer for storing the message.
  // Message must have data for all modified fields.
  std::vector<uint8_t> buffer;
};

bool ApplyPatchInplace(const MessagePatch& patch, std::span<uint8_t> message);
void ApplyPatch(const MessagePatch& patch, std::span<const uint8_t> src, std::vector<uint8_t>& dst);

template <typename T>
void ApplyPatch(const MessagePatch& patch, std::vector<uint8_t>& message) {
  if (CanApplyPatchInplace<T>(patch, message)) {
    ApplyPatchInplace(patch, message);
  } else {
    std::vector<uint8_t> buffer;
    ApplyPatch(patch, message, buffer);
    message = std::move(buffer);
  }
}

template <typename T>
bool CanApplyPatchInplace(const MessagePatch& patch, std::span<const uint8_t> message) {
  if (!IsBitmaskSubset(T::kFixedSizeFields, patch.modified)) return false;
  absl::InlinedVector<uint32_t, 2> current = T{message}.GetFieldsMask();
  if (!IsBitmaskSubset(current, patch.modified)) return false;
  if (!IsBitmaskSubsetInverted(current, patch.removed)) return false;
  return true;
}

}  // namespace gendb