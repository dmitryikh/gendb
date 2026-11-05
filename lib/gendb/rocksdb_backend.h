#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gendb/bytes.h"

namespace rocksdb {
class ColumnFamilyHandle;
class DB;
}  // namespace rocksdb

namespace gendb {

// Durable half of the engine: every commit is one atomic RocksDB write batch, so RocksDB's WAL
// provides atomicity and crash recovery. Reads are served from memory; this is read only on open.
class RocksDbBackend {
 public:
  struct Write {
    size_t collection;
    bool deleted;
    BytesConstView key;
    BytesConstView value;
  };

  // `collections` are the persistent collections' names; an index into it identifies one, and an
  // empty name means it is not persistent. Column families of removed collections are preserved.
  static absl::StatusOr<std::unique_ptr<RocksDbBackend>> Open(const std::string& path,
                                                              std::vector<std::string> collections,
                                                              bool change_log);
  ~RocksDbBackend();

  uint64_t last_seq() const noexcept { return _last_seq; }
  absl::Status Load(size_t collection,
                    const std::function<void(BytesConstView key, BytesConstView value)>& fn) const;
  absl::Status Commit(uint64_t seq, std::span<const Write> writes, bool sync);
  absl::Status SyncWal();

  absl::Status ReadChanges(uint64_t after_seq,
                           const std::function<bool(uint64_t seq, const Write&)>& fn) const;
  absl::Status TruncateChanges(uint64_t up_to_seq);

 private:
  RocksDbBackend() = default;

  std::unique_ptr<rocksdb::DB> _db;
  std::vector<rocksdb::ColumnFamilyHandle*> _handles;      // All open column families.
  std::vector<rocksdb::ColumnFamilyHandle*> _collections;  // By collection; null if ephemeral.
  rocksdb::ColumnFamilyHandle* _meta = nullptr;
  rocksdb::ColumnFamilyHandle* _changes = nullptr;
  std::vector<std::string> _names;
  bool _change_log = false;
  uint64_t _last_seq = 0;
};

}  // namespace gendb
