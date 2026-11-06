#include "gendb/file_io.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

#include "absl/strings/str_cat.h"

namespace gendb::internal {
namespace {

constexpr size_t kReadBlock = 1 << 20;

// fsync on macOS does not reach the platter; on Linux, fdatasync skips what recovery does not need.
int SyncFd(int fd) {
#if defined(__APPLE__)
  return fcntl(fd, F_FULLFSYNC);
#else
  return fdatasync(fd);
#endif
}

}  // namespace

absl::Status ErrnoStatus(std::string_view what, std::string_view path) {
  return absl::ErrnoToStatus(errno, absl::StrCat(what, " ", path));
}

absl::StatusOr<std::unique_ptr<AppendFile>> AppendFile::Create(std::string path) {
  const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_APPEND | O_CLOEXEC, 0644);
  if (fd < 0) return ErrnoStatus("create", path);
  return std::unique_ptr<AppendFile>(new AppendFile(std::move(path), fd));
}

AppendFile::~AppendFile() {
  close(_fd);
}

absl::Status AppendFile::Append(std::string_view data) {
  while (!data.empty()) {
    const ssize_t written = write(_fd, data.data(), data.size());
    if (written < 0) {
      if (errno == EINTR) continue;
      return ErrnoStatus("write", _path);
    }
    data.remove_prefix(static_cast<size_t>(written));
    _size.fetch_add(static_cast<uint64_t>(written), std::memory_order_relaxed);
  }
  return absl::OkStatus();
}

absl::Status AppendFile::Sync() {
  while (SyncFd(_fd) != 0) {
    if (errno != EINTR) return ErrnoStatus("sync", _path);
  }
  return absl::OkStatus();
}

absl::Status SyncDirectory(const std::string& path) {
  const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) return ErrnoStatus("open directory", path);
  const int result = fsync(fd);
  const int saved_errno = errno;
  close(fd);
  errno = saved_errno;
  return result == 0 ? absl::OkStatus() : ErrnoStatus("sync directory", path);
}

absl::Status TruncateAndSync(const std::string& path, uint64_t size) {
  const int fd = open(path.c_str(), O_WRONLY | O_CLOEXEC);
  if (fd < 0) return ErrnoStatus("open", path);
  absl::Status status;
  if (ftruncate(fd, static_cast<off_t>(size)) != 0 || fsync(fd) != 0) {
    status = ErrnoStatus("truncate", path);
  }
  close(fd);
  return status;
}

absl::StatusOr<std::unique_ptr<FrameFileReader>> FrameFileReader::Open(const std::string& path) {
  const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) return ErrnoStatus("open", path);
  return std::unique_ptr<FrameFileReader>(new FrameFileReader(fd));
}

FrameFileReader::~FrameFileReader() {
  close(_fd);
}

size_t FrameFileReader::Fill(size_t at_least) {
  _buffer.erase(0, _begin);
  _begin = 0;
  const size_t have = _buffer.size();
  const size_t want = std::max(at_least, kReadBlock);
  _buffer.resize(have + want);
  size_t total = 0;
  while (total < want) {
    const ssize_t got = pread(_fd, _buffer.data() + have + total, want - total,
                              static_cast<off_t>(_read_at + total));
    if (got < 0 && errno == EINTR) continue;
    if (got <= 0) break;  // A read error looks like the end of the data, which recovery checks.
    total += static_cast<size_t>(got);
  }
  _buffer.resize(have + total);
  _read_at += total;
  return total;
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
