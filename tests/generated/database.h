// AUTO GENERATED. DO NOT EDIT.
//
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "account.fbs.h"
#include "config.fbs.h"
#include "gendb/bytes.h"
#include "gendb/engine.h"
#include "gendb/key_codec.h"
#include "gendb/message.h"
#include "metadata.fbs.h"
#include "position.fbs.h"
#include "session.fbs.h"

namespace gendb::tests {

// Forward declarations.
class Snapshot;
class ScopedWrite;

enum class SequenceMetadataId : uint32_t {
  AccountIdSequence = 0,
  PositionIdSequence = 1,
};

enum CollectionId {
  MetadataValueCollId = 0,
  AccountCollId = 1,
  PositionCollId = 2,
  ConfigCollId = 3,
  SessionCollId = 4,
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
inline std::array<uint8_t, 8> ToAccountKey(uint64_t account_id) {
  std::array<uint8_t, 8> key_raw;
  internal::key_codec::EncodeTupleToView<std::tuple<uint64_t>>({account_id}, key_raw);
  return key_raw;
}

inline std::array<uint8_t, 8> ToAccountKey(Account account) {
  return ToAccountKey(account.account_id());
}
inline std::array<uint8_t, 4> ToPositionKey(int32_t position_id) {
  std::array<uint8_t, 4> key_raw;
  internal::key_codec::EncodeTupleToView<std::tuple<int32_t>>({position_id}, key_raw);
  return key_raw;
}

inline std::array<uint8_t, 4> ToPositionKey(Position position) {
  return ToPositionKey(position.position_id());
}
inline Bytes ToConfigKey(std::string_view config_name) {
  return internal::key_codec::EncodeTuple(std::make_tuple(config_name));
}

inline Bytes ToConfigKey(Config config) {
  return internal::key_codec::EncodeTuple(std::make_tuple(config.config_name()));
}

inline std::array<uint8_t, 8> ToSessionKey(uint64_t session_id) {
  std::array<uint8_t, 8> key_raw;
  internal::key_codec::EncodeTupleToView<std::tuple<uint64_t>>({session_id}, key_raw);
  return key_raw;
}

inline std::array<uint8_t, 8> ToSessionKey(Session session) {
  return ToSessionKey(session.session_id());
}

enum IndexId {
  AccountByAgeIndexId = 0,
  PositionByAccountIdIndexId = 1,
};

struct DbOptions {
  // RocksDB directory, created if missing.
  std::string path;
  // Whether Commit returns only after an fsync.
  bool sync_commits = true;
  // Keep every change in a durable log, for engine().ReadChanges.
  bool change_log = false;
};

class Db {
 public:
  // In memory only.
  Db();
  // Recovers committed state from disk.
  static absl::StatusOr<std::unique_ptr<Db>> Open(const DbOptions& options);

  // A consistent view of committed state. It never blocks a commit, or is blocked by one.
  Snapshot CreateSnapshot() const;
  // The single writer: blocks until any other writer is done.
  ScopedWrite CreateWriter();
  // Change listeners, the durable change log, and stats.
  gendb::Engine& engine() { return *_engine; }

 private:
  explicit Db(std::unique_ptr<gendb::Engine> engine) : _engine(std::move(engine)) {}

  std::unique_ptr<gendb::Engine> _engine;
};

// Messages returned by a snapshot stay valid until it is destroyed. Db must outlive it.
class Snapshot {
 public:
  absl::Status GetMetadataValue(const MetadataValueKey& key, MetadataValue& metadata_value) const;
  gendb::ScanIterator<MetadataValue> ScanMetadataValue() const;
  absl::Status GetAccount(uint64_t account_id, Account& account) const;
  gendb::ScanIterator<Account> ScanAccount() const;
  absl::Status GetPosition(int32_t position_id, Position& position) const;
  gendb::ScanIterator<Position> ScanPosition() const;
  absl::Status GetConfig(std::string_view config_name, Config& config) const;
  gendb::ScanIterator<Config> ScanConfig() const;
  absl::Status GetSession(uint64_t session_id, Session& session) const;
  gendb::ScanIterator<Session> ScanSession() const;
  // Index entries in [min_age, max_age), ordered by age.
  gendb::IndexIterator<Account> GetAccountByAgeRange(int32_t min_age, int32_t max_age) const;
  gendb::IndexIterator<Account> GetAccountByAgeEqual(int32_t age) const;
  // Index entries in [min_account_id, max_account_id), ordered by account_id.
  gendb::IndexIterator<Position> GetPositionByAccountIdRange(int32_t min_account_id,
                                                             int32_t max_account_id) const;
  gendb::IndexIterator<Position> GetPositionByAccountIdEqual(int32_t account_id) const;

 private:
  friend class Db;
  explicit Snapshot(const gendb::Engine& engine) : _snapshot(engine) {}

  gendb::EngineSnapshot _snapshot;
};

// A transaction: reads its own writes, and Commit applies all of them atomically. Dropping it
// without a commit rolls it back. Messages it returns are invalidated by the next write to that
// key and by Commit.
class ScopedWrite {
 private:
  absl::Status GetMetadataValue(const MetadataValueKey& key, MetadataValue& metadata_value) const;
  absl::Status PutMetadataValue(const MetadataValueKey& key, gendb::BytesConstView metadata_value);
  absl::Status UpdateMetadataValue(const MetadataValueKey& key, const gendb::MessagePatch& update);
  absl::Status DeleteMetadataValue(const MetadataValueKey& key);

 public:
  absl::Status GetAccount(uint64_t account_id, Account& account) const;
  absl::Status PutAccount(uint64_t account_id, gendb::BytesConstView account);
  absl::Status UpdateAccount(uint64_t account_id, const gendb::MessagePatch& update);
  absl::Status DeleteAccount(uint64_t account_id);
  absl::Status GetPosition(int32_t position_id, Position& position) const;
  absl::Status PutPosition(int32_t position_id, gendb::BytesConstView position);
  absl::Status UpdatePosition(int32_t position_id, const gendb::MessagePatch& update);
  absl::Status DeletePosition(int32_t position_id);
  absl::Status GetConfig(std::string_view config_name, Config& config) const;
  absl::Status PutConfig(std::string_view config_name, gendb::BytesConstView config);
  absl::Status UpdateConfig(std::string_view config_name, const gendb::MessagePatch& update);
  absl::Status DeleteConfig(std::string_view config_name);
  absl::Status GetSession(uint64_t session_id, Session& session) const;
  absl::Status PutSession(uint64_t session_id, gendb::BytesConstView session);
  absl::Status UpdateSession(uint64_t session_id, const gendb::MessagePatch& update);
  absl::Status DeleteSession(uint64_t session_id);

 public:
  gendb::IndexIterator<Account> GetAccountByAgeRange(int32_t min_age, int32_t max_age) const;
  gendb::IndexIterator<Account> GetAccountByAgeEqual(int32_t age) const;
  gendb::IndexIterator<Position> GetPositionByAccountIdRange(int32_t min_account_id,
                                                             int32_t max_account_id) const;
  gendb::IndexIterator<Position> GetPositionByAccountIdEqual(int32_t account_id) const;

  // Rolls back with the transaction, so committed IDs have no gaps.
  absl::Status NextAccountIdSequence(uint64_t& next_id);
  // Rolls back with the transaction, so committed IDs have no gaps.
  absl::Status NextPositionIdSequence(int32_t& next_id);
  absl::Status Commit(gendb::CommitOptions options = {}) { return _txn.Commit(options); }
  void Rollback() { _txn.Rollback(); }

 private:
  friend class Db;
  explicit ScopedWrite(gendb::Engine& engine) : _txn(engine) {}

  gendb::Transaction _txn;
};

}  // namespace gendb::tests
