// AUTO GENERATED. DO NOT EDIT.
//
#pragma once
#include <cstdint>
#include <mutex>
#include <shared_mutex>

#include "Account.h"
#include "Config.h"
#include "Position.h"
#include "absl/status/status.h"
#include "gendb/bytes.h"
#include "gendb/index.h"
#include "gendb/iterator.h"
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
  ConfigCollId = 2,
};

// Collection keys getters.
inline std::array<uint8_t, sizeof(uint64_t)> ToAccountKey(uint64_t account_id) {
  std::array<uint8_t, sizeof(uint64_t)> key_raw;
  WriteScalarRaw(key_raw.data(), account_id);
  return key_raw;
}
inline BytesConstView ToAccountKey(Account account) {
  return account.FieldRaw(Account::AccountId);
}
inline std::array<uint8_t, sizeof(int32_t)> ToPositionKey(int32_t position_id) {
  std::array<uint8_t, sizeof(int32_t)> key_raw;
  WriteScalarRaw(key_raw.data(), position_id);
  return key_raw;
}
inline BytesConstView ToPositionKey(Position position) {
  return position.FieldRaw(Position::PositionId);
}
inline BytesConstView ToConfigKey(std::string_view config_name) {
  return {reinterpret_cast<const uint8_t*>(config_name.data()), config_name.size()};
}
inline BytesConstView ToConfigKey(Config config) {
  return config.FieldRaw(Config::ConfigName);
}

struct Indices {
  using AccountByAgeIndexType =
      gendb::Index</*age*/ int32_t, std::array<uint8_t, sizeof(uint64_t)>>;
  AccountByAgeIndexType account_by_age;

  void MergeTempIndices(Indices&& temp_indices) {
    account_by_age.MergeTempIndex(std::move(temp_indices.account_by_age));
  }
};

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
  Indices _indices;
};

class Guard {
 public:
  absl::Status GetAccount(uint64_t account_id, Account& account) const;
  absl::Status GetPosition(int32_t position_id, Position& position) const;
  absl::Status GetConfig(std::string_view config_name, Config& config) const;

  gendb::Iterator<Account> GetAccountByAgeRange(int32_t min_age, int32_t max_age) const;
  gendb::Iterator<Account> GetAccountByAgeEqual(int32_t age) const;

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
  absl::Status GetAccount(uint64_t account_id, Account& account) const;
  absl::Status PutAccount(uint64_t account_id, std::vector<uint8_t> account);
  absl::Status UpdateAccount(uint64_t account_id, const MessagePatch& update);
  absl::Status GetPosition(int32_t position_id, Position& position) const;
  absl::Status PutPosition(int32_t position_id, std::vector<uint8_t> position);
  absl::Status UpdatePosition(int32_t position_id, const MessagePatch& update);
  absl::Status GetConfig(std::string_view config_name, Config& config) const;
  absl::Status PutConfig(std::string_view config_name, std::vector<uint8_t> config);
  absl::Status UpdateConfig(std::string_view config_name, const MessagePatch& update);

  gendb::Iterator<Account> GetAccountByAgeRange(int32_t min_age, int32_t max_age) const;
  gendb::Iterator<Account> GetAccountByAgeEqual(int32_t age) const;

  void Commit();
  ~ScopedWrite() = default;

 private:
  friend class Db;
  ScopedWrite(Db& db, std::unique_lock<std::mutex> lock)
      : _db(const_cast<Db&>(db)),
        _lock(std::move(lock)),
        _layered_storage(_db._storage, &_temp_storage) {}

  void MaybeUpdateAccountByAgeIndex(std::array<uint8_t, sizeof(uint64_t)> key,
                                    gendb::BytesConstView account_buffer,
                                    const MessagePatch* update);

 private:
  Db& _db;
  std::unique_lock<std::mutex> _lock;
  gendb::Storage _temp_storage;
  Indices _temp_indices;
  gendb::LayeredStorage _layered_storage;
};

}  // namespace gendb::tests
