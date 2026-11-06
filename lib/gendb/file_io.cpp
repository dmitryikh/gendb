#include "gendb/file_io.h"

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

size_t FrameFileReader::Fill() {
  _buffer.erase(0, _begin);
  _begin = 0;
  const size_t have = _buffer.size();
  _buffer.resize(have + kReadBlock);
  auto got = _file->Read(_read_at, _buffer.data() + have, kReadBlock);
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
    const ParseStatus status = ParseFrame(std::string_view(_buffer).substr(_begin), frame);
    if (status == ParseStatus::kOk) _current_size = frame->raw.size();
    if (status != ParseStatus::kIncomplete || Fill() == 0) return status;
  }
}

bool FrameFileReader::AtZeros() const noexcept {
  const std::string_view rest = std::string_view(_buffer).substr(_begin);
  return rest.size() >= kFrameHeaderSize &&
         rest.substr(0, kFrameHeaderSize).find_first_not_of('\0') == std::string_view::npos;
}

void FrameFileReader::Reread() noexcept {
  _buffer.clear();
  _begin = 0;
  _current_size = 0;
  _read_at = _offset;
}

void FrameFileReader::Advance() {
  _begin += _current_size;
  _offset += _current_size;
  _current_size = 0;
}

}  // namespace gendb::internal
