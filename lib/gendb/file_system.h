#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

// Everything the engine does to the disk goes through this, so that tests can fail any step and
// simulate power loss, and applications can store a database elsewhere.
namespace gendb {

class WritableFile {
 public:
  virtual ~WritableFile() = default;
  virtual absl::Status Append(std::string_view data) = 0;
  // Returns once the data appended so far is on stable storage, and the file's size with it. Does
  // not cover the file's name: see FileSystem::SyncDirectory. May run alongside Append.
  virtual absl::Status Sync() = 0;
  // Bytes appended so far, from any thread.
  virtual uint64_t size() const = 0;

  // Makes sure that the next `bytes` bytes appended fall inside the file as it already is, by
  // filling them with zeros on disk, and syncs. A sync then has only those bytes to write and not a
  // new file size, which on most file systems is several times faster. Reading the file shows the
  // zeros after what was appended. Only an error from the sync is returned, as it means appended
  // data may be gone, whereas without room for the zeros the file just goes on growing as it would
  // have. Runs alongside Sync, but not Append. Optional for a file system, as it is only about
  // speed.
  virtual absl::Status Reserve(uint64_t /*bytes*/) { return absl::OkStatus(); }
  // How many bytes can still be appended inside what Reserve made.
  virtual uint64_t reserved() const { return 0; }
  // Gives back what is left of a reservation, durably. Runs alone.
  virtual absl::Status ReleaseReserved() { return absl::OkStatus(); }
};

class ReadableFile {
 public:
  virtual ~ReadableFile() = default;
  // Reads up to `size` bytes at `offset`, and returns how many: fewer only at the end of the file.
  virtual absl::StatusOr<size_t> Read(uint64_t offset, char* out, size_t size) = 0;
};

// Held for as long as it lives.
class FileLock {
 public:
  virtual ~FileLock() = default;
};

class FileSystem {
 public:
  virtual ~FileSystem() = default;

  // Creates a file, or empties an existing one.
  virtual absl::StatusOr<std::unique_ptr<WritableFile>> Create(const std::string& path) = 0;
  virtual absl::StatusOr<std::unique_ptr<ReadableFile>> OpenForRead(const std::string& path) = 0;
  virtual absl::StatusOr<uint64_t> FileSize(const std::string& path) = 0;
  // The names of the entries of a directory, without their path.
  virtual absl::StatusOr<std::vector<std::string>> List(const std::string& directory) = 0;
  virtual absl::Status CreateDirectories(const std::string& path) = 0;
  // Replaces `to` atomically, if it exists.
  virtual absl::Status Rename(const std::string& from, const std::string& to) = 0;
  // Succeeds if the file does not exist.
  virtual absl::Status Remove(const std::string& path) = 0;
  // Cuts a file to `size` bytes, durably.
  virtual absl::Status TruncateAndSync(const std::string& path, uint64_t size) = 0;
  // Makes the creations, renames and removals in a directory durable.
  virtual absl::Status SyncDirectory(const std::string& path) = 0;
  // FailedPrecondition if another holder has it.
  virtual absl::StatusOr<std::unique_ptr<FileLock>> Lock(const std::string& path) = 0;
};

// The real one.
std::shared_ptr<FileSystem> DefaultFileSystem();

}  // namespace gendb
