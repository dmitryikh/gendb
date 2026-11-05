#include "gendb/rocksdb_backend.h"

#include <rocksdb/cache.h>
#include <rocksdb/db.h>
#include <rocksdb/iterator.h>
#include <rocksdb/options.h>
#include <rocksdb/table.h>
#include <rocksdb/write_batch.h>

#include <algorithm>
#include <cstring>

namespace gendb {
namespace {

constexpr std::string_view kCollectionPrefix = "c_";
constexpr std::string_view kMetaFamily = "__meta";
constexpr std::string_view kChangesFamily = "__changes";
constexpr std::string_view kLastSeqKey = "last_seq";

rocksdb::Slice AsSlice(BytesConstView bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
BytesConstView AsBytes(const rocksdb::Slice& slice) {
  return {reinterpret_cast<const uint8_t*>(slice.data()), slice.size()};
}
absl::Status ToStatus(const rocksdb::Status& status) {
  if (status.ok()) return absl::OkStatus();
  if (status.IsNotFound()) return absl::NotFoundError(status.ToString());
  return absl::InternalError(status.ToString());
}

void AppendBigEndian64(std::string& out, uint64_t value) {
  for (int shift = 56; shift >= 0; shift -= 8) out.push_back(char(value >> shift));
}
uint64_t ReadBigEndian64(const char* p) {
  uint64_t value = 0;
  for (int i = 0; i < 8; ++i) value = value << 8 | uint8_t(p[i]);
  return value;
}
void AppendU32(std::string& out, uint32_t value) {
  char bytes[4];
  std::memcpy(bytes, &value, 4);
  out.append(bytes, 4);
}
uint32_t ReadU32(const char* p) {
  uint32_t value;
  std::memcpy(&value, p, 4);
  return value;
}

// [deleted:u8][name size:u32][key size:u32][name][key][value]. The collection is stored by name,
// which stays valid when the schema reorders collections.
void EncodeChange(std::string& out, std::string_view name, const RocksDbBackend::Write& write) {
  out.clear();
  out.push_back(write.deleted ? 1 : 0);
  AppendU32(out, static_cast<uint32_t>(name.size()));
  AppendU32(out, static_cast<uint32_t>(write.key.size()));
  out.append(name);
  out.append(reinterpret_cast<const char*>(write.key.data()), write.key.size());
  out.append(reinterpret_cast<const char*>(write.value.data()), write.value.size());
}

// All reads are served from memory, so RocksDB needs little beyond its write path.
rocksdb::ColumnFamilyOptions FamilyOptions() {
  rocksdb::ColumnFamilyOptions options;
  options.write_buffer_size = 16 << 20;
  options.max_write_buffer_number = 2;
  rocksdb::BlockBasedTableOptions table;
  table.block_cache = rocksdb::NewLRUCache(8 << 20);
  table.block_size = 16 << 10;
  options.table_factory.reset(rocksdb::NewBlockBasedTableFactory(table));
  return options;
}

}  // namespace

absl::StatusOr<std::unique_ptr<RocksDbBackend>> RocksDbBackend::Open(
    const std::string& path, std::vector<std::string> collections, bool change_log) {
  rocksdb::DBOptions db_options;
  db_options.create_if_missing = true;
  db_options.create_missing_column_families = true;
  db_options.wal_recovery_mode = rocksdb::WALRecoveryMode::kPointInTimeRecovery;
  db_options.db_write_buffer_size = 64 << 20;

  std::vector<std::string> names;
  if (!rocksdb::DB::ListColumnFamilies(db_options, path, &names).ok()) {
    names = {rocksdb::kDefaultColumnFamilyName};  // A new database.
  }
  const auto add_family = [&names](std::string name) {
    if (std::find(names.begin(), names.end(), name) == names.end())
      names.push_back(std::move(name));
  };
  add_family(std::string(kMetaFamily));
  add_family(std::string(kChangesFamily));
  for (const std::string& name : collections) {
    if (!name.empty()) add_family(std::string(kCollectionPrefix) + name);
  }

  const rocksdb::ColumnFamilyOptions family_options = FamilyOptions();
  std::vector<rocksdb::ColumnFamilyDescriptor> descriptors;
  for (const std::string& name : names) descriptors.emplace_back(name, family_options);

  std::unique_ptr<RocksDbBackend> backend(new RocksDbBackend());
  rocksdb::DB* db = nullptr;
  rocksdb::Status opened =
      rocksdb::DB::Open(db_options, path, descriptors, &backend->_handles, &db);
  if (!opened.ok()) return ToStatus(opened);
  backend->_db.reset(db);

  const auto handle_of = [&](std::string_view name) {
    return backend->_handles[std::find(names.begin(), names.end(), name) - names.begin()];
  };
  backend->_meta = handle_of(kMetaFamily);
  backend->_changes = handle_of(kChangesFamily);
  for (const std::string& name : collections) {
    backend->_collections.push_back(
        name.empty() ? nullptr : handle_of(std::string(kCollectionPrefix) + name));
  }
  backend->_names = std::move(collections);
  backend->_change_log = change_log;

  std::string last_seq;
  rocksdb::Status read =
      db->Get(rocksdb::ReadOptions(), backend->_meta, rocksdb::Slice(kLastSeqKey), &last_seq);
  if (read.ok() && last_seq.size() == 8) {
    backend->_last_seq = ReadBigEndian64(last_seq.data());
  } else if (!read.IsNotFound()) {
    return read.ok() ? absl::DataLossError("Malformed last_seq") : ToStatus(read);
  }
  return backend;
}

RocksDbBackend::~RocksDbBackend() {
  if (_db == nullptr) return;
  for (auto* handle : _handles) (void)_db->DestroyColumnFamilyHandle(handle);
  (void)_db->Close();
}

absl::Status RocksDbBackend::Load(
    size_t collection,
    const std::function<void(BytesConstView key, BytesConstView value)>& fn) const {
  rocksdb::ReadOptions options;
  options.fill_cache = false;
  options.readahead_size = 2 << 20;
  std::unique_ptr<rocksdb::Iterator> it(_db->NewIterator(options, _collections[collection]));
  for (it->SeekToFirst(); it->Valid(); it->Next()) fn(AsBytes(it->key()), AsBytes(it->value()));
  return ToStatus(it->status());
}

absl::Status RocksDbBackend::Commit(uint64_t seq, std::span<const Write> writes, bool sync) {
  rocksdb::WriteBatch batch;
  std::string encoded;
  std::string change_key;
  uint32_t index = 0;
  for (const Write& write : writes) {
    auto* family = _collections[write.collection];
    rocksdb::Status added = write.deleted
                                ? batch.Delete(family, AsSlice(write.key))
                                : batch.Put(family, AsSlice(write.key), AsSlice(write.value));
    if (!added.ok()) return ToStatus(added);
    if (_change_log) {
      change_key.clear();
      AppendBigEndian64(change_key, seq);
      AppendBigEndian64(change_key, index++);
      EncodeChange(encoded, _names[write.collection], write);
      added = batch.Put(_changes, change_key, encoded);
      if (!added.ok()) return ToStatus(added);
    }
  }
  encoded.clear();
  AppendBigEndian64(encoded, seq);
  rocksdb::Status added = batch.Put(_meta, rocksdb::Slice(kLastSeqKey), encoded);
  if (!added.ok()) return ToStatus(added);

  rocksdb::WriteOptions options;
  options.sync = sync;
  return ToStatus(_db->Write(options, &batch));
}

absl::Status RocksDbBackend::SyncWal() {
  return ToStatus(_db->SyncWAL());
}

absl::Status RocksDbBackend::ReadChanges(
    uint64_t after_seq, const std::function<bool(uint64_t seq, const Write&)>& fn) const {
  std::string start;
  AppendBigEndian64(start, after_seq + 1);
  std::unique_ptr<rocksdb::Iterator> it(_db->NewIterator(rocksdb::ReadOptions(), _changes));
  for (it->Seek(start); it->Valid(); it->Next()) {
    const rocksdb::Slice key = it->key();
    const rocksdb::Slice value = it->value();
    constexpr size_t kHeader = 9;
    if (key.size() != 16 || value.size() < kHeader) return absl::DataLossError("Malformed change");
    const uint32_t name_size = ReadU32(value.data() + 1);
    const uint32_t key_size = ReadU32(value.data() + 5);
    if (uint64_t(kHeader) + name_size + key_size > value.size()) {
      return absl::DataLossError("Malformed change");
    }
    const std::string_view name(value.data() + kHeader, name_size);
    const auto found = std::find(_names.begin(), _names.end(), name);
    if (found == _names.end()) continue;  // A collection that left the schema.
    const auto* payload = reinterpret_cast<const uint8_t*>(value.data()) + kHeader + name_size;
    const Write write{
        static_cast<size_t>(found - _names.begin()), value[0] != 0,
        BytesConstView(payload, key_size),
        BytesConstView(payload + key_size, value.size() - kHeader - name_size - key_size)};
    if (!fn(ReadBigEndian64(key.data()), write)) return absl::OkStatus();
  }
  return ToStatus(it->status());
}

absl::Status RocksDbBackend::TruncateChanges(uint64_t up_to_seq) {
  std::string begin, end;
  AppendBigEndian64(begin, 0);
  AppendBigEndian64(end, up_to_seq + 1);
  return ToStatus(_db->DeleteRange(rocksdb::WriteOptions(), _changes, begin, end));
}

}  // namespace gendb
