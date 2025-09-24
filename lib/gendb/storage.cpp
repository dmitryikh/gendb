#include "gendb/storage.h"

#include <rocksdb/db.h>
#include <rocksdb/iterator.h>
#include <rocksdb/options.h>
#include <rocksdb/slice.h>

#include "absl/strings/str_cat.h"

namespace gendb {

RocksDBStorage::RocksDBStorage(const std::string& db_path) {
  rocksdb::Options options;
  options.create_if_missing = true;
  options.create_missing_column_families = true;

  // List existing column families
  std::vector<std::string> column_family_names;
  rocksdb::Status status = rocksdb::DB::ListColumnFamilies(options, db_path, &column_family_names);

  // If the database doesn't exist yet, start with default column family
  if (!status.ok()) {
    column_family_names = {rocksdb::kDefaultColumnFamilyName};
  }

  // Create column family descriptors
  std::vector<rocksdb::ColumnFamilyDescriptor> column_families;
  for (const auto& name : column_family_names) {
    column_families.emplace_back(name, rocksdb::ColumnFamilyOptions());
  }

  std::vector<rocksdb::ColumnFamilyHandle*> handles;
  rocksdb::DB* db;

  status = rocksdb::DB::Open(options, db_path, column_families, &handles, &db);
  if (!status.ok()) {
    throw std::runtime_error("Failed to open RocksDB: " + status.ToString());
  }

  db_.reset(db);
  column_families_.reserve(handles.size());
  for (auto* handle : handles) {
    column_families_.emplace_back(handle);
  }
}

RocksDBStorage::~RocksDBStorage() = default;

RocksDBStorage::RocksDBStorage(RocksDBStorage&& other) noexcept
    : db_(std::move(other.db_)),
      column_families_(std::move(other.column_families_)),
      get_buffer_(std::move(other.get_buffer_)) {}

RocksDBStorage& RocksDBStorage::operator=(RocksDBStorage&& other) noexcept {
  if (this != &other) {
    db_ = std::move(other.db_);
    column_families_ = std::move(other.column_families_);
    get_buffer_ = std::move(other.get_buffer_);
  }
  return *this;
}

void RocksDBStorage::Put(const size_t collection_id, BytesConstView key, Bytes&& value) {
  rocksdb::ColumnFamilyHandle* cf = GetOrCreateColumnFamily(collection_id);

  rocksdb::Slice key_slice(reinterpret_cast<const char*>(key.data()), key.size());
  rocksdb::Slice value_slice(reinterpret_cast<const char*>(value.data()), value.size());

  rocksdb::Status status = db_->Put(rocksdb::WriteOptions(), cf, key_slice, value_slice);
  if (!status.ok()) {
    throw std::runtime_error("RocksDB Put failed: " + status.ToString());
  }
}

absl::Status RocksDBStorage::Delete(const size_t collection_id, BytesConstView key) {
  if (collection_id >= column_families_.size()) {
    return absl::NotFoundError("Collection not found");
  }

  rocksdb::ColumnFamilyHandle* cf = column_families_[collection_id].get();
  rocksdb::Slice key_slice(reinterpret_cast<const char*>(key.data()), key.size());

  rocksdb::Status status = db_->Delete(rocksdb::WriteOptions(), cf, key_slice);
  if (!status.ok()) {
    return absl::InternalError("RocksDB Delete failed: " + status.ToString());
  }

  return absl::OkStatus();
}

absl::Status RocksDBStorage::Get(const size_t collection_id, BytesConstView key,
                                 BytesConstView& value) const {
  if (collection_id >= column_families_.size()) {
    return absl::NotFoundError("Collection not found");
  }

  rocksdb::ColumnFamilyHandle* cf = column_families_[collection_id].get();
  rocksdb::Slice key_slice(reinterpret_cast<const char*>(key.data()), key.size());

  // Resize the buffer for this thread if needed
  if (get_buffer_.size() <= collection_id) {
    get_buffer_.resize(collection_id + 1);
  }

  std::string result;
  rocksdb::Status status = db_->Get(rocksdb::ReadOptions(), cf, key_slice, &result);

  if (status.IsNotFound()) {
    return absl::NotFoundError("Key not found");
  }

  if (!status.ok()) {
    return absl::InternalError("RocksDB Get failed: " + status.ToString());
  }

  // Copy result to our buffer and return a view
  get_buffer_[collection_id].assign(
      reinterpret_cast<const uint8_t*>(result.data()),
      reinterpret_cast<const uint8_t*>(result.data()) + result.size());

  value = BytesConstView(get_buffer_[collection_id]);
  return absl::OkStatus();
}

bool RocksDBStorage::Exists(const size_t collection_id, BytesConstView key) const {
  if (collection_id >= column_families_.size()) {
    return false;
  }

  rocksdb::ColumnFamilyHandle* cf = column_families_[collection_id].get();
  rocksdb::Slice key_slice(reinterpret_cast<const char*>(key.data()), key.size());

  std::string result;
  rocksdb::Status status = db_->Get(rocksdb::ReadOptions(), cf, key_slice, &result);

  return status.ok();
}

size_t RocksDBStorage::GetCollectionCount() const {
  return column_families_.size();
}

size_t RocksDBStorage::GetCollectionSize(const size_t collection_id) const {
  if (collection_id >= column_families_.size()) {
    return 0;
  }

  rocksdb::ColumnFamilyHandle* cf = column_families_[collection_id].get();
  std::unique_ptr<rocksdb::Iterator> it(db_->NewIterator(rocksdb::ReadOptions(), cf));

  size_t count = 0;
  for (it->SeekToFirst(); it->Valid(); it->Next()) {
    ++count;
  }

  return count;
}

void RocksDBStorage::Clear() {
  // Delete all column families except the default one, then recreate them
  for (size_t i = 1; i < column_families_.size(); ++i) {
    rocksdb::Status status = db_->DropColumnFamily(column_families_[i].get());
    if (!status.ok()) {
      throw std::runtime_error("Failed to drop column family: " + status.ToString());
    }
  }

  // Clear the default column family
  if (!column_families_.empty()) {
    rocksdb::ColumnFamilyHandle* default_cf = column_families_[0].get();
    std::unique_ptr<rocksdb::Iterator> it(db_->NewIterator(rocksdb::ReadOptions(), default_cf));

    for (it->SeekToFirst(); it->Valid(); it->Next()) {
      rocksdb::Status status = db_->Delete(rocksdb::WriteOptions(), default_cf, it->key());
      if (!status.ok()) {
        throw std::runtime_error("Failed to delete key during clear: " + status.ToString());
      }
    }
  }

  // Keep only the default column family
  column_families_.resize(1);
  get_buffer_.clear();
}

std::string RocksDBStorage::MakeCollectionKey(size_t collection_id, BytesConstView key) const {
  return absl::StrCat(collection_id, ":",
                      std::string(reinterpret_cast<const char*>(key.data()), key.size()));
}

rocksdb::ColumnFamilyHandle* RocksDBStorage::GetOrCreateColumnFamily(size_t collection_id) {
  // Ensure we have enough column families
  while (collection_id >= column_families_.size()) {
    std::string cf_name = "collection_" + std::to_string(column_families_.size());

    rocksdb::ColumnFamilyHandle* cf_handle;
    rocksdb::Status status =
        db_->CreateColumnFamily(rocksdb::ColumnFamilyOptions(), cf_name, &cf_handle);
    if (!status.ok()) {
      throw std::runtime_error("Failed to create column family: " + status.ToString());
    }

    column_families_.emplace_back(cf_handle);
  }

  return column_families_[collection_id].get();
}

}  // namespace gendb