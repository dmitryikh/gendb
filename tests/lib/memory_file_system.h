#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "gendb/file_system.h"

namespace gendb::testing {

// A file system in memory that models what a disk promises and no more, to test what a crash can
// do.
//
// Appended data is safe only once its file is synced; creations, renames and removals only once
// their directory is synced, and until then any of them may or may not survive, in any combination.
// PowerLoss() takes away the rest, in the way a mode describes. Any mutating operation can be made
// to fail, or to be where the power goes out.
class MemoryFileSystem final : public FileSystem {
 public:
  enum class Loss {
    kNothing,      // The process died: the OS still has everything.
    kAllUnsynced,  // Back to exactly what was promised.
    // Promised state, plus a random part of unsynced data, maybe garbage, and a random subset of
    // the unsynced directory operations.
    kPartOfUnsynced,
  };

  // Everything from the `count`th mutating operation on, counted from now, fails, as if the machine
  // had stopped. PowerLoss() brings it back.
  void CrashAfter(uint64_t count) {
    std::lock_guard lock(_mutex);
    _crash_in = count;
  }
  // The `count`th mutating operation from now fails with `error`, and only that one. An Append
  // writes half of its data first.
  void FailAfter(uint64_t count, absl::Status error) {
    std::lock_guard lock(_mutex);
    _fail_in = count;
    _failure = std::move(error);
  }
  // Reads at or past `offset` of paths containing `fragment` fail, until cleared with an empty one.
  void FailReads(std::string fragment, uint64_t offset = 0) {
    std::lock_guard lock(_mutex);
    _failing_reads = std::move(fragment);
    _failing_reads_from = offset;
  }
  uint64_t operations() const {
    std::lock_guard lock(_mutex);
    return _operations;
  }
  bool crashed() const {
    std::lock_guard lock(_mutex);
    return _crashed;
  }

  void PowerLoss(Loss loss, uint32_t seed = 0) {
    std::lock_guard lock(_mutex);
    std::mt19937 rng(seed);
    if (loss != Loss::kNothing) {
      for (const DirectoryOp& op : _pending) {
        if (loss == Loss::kPartOfUnsynced && rng() % 2 == 0) Apply(op);
      }
      _entries = _durable;
      for (auto& [path, node] : _entries) {
        const bool growing = node->data.size() > node->synced;
        size_t keep = node->synced;
        if (loss == Loss::kPartOfUnsynced && growing) {
          keep += rng() % (node->data.size() - node->synced + 1);
        }
        node->data.resize(keep);
        // The OS writes pages in any order, so unsynced data can have holes in what survives.
        if (keep > node->synced && rng() % 2 == 0) {
          const size_t begin = node->synced + rng() % (keep - node->synced);
          const size_t count = std::min<size_t>(1 + rng() % 64, keep - begin);
          node->data.replace(begin, count, count, '\0');
        }
        // A file that was growing can end in blocks that were allocated but never written.
        if (loss == Loss::kPartOfUnsynced && growing && rng() % 2 == 0) {
          for (uint32_t n = rng() % 32; n > 0; --n) {
            node->data.push_back(rng() % 3 == 0 ? '\0' : static_cast<char>(rng()));
          }
        }
      }
    }
    for (auto& [path, node] : _entries) node->synced = node->data.size();
    _durable = _entries;
    _pending.clear();
    _crashed = false;
    _crash_in.reset();
    _fail_in.reset();
  }

  // What is there now, and a way to damage it, as a failing disk would.
  std::vector<std::string> Paths() const {
    std::lock_guard lock(_mutex);
    std::vector<std::string> paths;
    for (const auto& [path, node] : _entries) paths.push_back(path);
    return paths;
  }
  std::string Contents(const std::string& path) const {
    std::lock_guard lock(_mutex);
    return _entries.at(path)->data;
  }
  void FlipByte(const std::string& path, size_t offset) {
    std::lock_guard lock(_mutex);
    _entries.at(path)->data[offset] ^= 0x5a;
  }

  absl::StatusOr<std::unique_ptr<WritableFile>> Create(const std::string& path) override {
    std::lock_guard lock(_mutex);
    if (absl::Status status = Step(); !status.ok()) return status;
    auto& node = _entries[path];
    if (node == nullptr) {
      node = std::make_shared<Node>();
      _pending.push_back({path, path, node});
    } else {
      node->data.clear();
      node->synced = 0;
    }
    return std::unique_ptr<WritableFile>(new File(*this, node));
  }

  absl::StatusOr<std::unique_ptr<ReadableFile>> OpenForRead(const std::string& path) override {
    std::lock_guard lock(_mutex);
    const auto it = _entries.find(path);
    if (it == _entries.end()) return absl::NotFoundError(absl::StrCat("No file ", path));
    return std::unique_ptr<ReadableFile>(new Reader(*this, it->second, path));
  }

  absl::StatusOr<uint64_t> FileSize(const std::string& path) override {
    std::lock_guard lock(_mutex);
    const auto it = _entries.find(path);
    if (it == _entries.end()) return absl::NotFoundError(absl::StrCat("No file ", path));
    return it->second->data.size();
  }

  absl::StatusOr<std::vector<std::string>> List(const std::string& directory) override {
    std::lock_guard lock(_mutex);
    if (_crashed) return Stopped();
    const std::string prefix = directory + "/";
    std::vector<std::string> names;
    for (const auto& [path, node] : _entries) {
      if (path.starts_with(prefix)) names.push_back(path.substr(prefix.size()));
    }
    return names;
  }

  absl::Status CreateDirectories(const std::string&) override { return absl::OkStatus(); }

  absl::Status Rename(const std::string& from, const std::string& to) override {
    std::lock_guard lock(_mutex);
    if (absl::Status status = Step(); !status.ok()) return status;
    const auto it = _entries.find(from);
    if (it == _entries.end()) return absl::NotFoundError(absl::StrCat("No file ", from));
    auto node = it->second;
    _entries.erase(it);
    _pending.push_back({from, to, node});
    _entries[to] = std::move(node);
    return absl::OkStatus();
  }

  absl::Status Remove(const std::string& path) override {
    std::lock_guard lock(_mutex);
    if (absl::Status status = Step(); !status.ok()) return status;
    if (_entries.erase(path) > 0) _pending.push_back({path, "", nullptr});
    return absl::OkStatus();
  }

  absl::Status TruncateAndSync(const std::string& path, uint64_t size) override {
    std::lock_guard lock(_mutex);
    if (absl::Status status = Step(); !status.ok()) return status;
    const auto& node = _entries.at(path);
    node->data.resize(size);
    node->synced = size;
    return absl::OkStatus();
  }

  absl::Status SyncDirectory(const std::string& path) override {
    std::lock_guard lock(_mutex);
    if (absl::Status status = Step(); !status.ok()) return status;
    const std::string prefix = path + "/";
    std::erase_if(_pending, [&](const DirectoryOp& op) {
      if (!op.from.starts_with(prefix)) return false;
      Apply(op);
      return true;
    });
    return absl::OkStatus();
  }

  absl::StatusOr<std::unique_ptr<FileLock>> Lock(const std::string& path) override {
    std::lock_guard lock(_mutex);
    if (_locks[path]) return absl::FailedPreconditionError(absl::StrCat(path, " is held"));
    _locks[path] = true;
    return std::unique_ptr<FileLock>(new Held(*this, path));
  }

 private:
  struct Node {
    std::string data;
    size_t synced = 0;
  };
  // A rename from `from` to `to`, which is a creation if they are equal, and a removal if `to` is
  // empty.
  struct DirectoryOp {
    std::string from;
    std::string to;
    std::shared_ptr<Node> node;
  };

  void Apply(const DirectoryOp& op) {
    if (op.from != op.to) _durable.erase(op.from);
    if (!op.to.empty()) _durable[op.to] = op.node;
  }

  class File final : public WritableFile {
   public:
    File(MemoryFileSystem& owner, std::shared_ptr<Node> node)
        : _owner(owner), _node(std::move(node)) {}
    absl::Status Append(std::string_view data) override {
      std::lock_guard lock(_owner._mutex);
      if (absl::Status status = _owner.Step(); !status.ok()) {
        if (!_owner._crashed) _node->data.append(data.substr(0, data.size() / 2));
        return status;
      }
      _node->data.append(data);
      return absl::OkStatus();
    }
    absl::Status Sync() override {
      std::lock_guard lock(_owner._mutex);
      if (absl::Status status = _owner.Step(); !status.ok()) return status;
      _node->synced = _node->data.size();
      return absl::OkStatus();
    }
    uint64_t size() const override {
      std::lock_guard lock(_owner._mutex);
      return _node->data.size();
    }

   private:
    MemoryFileSystem& _owner;
    std::shared_ptr<Node> _node;
  };

  class Reader final : public ReadableFile {
   public:
    Reader(MemoryFileSystem& owner, std::shared_ptr<Node> node, std::string path)
        : _owner(owner), _node(std::move(node)), _path(std::move(path)) {}
    absl::StatusOr<size_t> Read(uint64_t offset, char* out, size_t size) override {
      std::lock_guard lock(_owner._mutex);
      if (_owner._crashed) return Stopped();
      if (!_owner._failing_reads.empty() && offset >= _owner._failing_reads_from &&
          _path.find(_owner._failing_reads) != std::string::npos) {
        return absl::InternalError(absl::StrCat("Cannot read ", _path));
      }
      if (offset >= _node->data.size()) return size_t{0};
      const size_t count = std::min(size, _node->data.size() - offset);
      _node->data.copy(out, count, offset);
      return count;
    }

   private:
    MemoryFileSystem& _owner;
    std::shared_ptr<Node> _node;
    std::string _path;
  };

  class Held final : public FileLock {
   public:
    Held(MemoryFileSystem& owner, std::string path) : _owner(owner), _path(std::move(path)) {}
    ~Held() override {
      std::lock_guard lock(_owner._mutex);
      _owner._locks[_path] = false;
    }

   private:
    MemoryFileSystem& _owner;
    std::string _path;
  };

  static absl::Status Stopped() { return absl::UnavailableError("The machine has stopped"); }

  // Counts one mutating operation, and says whether it is allowed.
  absl::Status Step() {
    if (_crashed) return Stopped();
    ++_operations;
    if (_crash_in.has_value() && (*_crash_in)-- == 0) {
      _crashed = true;
      return Stopped();
    }
    if (_fail_in.has_value() && (*_fail_in)-- == 0) {
      _fail_in.reset();
      return _failure;
    }
    return absl::OkStatus();
  }

  mutable std::mutex _mutex;
  std::map<std::string, std::shared_ptr<Node>> _entries;  // What the running system sees.
  std::map<std::string, std::shared_ptr<Node>> _durable;  // What survives a power loss.
  std::vector<DirectoryOp> _pending;                      // What may survive it.
  std::map<std::string, bool> _locks;
  uint64_t _operations = 0;
  std::optional<uint64_t> _crash_in;
  std::optional<uint64_t> _fail_in;
  absl::Status _failure;
  std::string _failing_reads;
  uint64_t _failing_reads_from = 0;
  bool _crashed = false;
};

}  // namespace gendb::testing
