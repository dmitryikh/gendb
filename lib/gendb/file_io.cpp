#include "gendb/file_io.h"

#include <algorithm>
#include <cstring>

namespace gendb::internal {
namespace {

constexpr size_t kReadBlock = 1 << 20;

}  // namespace

absl::StatusOr<std::unique_ptr<FrameFileReader>> FrameFileReader::Open(FileSystem& file_system,
                                                                       const std::string& path) {
  auto file = file_system.OpenForRead(path);
  if (!file.ok()) return file.status();
  return std::unique_ptr<FrameFileReader>(new FrameFileReader(std::move(*file)));
}

size_t FrameFileReader::Fill(size_t at_least) {
  _buffer.erase(0, _begin);
  _begin = 0;
  const size_t have = _buffer.size();
  const size_t want = std::max(at_least, kReadBlock);
  _buffer.resize(have + want);
  auto got = _file->Read(_read_at, _buffer.data() + have, want);
  if (!got.ok()) {
    _status = got.status();
    _buffer.resize(have);
    return 0;
  }
  _buffer.resize(have + *got);
  _read_at += *got;
  return *got;
}

ParseStatus FrameFileReader::Peek(Frame* frame) {
  for (;;) {
    const std::string_view available = std::string_view(_buffer).substr(_begin);
    const ParseStatus status = ParseFrame(available, frame);
    if (status == ParseStatus::kOk) _current_size = frame->raw.size();
    if (status != ParseStatus::kIncomplete) return status;
    uint32_t size = 0;
    if (available.size() >= kFrameHeaderSize) std::memcpy(&size, available.data() + 4, 4);
    const size_t needed = kFrameHeaderSize + size;
    if (Fill(needed > available.size() ? needed - available.size() : 0) == 0) return status;
  }
}

void FrameFileReader::Advance() {
  _begin += _current_size;
  _offset += _current_size;
  _current_size = 0;
}

}  // namespace gendb::internal
