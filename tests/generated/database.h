// AUTO GENERATED. DO NOT EDIT.
//
#pragma once
#include <cstdint>
#include <mutex>
#include <shared_mutex>

#include "Account.h"
#include "Position.h"
#include "absl/status/status.h"
#include "gendb/layered_storage.h"
#include "gendb/message_base.h"
#include "gendb/message_patch.h"

namespace gendb::tests {

// Forward declarations.
class Guard;
class ScopedWrite;

enum CollectionId {
  AccountCollId = 0,
  PositionCollId = 1,
};

// Collection keys getters.
inline std::array<uint8_t, sizeof(int)> ToAccountKey(int account_id) {
  std::array<uint8_t, sizeof(int)> key_raw;
  WriteScalarRaw(key_raw.data(), account_id);
  return key_raw;
}
inline BytesConstView ToAccountKey(Account account) {
  return account.FieldRaw(Account::AccountId);
}
inline std::array<uint8_t, sizeof(int)> ToPositionKey(int position_id) {
  std::array<uint8_t, sizeof(int)> key_raw;
  WriteScalarRaw(key_raw.data(), position_id);
  return key_raw;
}
inline BytesConstView ToPositionKey(Position position) {
  return position.FieldRaw(Position::PositionId);
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
  Storage _storage;
};

class Guard {
 public:
  absl::Status GetAccount(int account_id, Account& account) const;
  absl::Status GetPosition(int position_id, Position& position) const;
  ~Guard() = default;

 private:
  friend class Db;
  Guard(const Db& db, std::shared_lock<std::shared_mutex> lock)
      : _db(db),
        _lock(std::move(lock)),
        _layered_storage(const_cast<Storage&>(_db._storage), /*temp_storage_ptr=*/nullptr) {}

 private:
  const Db& _db;
  std::shared_lock<std::shared_mutex> _lock;
  const gendb::LayeredStorage _layered_storage;
};

class ScopedWrite {
 public:
  absl::Status GetAccount(int account_id, Account& account) const;
  absl::Status PutAccount(int account_id, std::vector<uint8_t> obj);
  absl::Status UpdateAccount(int account_id, const MessagePatch& update);
  absl::Status GetPosition(int position_id, Position& position) const;
  absl::Status PutPosition(int position_id, std::vector<uint8_t> obj);
  absl::Status UpdatePosition(int position_id, const MessagePatch& update);

  void Commit();
  ~ScopedWrite() = default;

 private:
  friend class Db;
  ScopedWrite(Db& db, std::unique_lock<std::mutex> lock)
      : _db(const_cast<Db&>(db)),
        _lock(std::move(lock)),
        _layered_storage(_db._storage, &_temp_storage) {}

 private:
  Db& _db;
  std::unique_lock<std::mutex> _lock;
  gendb::Storage _temp_storage;
  gendb::LayeredStorage _layered_storage;
};

}  // namespace gendb::tests
