#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gendb/log_format.h"

namespace gendb::internal {

absl::Status ErrnoStatus(std::string_view what, std::string_view path);

// An append-only file.
class AppendFile {
 public:
  // Creates the file, replacing any existing one.
  static absl::StatusOr<std::unique_ptr<AppendFile>> Create(std::string path);
  ~AppendFile();
  AppendFile(const AppendFile&) = delete;
  AppendFile& operator=(const AppendFile&) = delete;

  absl::Status Append(std::string_view data);
  // Flushes the file's data to stable storage; safe alongside Append from another thread.
  absl::Status Sync();
  const std::string& path() const noexcept { return _path; }
  uint64_t size() const noexcept { return _size.load(std::memory_order_relaxed); }

 private:
  AppendFile(std::string path, int fd) : _path(std::move(path)), _fd(fd) {}

  std::string _path;
  int _fd;
  std::atomic<uint64_t> _size{0};
};

// Makes file creation, renaming and removal in a directory durable.
absl::Status SyncDirectory(const std::string& path);
absl::Status TruncateAndSync(const std::string& path, uint64_t size);

// A source of frames, from a file or from memory.
class FrameSource {
 public:
  virtual ~FrameSource() = default;
  // The frame at the current position, without moving past it. Its views stay valid until the
  // next call. kIncomplete at the end of the data.
  virtual ParseStatus Peek(Frame* frame) = 0;
  virtual void Advance() = 0;
};

// Reads frames from a file, which may be growing: after kIncomplete, a later Peek sees new data.
class FrameFileReader final : public FrameSource {
 public:
  static absl::StatusOr<std::unique_ptr<FrameFileReader>> Open(const std::string& path);
  ~FrameFileReader() override;

  ParseStatus Peek(Frame* frame) override;
  void Advance() override;
  // Offset of the frame Peek returns.
  uint64_t offset() const noexcept { return _offset; }
  // After Peek returned kIncomplete: whether the file ended cleanly at a frame boundary.
  bool at_end() const noexcept { return _begin == _buffer.size(); }

 private:
  explicit FrameFileReader(int fd) : _fd(fd) {}
  size_t Fill(size_t at_least);

  int _fd;
  std::string _buffer;
  size_t _begin = 0;         // Start of the current frame in _buffer.
  size_t _current_size = 0;  // Size of the frame last returned by Peek.
  uint64_t _offset = 0;      // File offset of _begin.
  uint64_t _read_at = 0;     // File offset of the end of _buffer.
};

}  // namespace gendb::internal
