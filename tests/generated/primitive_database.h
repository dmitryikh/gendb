// AUTO GENERATED. DO NOT EDIT.
//
#pragma once
#include <cstdint>
#include <mutex>
#include <shared_mutex>

#include "absl/status/status.h"
#include "gendb/bytes.h"
#include "gendb/key_codec.h"
#include "gendb/layered_storage.h"
#include "gendb/message_patch.h"
#include "messageA.fbs.h"
#include "metadata.fbs.h"

namespace gendb::tests::primitive {

// Forward declarations.
class Guard;
class ScopedWrite;

enum CollectionId {
  MetadataValueCollId = 0,
  MessageACollId = 1,
};

// Collection keys getters.
struct MetadataValueKey {
  gendb::MetadataType type;
  uint32_t id;
};

inline std::array<uint8_t, 8> ToMetadataValueKey(const MetadataValueKey& key) {
  std::array<uint8_t, 8> key_raw;
  internal::key_codec::EncodeTupleToView<std::tuple<gendb::MetadataType, uint32_t>>(
      {key.type, key.id}, key_raw);
  return key_raw;
}

inline std::array<uint8_t, 8> ToMetadataValueKey(MetadataValue metadata_value) {
  std::array<uint8_t, 8> key_raw;
  internal::key_codec::EncodeTupleToView<std::tuple<gendb::MetadataType, uint32_t>>(
      {metadata_value.type(), metadata_value.id()}, key_raw);
  return key_raw;
}
inline std::array<uint8_t, 4> ToMessageAKey(gendb::tests::primitive::KeyEnum key) {
  std::array<uint8_t, 4> key_raw;
  internal::key_codec::EncodeTupleToView<std::tuple<gendb::tests::primitive::KeyEnum>>({key},
                                                                                       key_raw);
  return key_raw;
}

inline std::array<uint8_t, 4> ToMessageAKey(MessageA message_a) {
  return ToMessageAKey(message_a.key());
}

class Db {
 public:
  Guard SharedLock() const;
  ScopedWrite CreateWriter();

 private:
  friend class Guard;
  friend class ScopedWrite;

  std::mutex _writer_mutex;
  mutable std::shared_mutex _reader_mutex;
  MemoryStorage _storage;
};

class Guard {
 public:
  absl::Status GetMetadataValue(const MetadataValueKey& key, MetadataValue& metadata_value) const;
  absl::Status GetMessageA(gendb::tests::primitive::KeyEnum key, MessageA& message_a) const;
  ~Guard() = default;

 private:
  friend class Db;
  Guard(const Db& db, std::shared_lock<std::shared_mutex> lock)
      : _db(db),
        _lock(std::move(lock)),
        _layered_storage(const_cast<MemoryStorage&>(_db._storage), /*temp_storage_ptr=*/nullptr) {}

 private:
  const Db& _db;
  std::shared_lock<std::shared_mutex> _lock;
  const gendb::LayeredStorage _layered_storage;
};

class ScopedWrite {
 private:
  absl::Status GetMetadataValue(const MetadataValueKey& key, MetadataValue& metadata_value) const;
  absl::Status PutMetadataValue(const MetadataValueKey& key, std::vector<uint8_t> metadata_value);
  absl::Status UpdateMetadataValue(const MetadataValueKey& key, const MessagePatch& update);

 public:
  absl::Status GetMessageA(gendb::tests::primitive::KeyEnum key, MessageA& message_a) const;
  absl::Status PutMessageA(gendb::tests::primitive::KeyEnum key, std::vector<uint8_t> message_a);
  absl::Status UpdateMessageA(gendb::tests::primitive::KeyEnum key, const MessagePatch& update);

 public:
  void Commit();
  ~ScopedWrite() = default;

 private:
  friend class Db;
  ScopedWrite(Db& db, std::unique_lock<std::mutex> lock)
      : _db(const_cast<Db&>(db)),
        _lock(std::move(lock)),
        _layered_storage(_db._storage, &_temp_storage) {}

  // Index update helpers

 private:
  Db& _db;
  std::unique_lock<std::mutex> _lock;
  gendb::MemoryStorage _temp_storage;
  gendb::LayeredStorage _layered_storage;
};

}  // namespace gendb::tests::primitive
