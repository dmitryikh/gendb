#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gendb/file_system.h"
#include "gendb/log_format.h"

namespace gendb::internal {

// A source of frames, from a file or from memory.
class FrameSource {
 public:
  virtual ~FrameSource() = default;
  // The frame at the current position, without moving past it. Its views stay valid until the
  // next call. kIncomplete at the end of the data, or after an error: see status().
  virtual ParseStatus Peek(Frame* frame) = 0;
  virtual void Advance() = 0;
  // A failure to read, which says nothing about the data.
  virtual absl::Status status() const { return absl::OkStatus(); }
};

// Reads frames from a file, which may be growing: after kIncomplete, a later Peek sees new data.
class FrameFileReader final : public FrameSource {
 public:
  static absl::StatusOr<std::unique_ptr<FrameFileReader>> Open(FileSystem& file_system,
                                                               const std::string& path);

  ParseStatus Peek(Frame* frame) override;
  void Advance() override;
  absl::Status status() const override { return _status; }
  // Offset of the frame Peek returns.
  uint64_t offset() const noexcept { return _offset; }
  // After Peek returned kIncomplete: whether the file ended cleanly at a frame boundary.
  bool at_end() const noexcept { return _status.ok() && _begin == _buffer.size(); }

 private:
  explicit FrameFileReader(std::unique_ptr<ReadableFile> file) : _file(std::move(file)) {}
  size_t Fill(size_t at_least);

  std::unique_ptr<ReadableFile> _file;
  absl::Status _status;
  std::string _buffer;
  size_t _begin = 0;         // Start of the current frame in _buffer.
  size_t _current_size = 0;  // Size of the frame last returned by Peek.
  uint64_t _offset = 0;      // File offset of _begin.
  uint64_t _read_at = 0;     // File offset of the end of _buffer.
};

}  // namespace gendb::internal
