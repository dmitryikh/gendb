#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string_view>

#include "absl/crc/crc32c.h"
#include "absl/hash/hash.h"
#include "gendb/bytes.h"
#include "gendb/message.h"
#include "gendb/record_table.h"

// What the parts of the engine share, and nobody else needs.
namespace gendb::internal {

inline constexpr std::string_view kCheckpointPrefix = "checkpoint-";

inline constexpr std::string_view kCheckpointSuffix = ".gdb";

inline constexpr std::string_view kTempSuffix = ".tmp";

inline uint64_t HashKey(std::string_view key) noexcept {
  return absl::HashOf(key);
}

inline BytesConstView AsBytes(std::string_view s) noexcept {
  return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

inline bool SameBytes(BytesConstView a, std::string_view b) noexcept {
  return a.size() == b.size() && (b.empty() || std::memcmp(a.data(), b.data(), b.size()) == 0);
}

inline uint32_t CrcOf(BytesConstView bytes) noexcept {
  return static_cast<uint32_t>(absl::ComputeCrc32c(AsStringView(bytes)));
}

// What a patch does to a record: on the writer, in recovery and on replicas alike, which must
// produce the same bytes, since the log holds only the patch. Null if the record is malformed.
inline Version* PatchedVersion(BytesConstView current, const MessagePatch& patch) {
  const auto view = MessageView::Open(current);
  if (!view.has_value()) return nullptr;
  if (CanApplyPatchInplace(patch, *view)) {
    Version* next = Version::Copy(current);
    ApplyPatchInplace(patch, std::span<uint8_t>(next->data(), next->size));
    return next;
  }
  try {
    return Version::Copy(Rebuild(*view, patch));
  } catch (const std::invalid_argument&) {
    return nullptr;
  }
}

}  // namespace gendb::internal
