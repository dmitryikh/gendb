
#include <cstdint>
#include <mutex>
#include <shared_mutex>

#include "AccountFb.h"
#include "PositionFb.h"
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
inline std::array<uint8_t, sizeof(uint32_t)> ToAccountKey(uint32_t account_id) {
  std::array<uint8_t, sizeof(uint32_t)> key_raw;
  WriteScalarRaw(key_raw.data(), account_id);
  return key_raw;
}

inline BytesConstView ToAccountKey(AccountFb account) {
  return account.FieldRaw(AccountFb::AccountId);
}

inline std::array<uint8_t, sizeof(uint32_t)> ToPositionKey(uint32_t position_id) {
  std::array<uint8_t, sizeof(uint32_t)> key_raw;
  WriteScalarRaw(key_raw.data(), position_id);
  return key_raw;
}

inline BytesConstView ToPositionKey(PositionFb position) {
  return position.FieldRaw(PositionFb::PositionId);
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
  absl::Status GetAccount(uint32_t account_id, AccountFb& account) const;
  absl::Status GetPosition(uint32_t position_id, PositionFb& position) const;

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
  absl::Status GetAccount(uint32_t account_id, AccountFb& account) const;
  absl::Status GetPosition(uint32_t position_id, PositionFb& position) const;

  absl::Status PutAccount(uint32_t account_id, std::vector<uint8_t> account);
  absl::Status PutPosition(uint32_t position_id, std::vector<uint8_t> position);

  absl::Status UpdateAccount(uint32_t account_id, const MessagePatch& update);
  absl::Status UpdatePosition(uint32_t position_id, const MessagePatch& update);

  //   absl::Status DeleteAccount(uint32_t account_id);
  //   absl::Status DeletePosition(uint32_t position_id);

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