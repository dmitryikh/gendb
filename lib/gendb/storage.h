#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "absl/hash/hash.h"
#include "absl/status/status.h"
#include "gendb/bytes.h"

// Forward declaration for RocksDB
namespace rocksdb {
class DB;
class ColumnFamilyHandle;
}  // namespace rocksdb

namespace gendb {

// Abstract base class for storage backends
class Storage {
 public:
  using Collection = std::unordered_map<Bytes, Bytes, BytesHash, BytesEqual>;

  virtual ~Storage() = default;

  // Store a key-value pair in the specified collection
  virtual void Put(const size_t collection_id, BytesConstView key, Bytes&& value) = 0;

  // Delete a key from the specified collection
  virtual absl::Status Delete(const size_t collection_id, BytesConstView key) = 0;

  // Get a value by key from the specified collection
  virtual absl::Status Get(const size_t collection_id, BytesConstView key,
                           BytesConstView& value) const = 0;

  // Check if a key exists in the specified collection
  virtual bool Exists(const size_t collection_id, BytesConstView key) const = 0;

  // Get the number of collections
  virtual size_t GetCollectionCount() const = 0;

  // Get the size of a specific collection
  virtual size_t GetCollectionSize(const size_t collection_id) const = 0;

  // Clear all data from all collections
  virtual void Clear() = 0;
};

// In-memory storage implementation
class MemoryStorage : public Storage {
 public:
  std::vector<Collection> collections;

  void Put(const size_t collection_id, BytesConstView key, Bytes&& value) override {
    if (collection_id >= collections.size()) {
      collections.resize(collection_id + 1);
    }
    auto& coll = collections[collection_id];
    // TODO(perf): can we build the pair in-place?
    coll[Bytes{key.begin(), key.end()}] = std::move(value);
  }

  absl::Status Delete(const size_t collection_id, BytesConstView key) override {
    if (collection_id >= collections.size()) {
      return absl::NotFoundError("Collection not found");
    }
    auto& coll = collections[collection_id];
    auto it = coll.find(Bytes{key.begin(), key.end()});
    if (it == coll.end()) {
      return absl::NotFoundError("Key not found");
    }
    coll.erase(it);
    return absl::OkStatus();
  }

  absl::Status Get(const size_t collection_id, BytesConstView key,
                   BytesConstView& value) const override {
    if (collection_id >= collections.size()) {
      return absl::NotFoundError("Collection not found");
    }
    const auto& coll = collections[collection_id];
    auto it = coll.find(Bytes{key.begin(), key.end()});
    if (it == coll.end()) {
      return absl::NotFoundError("Key not found");
    }
    value = BytesConstView{it->second};
    return absl::OkStatus();
  }

  bool Exists(const size_t collection_id, BytesConstView key) const override {
    if (collection_id >= collections.size()) {
      return false;
    }
    const auto& coll = collections[collection_id];
    return coll.find(Bytes{key.begin(), key.end()}) != coll.end();
  }

  size_t GetCollectionCount() const override { return collections.size(); }

  size_t GetCollectionSize(const size_t collection_id) const override {
    if (collection_id >= collections.size()) {
      return 0;
    }
    return collections[collection_id].size();
  }

  void Clear() override { collections.clear(); }
};

// RocksDB storage implementation
class RocksDBStorage : public Storage {
 public:
  // Constructor that takes a database path
  explicit RocksDBStorage(const std::string& db_path);

  // Destructor
  ~RocksDBStorage() override;

  // Move constructor and assignment
  RocksDBStorage(RocksDBStorage&& other) noexcept;
  RocksDBStorage& operator=(RocksDBStorage&& other) noexcept;

  // Delete copy constructor and assignment
  RocksDBStorage(const RocksDBStorage&) = delete;
  RocksDBStorage& operator=(const RocksDBStorage&) = delete;

  void Put(const size_t collection_id, BytesConstView key, Bytes&& value) override;

  absl::Status Delete(const size_t collection_id, BytesConstView key) override;

  absl::Status Get(const size_t collection_id, BytesConstView key,
                   BytesConstView& value) const override;

  bool Exists(const size_t collection_id, BytesConstView key) const override;

  size_t GetCollectionCount() const override;

  size_t GetCollectionSize(const size_t collection_id) const override;

  void Clear() override;

 private:
  std::unique_ptr<rocksdb::DB> db_;
  std::vector<std::unique_ptr<rocksdb::ColumnFamilyHandle>> column_families_;
  mutable std::vector<Bytes> get_buffer_;  // Thread-local buffer for Get operations

  // Helper methods
  std::string MakeCollectionKey(size_t collection_id, BytesConstView key) const;
  rocksdb::ColumnFamilyHandle* GetOrCreateColumnFamily(size_t collection_id);
};

}  // namespace gendb