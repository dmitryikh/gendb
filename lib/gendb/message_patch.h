#pragma once
#include <atomic>

#include "absl/container/inlined_vector.h"

namespace gendb {
struct MessagePatch {
  absl::InlinedVector<uint32_t, 2> modified;
  absl::InlinedVector<uint32_t, 2> removed;

  // Buffer for storing the message.
  // Message must have data for all modified fields.
  std::vector<uint8_t> buffer;
};

}  // namespace gendb