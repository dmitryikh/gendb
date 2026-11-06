#pragma once

#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <string_view>

#include "absl/crc/crc32c.h"

// The on-disk and on-wire format shared by the WAL, checkpoints and replication. Everything is a
// sequence of self-checking frames, so a commit can be moved between them byte for byte:
//
//   frame:   [crc32c: u32] [body size + 1: u32] [type: u8] [body]
//   commit:  [lsn: u64] [entry count: u32] entry*
//   entry:   [collection id: u32] [op: u8] [key size: varint] [value size: varint] key value
//
// The value of a kPatch entry is [crc32c of the resulting record: u32]
// [MessagePatch::AppendEncoded].
//
// The checksum covers everything after itself. Integers are little-endian. docs/storage_engine.md
// describes the layout of each file.
namespace gendb::internal {

enum class FrameType : uint8_t {
  kWalHeader = 1,
  kCommit = 2,
  kCheckpointHeader = 3,
  kCollection = 4,
  kRecords = 5,
  kCheckpointFooter = 6,
  kSyncMark = 7,
};

inline constexpr size_t kFrameHeaderSize = 8;
// What this code writes, and the range it reads. Version 3 changed how strings encode in keys, so
// older files cannot be read.
inline constexpr uint32_t kFormatVersion = 3;
inline constexpr uint32_t kOldestFormatVersion = 3;

// Identifies one linear history of commits: the same ID and LSN mean the same database state.
struct HistoryId {
  uint64_t high = 0;
  uint64_t low = 0;
  bool operator==(const HistoryId&) const = default;

  static HistoryId Random() {
    std::random_device device;
    const auto word = [&device] { return uint64_t{device()} << 32 | device(); };
    return {word(), word()};
  }
};

inline void PutFixed32(std::string& out, uint32_t value) {
  out.append(reinterpret_cast<const char*>(&value), sizeof(value));
}
inline void PutFixed64(std::string& out, uint64_t value) {
  out.append(reinterpret_cast<const char*>(&value), sizeof(value));
}
inline void PutVarint(std::string& out, uint64_t value) {
  while (value >= 0x80) {
    out.push_back(static_cast<char>(value | 0x80));
    value >>= 7;
  }
  out.push_back(static_cast<char>(value));
}
inline void PutHistory(std::string& out, const HistoryId& history) {
  PutFixed64(out, history.high);
  PutFixed64(out, history.low);
}
inline void PutString(std::string& out, std::string_view value) {
  PutVarint(out, value.size());
  out.append(value);
}

// Bounds-checked reads. After a failed read the decoder stays failed.
class Decoder {
 public:
  explicit Decoder(std::string_view in) : _in(in) {}

  bool ok() const noexcept { return _ok; }
  std::string_view rest() const noexcept { return _in; }
  bool empty() const noexcept { return _in.empty(); }

  uint8_t Byte() noexcept { return ReadFixed<uint8_t>(); }
  uint32_t Fixed32() noexcept { return ReadFixed<uint32_t>(); }
  uint64_t Fixed64() noexcept { return ReadFixed<uint64_t>(); }
  uint64_t Varint() noexcept {
    uint64_t value = 0;
    for (int shift = 0; shift < 64 && !_in.empty(); shift += 7) {
      const auto byte = static_cast<uint8_t>(_in.front());
      _in.remove_prefix(1);
      value |= uint64_t{byte & 0x7fu} << shift;
      if (byte < 0x80) return value;
    }
    _ok = false;
    return 0;
  }
  std::string_view Bytes(uint64_t size) noexcept {
    if (size > _in.size()) {
      _ok = false;
      _in = {};
      return {};
    }
    const std::string_view out = _in.substr(0, size);
    _in.remove_prefix(size);
    return out;
  }
  std::string_view String() noexcept { return Bytes(Varint()); }
  HistoryId History() noexcept {
    HistoryId history;
    history.high = Fixed64();
    history.low = Fixed64();
    return history;
  }

 private:
  template <typename T>
  T ReadFixed() noexcept {
    T value{};
    if (_in.size() < sizeof(T)) {
      _ok = false;
      _in = {};
      return value;
    }
    std::memcpy(&value, _in.data(), sizeof(T));
    _in.remove_prefix(sizeof(T));
    return value;
  }

  std::string_view _in;
  bool _ok = true;
};

// Starts a frame at the end of `out`. Returns the value EndFrame needs.
inline size_t BeginFrame(std::string& out, FrameType type) {
  const size_t begin = out.size();
  out.append(kFrameHeaderSize, '\0');
  out.push_back(static_cast<char>(type));
  return begin;
}

inline void EndFrame(std::string& out, size_t begin) {
  const auto size = static_cast<uint32_t>(out.size() - begin - kFrameHeaderSize);
  std::memcpy(out.data() + begin + 4, &size, 4);
  const std::string_view covered(out.data() + begin + 4, size + 4);
  const auto crc = static_cast<uint32_t>(absl::ComputeCrc32c(covered));
  std::memcpy(out.data() + begin, &crc, 4);
}

enum class ParseStatus { kOk, kIncomplete, kCorrupt };

struct Frame {
  FrameType type;
  std::string_view body;
  std::string_view raw;  // The whole frame, header included.
};

// Parses the frame at the start of `in`, which may hold more after it. An all-zero tail, as a
// filesystem can leave after a crash, reads as corrupt.
inline ParseStatus ParseFrame(std::string_view in, Frame* frame) noexcept {
  if (in.size() < kFrameHeaderSize) return ParseStatus::kIncomplete;
  uint32_t crc, size;
  std::memcpy(&crc, in.data(), 4);
  std::memcpy(&size, in.data() + 4, 4);
  if (in.size() - kFrameHeaderSize < size) return ParseStatus::kIncomplete;
  const std::string_view covered(in.data() + 4, size_t{size} + 4);
  if (size == 0 || crc != static_cast<uint32_t>(absl::ComputeCrc32c(covered))) {
    return ParseStatus::kCorrupt;
  }
  frame->type = static_cast<FrameType>(in[kFrameHeaderSize]);
  frame->body = in.substr(kFrameHeaderSize + 1, size - 1);
  frame->raw = in.substr(0, kFrameHeaderSize + size);
  return ParseStatus::kOk;
}

// The size of the whole frame at the start of `in`, or 0 if it has not fully arrived. Does not
// check it.
inline size_t CompleteFrameSize(std::string_view in) noexcept {
  if (in.size() < kFrameHeaderSize) return 0;
  uint32_t size;
  std::memcpy(&size, in.data() + 4, 4);
  const size_t total = kFrameHeaderSize + size_t{size};
  return in.size() >= total ? total : 0;
}

// A collection's stable identity in frames: reordering a schema does not change it.
inline uint32_t CollectionIdOf(std::string_view name) {
  return static_cast<uint32_t>(absl::ComputeCrc32c(name));
}

inline constexpr size_t kCommitCountOffset = kFrameHeaderSize + 1 + sizeof(uint64_t);

inline size_t BeginCommit(std::string& out, uint64_t lsn) {
  const size_t begin = BeginFrame(out, FrameType::kCommit);
  PutFixed64(out, lsn);
  PutFixed32(out, 0);
  return begin;
}

enum class CommitOp : uint8_t { kPut = 0, kDelete = 1, kPatch = 2 };

inline void AddCommitEntry(std::string& out, uint32_t collection_id, CommitOp op,
                           std::string_view key, std::string_view value) {
  PutFixed32(out, collection_id);
  out.push_back(static_cast<char>(op));
  PutVarint(out, key.size());
  PutVarint(out, value.size());
  out.append(key);
  out.append(value);
}

inline constexpr size_t kPatchEntryOverhead = sizeof(uint32_t);

inline void AddCommitPatch(std::string& out, uint32_t collection_id, std::string_view key,
                           uint32_t result_crc, std::string_view patch) {
  PutFixed32(out, collection_id);
  out.push_back(static_cast<char>(CommitOp::kPatch));
  PutVarint(out, key.size());
  PutVarint(out, kPatchEntryOverhead + patch.size());
  out.append(key);
  PutFixed32(out, result_crc);
  out.append(patch);
}

inline void EndCommit(std::string& out, size_t begin, uint32_t count) {
  std::memcpy(out.data() + begin + kCommitCountOffset, &count, sizeof(count));
  EndFrame(out, begin);
}

struct CommitEntry {
  uint32_t collection_id;
  CommitOp op;
  std::string_view key;
  std::string_view value;
};

class CommitDecoder {
 public:
  explicit CommitDecoder(std::string_view body) : _in(body) {
    lsn = _in.Fixed64();
    count = _in.Fixed32();
  }

  uint64_t lsn;
  uint32_t count;

  bool ok() const noexcept { return _in.ok(); }
  // The caller reads exactly `count` entries, and checks ok() after each.
  CommitEntry Next() noexcept {
    CommitEntry entry;
    entry.collection_id = _in.Fixed32();
    entry.op = static_cast<CommitOp>(_in.Byte());
    const uint64_t key_size = _in.Varint();
    const uint64_t value_size = _in.Varint();
    entry.key = _in.Bytes(key_size);
    entry.value = _in.Bytes(value_size);
    return entry;
  }

 private:
  Decoder _in;
};

}  // namespace gendb::internal
