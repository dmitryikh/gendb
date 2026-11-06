#include "gendb/file_system.h"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <system_error>

#include "absl/strings/str_cat.h"

namespace gendb {
namespace {

absl::Status ErrnoStatus(std::string_view what, std::string_view path) {
  return absl::ErrnoToStatus(errno, absl::StrCat(what, " ", path));
}

// fsync on macOS does not reach the platter; on Linux, fdatasync skips what recovery does not need.
int SyncFd(int fd) {
#if defined(__APPLE__)
  return fcntl(fd, F_FULLFSYNC);
#else
  return fdatasync(fd);
#endif
}

class PosixWritableFile final : public WritableFile {
 public:
  PosixWritableFile(std::string path, int fd) : _path(std::move(path)), _fd(fd) {}
  ~PosixWritableFile() override { close(_fd); }

  absl::Status Append(std::string_view data) override {
    uint64_t at = _size.load(std::memory_order_relaxed);
    while (!data.empty()) {
      const ssize_t written = pwrite(_fd, data.data(), data.size(), static_cast<off_t>(at));
      if (written < 0) {
        if (errno == EINTR) continue;
        return ErrnoStatus("write", _path);
      }
      data.remove_prefix(static_cast<size_t>(written));
      at += static_cast<uint64_t>(written);
      _size.store(at, std::memory_order_relaxed);
    }
    _extent = std::max(_extent, at);
    return absl::OkStatus();
  }
  absl::Status Sync() override {
    while (SyncFd(_fd) != 0) {
      if (errno != EINTR) return ErrnoStatus("sync", _path);
    }
    return absl::OkStatus();
  }
  uint64_t size() const override { return _size.load(std::memory_order_relaxed); }

  absl::Status Reserve(uint64_t bytes) override {
    // Zeros rather than fallocate, as a block that was never written can still need its metadata
    // changed at a sync, and on some file systems does.
    static const std::array<char, 1 << 16> kZeros{};
    const uint64_t end = _size.load(std::memory_order_relaxed) + bytes;
    while (_extent < end) {
      const size_t count = static_cast<size_t>(std::min<uint64_t>(end - _extent, kZeros.size()));
      const ssize_t written = pwrite(_fd, kZeros.data(), count, static_cast<off_t>(_extent));
      if (written < 0 && errno == EINTR) continue;
      if (written <= 0) break;  // Appends will find out, if there is no room for them either.
      _extent += static_cast<uint64_t>(written);
    }
    return Sync();
  }
  uint64_t reserved() const override { return _extent - _size.load(std::memory_order_relaxed); }
  absl::Status ReleaseReserved() override {
    const uint64_t size = _size.load(std::memory_order_relaxed);
    if (_extent == size) return absl::OkStatus();
    if (ftruncate(_fd, static_cast<off_t>(size)) != 0 || fsync(_fd) != 0) {
      return ErrnoStatus("truncate", _path);
    }
    _extent = size;
    return absl::OkStatus();
  }

 private:
  std::string _path;
  int _fd;
  std::atomic<uint64_t> _size{0};
  uint64_t _extent = 0;  // The size of the file, with reserved bytes. Writer only.
};

class PosixReadableFile final : public ReadableFile {
 public:
  PosixReadableFile(std::string path, int fd) : _path(std::move(path)), _fd(fd) {}
  ~PosixReadableFile() override { close(_fd); }

  absl::StatusOr<size_t> Read(uint64_t offset, char* out, size_t size) override {
    size_t total = 0;
    while (total < size) {
      const ssize_t got = pread(_fd, out + total, size - total, static_cast<off_t>(offset + total));
      if (got < 0) {
        if (errno == EINTR) continue;
        return ErrnoStatus("read", _path);
      }
      if (got == 0) break;
      total += static_cast<size_t>(got);
    }
    return total;
  }

 private:
  std::string _path;
  int _fd;
};

class PosixFileLock final : public FileLock {
 public:
  explicit PosixFileLock(int fd) : _fd(fd) {}
  ~PosixFileLock() override { close(_fd); }

 private:
  int _fd;
};

class PosixFileSystem final : public FileSystem {
 public:
  absl::StatusOr<std::unique_ptr<WritableFile>> Create(const std::string& path) override {
    const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return ErrnoStatus("create", path);
    return std::unique_ptr<WritableFile>(new PosixWritableFile(path, fd));
  }

  absl::StatusOr<std::unique_ptr<ReadableFile>> OpenForRead(const std::string& path) override {
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return ErrnoStatus("open", path);
    return std::unique_ptr<ReadableFile>(new PosixReadableFile(path, fd));
  }

  absl::StatusOr<uint64_t> FileSize(const std::string& path) override {
    struct stat info;
    if (stat(path.c_str(), &info) != 0) return ErrnoStatus("stat", path);
    return static_cast<uint64_t>(info.st_size);
  }

  absl::StatusOr<std::vector<std::string>> List(const std::string& directory) override {
    std::vector<std::string> names;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
      names.push_back(entry.path().filename().string());
    }
    if (error) return absl::InternalError(absl::StrCat("list ", directory, ": ", error.message()));
    return names;
  }

  absl::Status CreateDirectories(const std::string& path) override {
    std::error_code error;
    std::filesystem::create_directories(path, error);
    if (error) return absl::InternalError(absl::StrCat("create ", path, ": ", error.message()));
    return absl::OkStatus();
  }

  absl::Status Rename(const std::string& from, const std::string& to) override {
    return std::rename(from.c_str(), to.c_str()) == 0 ? absl::OkStatus()
                                                      : ErrnoStatus("rename", to);
  }

  absl::Status Remove(const std::string& path) override {
    if (unlink(path.c_str()) == 0 || errno == ENOENT) return absl::OkStatus();
    return ErrnoStatus("remove", path);
  }

  absl::Status TruncateAndSync(const std::string& path, uint64_t size) override {
    const int fd = open(path.c_str(), O_WRONLY | O_CLOEXEC);
    if (fd < 0) return ErrnoStatus("open", path);
    absl::Status status;
    if (ftruncate(fd, static_cast<off_t>(size)) != 0 || fsync(fd) != 0) {
      status = ErrnoStatus("truncate", path);
    }
    close(fd);
    return status;
  }

  absl::Status SyncDirectory(const std::string& path) override {
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return ErrnoStatus("open directory", path);
    absl::Status status;
    if (fsync(fd) != 0) status = ErrnoStatus("sync directory", path);
    close(fd);
    return status;
  }

  absl::StatusOr<std::unique_ptr<FileLock>> Lock(const std::string& path) override {
    const int fd = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) return ErrnoStatus("open", path);
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
      close(fd);
      return absl::FailedPreconditionError(absl::StrCat(path, " is held by another engine"));
    }
    return std::unique_ptr<FileLock>(new PosixFileLock(fd));
  }
};

}  // namespace

std::shared_ptr<FileSystem> DefaultFileSystem() {
  static const auto* file_system = new std::shared_ptr<FileSystem>(new PosixFileSystem());
  return *file_system;
}

}  // namespace gendb
