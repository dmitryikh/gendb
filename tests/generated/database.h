// AUTO GENERATED. DO NOT EDIT.
//
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "account.gendb.h"
#include "config.gendb.h"
#include "gendb/bytes.h"
#include "gendb/engine.h"
#include "gendb/key_codec.h"
#include "gendb/message.h"
#include "metadata.gendb.h"
#include "position.gendb.h"
#include "session.gendb.h"

namespace gendb::tests {

// Message types in other namespaces than this code's.
using ::gendb::MetadataValue;
using ::gendb::MetadataValueBuilder;
using ::gendb::MetadataValuePatchBuilder;

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
  gendb::internal::key_codec::EncodeTupleToView<std::tuple<gendb::MetadataType, uint32_t>>(
      {key.type, key.id}, key_raw);
  return key_raw;
}

inline std::array<uint8_t, 8> ToMetadataValueKey(MetadataValue metadata_value) {
  std::array<uint8_t, 8> key_raw;
  gendb::internal::key_codec::EncodeTupleToView<std::tuple<gendb::MetadataType, uint32_t>>(
      {metadata_value.type(), metadata_value.id()}, key_raw);
  return key_raw;
}
inline std::array<uint8_t, 8> ToAccountKey(uint64_t account_id) {
  std::array<uint8_t, 8> key_raw;
  gendb::internal::key_codec::EncodeTupleToView<std::tuple<uint64_t>>({account_id}, key_raw);
  return key_raw;
}

inline std::array<uint8_t, 8> ToAccountKey(Account account) {
  return ToAccountKey(account.account_id());
}
inline std::array<uint8_t, 4> ToPositionKey(int32_t position_id) {
  std::array<uint8_t, 4> key_raw;
  gendb::internal::key_codec::EncodeTupleToView<std::tuple<int32_t>>({position_id}, key_raw);
  return key_raw;
}

inline std::array<uint8_t, 4> ToPositionKey(Position position) {
  return ToPositionKey(position.position_id());
}
inline Bytes ToConfigKey(std::string_view config_name) {
  return gendb::internal::key_codec::EncodeTuple(std::make_tuple(config_name));
}

inline Bytes ToConfigKey(Config config) {
  return gendb::internal::key_codec::EncodeTuple(std::make_tuple(config.config_name()));
}

inline std::array<uint8_t, 8> ToSessionKey(uint64_t session_id) {
  std::array<uint8_t, 8> key_raw;
  gendb::internal::key_codec::EncodeTupleToView<std::tuple<uint64_t>>({session_id}, key_raw);
  return key_raw;
}

inline std::array<uint8_t, 8> ToSessionKey(Session session) {
  return ToSessionKey(session.session_id());
}

enum IndexId {
  AccountByAgeIndexId = 0,
  PositionByAccountIdIndexId = 1,
  PositionByAccountAndInstrumentIndexId = 2,
  AccountByAddressIndexId = 3,
};

using DbOptions = gendb::EngineOptions;

// What a commit did to one record, valid only during the callback. Whole messages before and after
// are available, and the *_changed() methods compare one field across them. On insert or delete
// the missing side reads as an empty message. While catching up with SubscribeFrom, there is no
// `before`: kUpsert gives the current state, and kDelete only the key.
class AccountChange {
 public:
  explicit AccountChange(const gendb::Change& change)
      : _change(&change),
        _before(change.before.value_or(gendb::BytesConstView{})),
        _after(change.after.value_or(gendb::BytesConstView{})) {}

  gendb::ChangeKind kind() const { return _change->kind(); }
  gendb::BytesConstView key() const { return _change->key; }
  const Account& before() const { return _before; }
  const Account& after() const { return _after; }
  bool account_id_changed() const {
    return _before.has_account_id() != _after.has_account_id() ||
           _before.account_id() != _after.account_id();
  }
  bool trader_id_changed() const {
    return _before.has_trader_id() != _after.has_trader_id() ||
           _before.trader_id() != _after.trader_id();
  }
  bool name_changed() const {
    return _before.has_name() != _after.has_name() || _before.name() != _after.name();
  }
  bool address_changed() const {
    return _before.has_address() != _after.has_address() || _before.address() != _after.address();
  }
  bool age_changed() const {
    return _before.has_age() != _after.has_age() || _before.age() != _after.age();
  }
  bool is_active_changed() const {
    return _before.has_is_active() != _after.has_is_active() ||
           _before.is_active() != _after.is_active();
  }
  bool balance_changed() const {
    return _before.has_balance() != _after.has_balance() || _before.balance() != _after.balance();
  }
  bool config_name_changed() const {
    return _before.has_config_name() != _after.has_config_name() ||
           _before.config_name() != _after.config_name();
  }

 private:
  const gendb::Change* _change;
  Account _before;
  Account _after;
};

class PositionChange {
 public:
  explicit PositionChange(const gendb::Change& change)
      : _change(&change),
        _before(change.before.value_or(gendb::BytesConstView{})),
        _after(change.after.value_or(gendb::BytesConstView{})) {}

  gendb::ChangeKind kind() const { return _change->kind(); }
  gendb::BytesConstView key() const { return _change->key; }
  const Position& before() const { return _before; }
  const Position& after() const { return _after; }
  bool position_id_changed() const {
    return _before.has_position_id() != _after.has_position_id() ||
           _before.position_id() != _after.position_id();
  }
  bool account_id_changed() const {
    return _before.has_account_id() != _after.has_account_id() ||
           _before.account_id() != _after.account_id();
  }
  bool volume_changed() const {
    return _before.has_volume() != _after.has_volume() || _before.volume() != _after.volume();
  }
  bool instrument_changed() const {
    return _before.has_instrument() != _after.has_instrument() ||
           _before.instrument() != _after.instrument();
  }
  bool open_price_changed() const {
    return _before.has_open_price() != _after.has_open_price() ||
           _before.open_price() != _after.open_price();
  }
  bool direction_changed() const {
    return _before.has_direction() != _after.has_direction() ||
           _before.direction() != _after.direction();
  }

 private:
  const gendb::Change* _change;
  Position _before;
  Position _after;
};

class ConfigChange {
 public:
  explicit ConfigChange(const gendb::Change& change)
      : _change(&change),
        _before(change.before.value_or(gendb::BytesConstView{})),
        _after(change.after.value_or(gendb::BytesConstView{})) {}

  gendb::ChangeKind kind() const { return _change->kind(); }
  gendb::BytesConstView key() const { return _change->key; }
  const Config& before() const { return _before; }
  const Config& after() const { return _after; }
  bool config_name_changed() const {
    return _before.has_config_name() != _after.has_config_name() ||
           _before.config_name() != _after.config_name();
  }
  bool max_trade_volume_changed() const {
    return _before.has_max_trade_volume() != _after.has_max_trade_volume() ||
           _before.max_trade_volume() != _after.max_trade_volume();
  }

 private:
  const gendb::Change* _change;
  Config _before;
  Config _after;
};

class SessionChange {
 public:
  explicit SessionChange(const gendb::Change& change)
      : _change(&change),
        _before(change.before.value_or(gendb::BytesConstView{})),
        _after(change.after.value_or(gendb::BytesConstView{})) {}

  gendb::ChangeKind kind() const { return _change->kind(); }
  gendb::BytesConstView key() const { return _change->key; }
  const Session& before() const { return _before; }
  const Session& after() const { return _after; }
  bool session_id_changed() const {
    return _before.has_session_id() != _after.has_session_id() ||
           _before.session_id() != _after.session_id();
  }
  bool token_changed() const {
    return _before.has_token() != _after.has_token() || _before.token() != _after.token();
  }

 private:
  const gendb::Change* _change;
  Session _before;
  Session _after;
};

// Set the handlers of the collections to follow. All run on one thread, in commit order, so changes
// to different collections keep their order.
struct ChangeHandlers {
  std::function<void(const AccountChange&)> on_account;
  std::function<void(const PositionChange&)> on_position;
  std::function<void(const ConfigChange&)> on_config;
  std::function<void(const SessionChange&)> on_session;
  // After the changes of each commit that had any, for acting on a commit as a whole. Store the
  // position to resume from later with SubscribeFrom.
  std::function<void(const gendb::ChangePosition& position)> on_commit;
};

class Db {
 public:
  // In memory only.
  explicit Db(std::shared_ptr<gendb::EngineObserver> observer = nullptr);
  // Recovers committed state from disk.
  static absl::StatusOr<std::unique_ptr<Db>> Open(const DbOptions& options);

  // A consistent view of committed state. It never blocks a commit, or is blocked by one.
  Snapshot CreateSnapshot() const;
  // The single writer: blocks until any other writer is done.
  ScopedWrite CreateWriter();
  // Delivers changes committed after this call until the returned handle is destroyed.
  gendb::Subscription Subscribe(ChangeHandlers handlers, gendb::SubscribeOptions options = {});
  // The same, starting with what changed since `position`. See gendb::Engine::SubscribeFrom.
  absl::StatusOr<gendb::Subscription> SubscribeFrom(const gendb::ChangePosition& position,
                                                    ChangeHandlers handlers,
                                                    gendb::SubscribeOptions options = {});
  // Durability controls, stats, and the handle replication attaches to.
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
  // Entries with these values, ordered by what remains of the index and the primary key.
  gendb::IndexIterator<Account> GetAccountByAgeEqual(int32_t age,
                                                     gendb::ScanOptions options = {}) const;
  // Entries age in [min, max); a missing bound leaves that side open.
  gendb::IndexIterator<Account> GetAccountByAgeRange(std::optional<int32_t> min_age,
                                                     std::optional<int32_t> max_age,
                                                     gendb::ScanOptions options = {}) const;
  // Entries with these values, ordered by what remains of the index and the primary key.
  gendb::IndexIterator<Position> GetPositionByAccountIdEqual(int32_t account_id,
                                                             gendb::ScanOptions options = {}) const;
  // Entries account_id in [min, max); a missing bound leaves that side open.
  gendb::IndexIterator<Position> GetPositionByAccountIdRange(std::optional<int32_t> min_account_id,
                                                             std::optional<int32_t> max_account_id,
                                                             gendb::ScanOptions options = {}) const;
  // Entries with these values, ordered by what remains of the index and the primary key.
  gendb::IndexIterator<Position> GetPositionByAccountAndInstrumentEqual(
      int32_t account_id, gendb::ScanOptions options = {}) const;
  // Entries with these values, ordered by what remains of the index and the primary key.
  gendb::IndexIterator<Position> GetPositionByAccountAndInstrumentEqual(
      int32_t account_id, std::string_view instrument, gendb::ScanOptions options = {}) const;
  // Entries account_id in [min, max); a missing bound leaves that side open.
  gendb::IndexIterator<Position> GetPositionByAccountAndInstrumentRange(
      std::optional<int32_t> min_account_id, std::optional<int32_t> max_account_id,
      gendb::ScanOptions options = {}) const;
  // Entries with these values and instrument in [min, max); a missing bound leaves that side open.
  gendb::IndexIterator<Position> GetPositionByAccountAndInstrumentRange(
      int32_t account_id, std::optional<std::string_view> min_instrument,
      std::optional<std::string_view> max_instrument, gendb::ScanOptions options = {}) const;
  // Entries with these values, ordered by what remains of the index and the primary key.
  gendb::IndexIterator<Account> GetAccountByAddressEqual(std::string_view address,
                                                         gendb::ScanOptions options = {}) const;
  // Entries address in [min, max); a missing bound leaves that side open.
  gendb::IndexIterator<Account> GetAccountByAddressRange(
      std::optional<std::string_view> min_address, std::optional<std::string_view> max_address,
      gendb::ScanOptions options = {}) const;
  // The one record with these values.
  absl::Status GetAccountByAddress(std::string_view address, Account& out) const;

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
  // The key comes from the message, which must have its primary key fields set.
  absl::Status PutMetadataValue(gendb::BytesConstView metadata_value);
  // A patch cannot change the primary key: that is another record.
  absl::Status UpdateMetadataValue(const MetadataValueKey& key, const gendb::MessagePatch& update);
  absl::Status DeleteMetadataValue(const MetadataValueKey& key);
  // Includes this transaction's changes. Writing or committing during the scan ends its validity.
  gendb::ScanIterator<MetadataValue> ScanMetadataValue() const;

 public:
  absl::Status GetAccount(uint64_t account_id, Account& account) const;
  // The key comes from the message, which must have its primary key fields set.
  absl::Status PutAccount(gendb::BytesConstView account);
  // A patch cannot change the primary key: that is another record.
  absl::Status UpdateAccount(uint64_t account_id, const gendb::MessagePatch& update);
  absl::Status DeleteAccount(uint64_t account_id);
  // Includes this transaction's changes. Writing or committing during the scan ends its validity.
  gendb::ScanIterator<Account> ScanAccount() const;
  absl::Status GetPosition(int32_t position_id, Position& position) const;
  // The key comes from the message, which must have its primary key fields set.
  absl::Status PutPosition(gendb::BytesConstView position);
  // A patch cannot change the primary key: that is another record.
  absl::Status UpdatePosition(int32_t position_id, const gendb::MessagePatch& update);
  absl::Status DeletePosition(int32_t position_id);
  // Includes this transaction's changes. Writing or committing during the scan ends its validity.
  gendb::ScanIterator<Position> ScanPosition() const;
  absl::Status GetConfig(std::string_view config_name, Config& config) const;
  // The key comes from the message, which must have its primary key fields set.
  absl::Status PutConfig(gendb::BytesConstView config);
  // A patch cannot change the primary key: that is another record.
  absl::Status UpdateConfig(std::string_view config_name, const gendb::MessagePatch& update);
  absl::Status DeleteConfig(std::string_view config_name);
  // Includes this transaction's changes. Writing or committing during the scan ends its validity.
  gendb::ScanIterator<Config> ScanConfig() const;
  absl::Status GetSession(uint64_t session_id, Session& session) const;
  // The key comes from the message, which must have its primary key fields set.
  absl::Status PutSession(gendb::BytesConstView session);
  // A patch cannot change the primary key: that is another record.
  absl::Status UpdateSession(uint64_t session_id, const gendb::MessagePatch& update);
  absl::Status DeleteSession(uint64_t session_id);
  // Includes this transaction's changes. Writing or committing during the scan ends its validity.
  gendb::ScanIterator<Session> ScanSession() const;

 public:
  // Entries with these values, ordered by what remains of the index and the primary key.
  gendb::IndexIterator<Account> GetAccountByAgeEqual(int32_t age,
                                                     gendb::ScanOptions options = {}) const;
  // Entries age in [min, max); a missing bound leaves that side open.
  gendb::IndexIterator<Account> GetAccountByAgeRange(std::optional<int32_t> min_age,
                                                     std::optional<int32_t> max_age,
                                                     gendb::ScanOptions options = {}) const;
  // Entries with these values, ordered by what remains of the index and the primary key.
  gendb::IndexIterator<Position> GetPositionByAccountIdEqual(int32_t account_id,
                                                             gendb::ScanOptions options = {}) const;
  // Entries account_id in [min, max); a missing bound leaves that side open.
  gendb::IndexIterator<Position> GetPositionByAccountIdRange(std::optional<int32_t> min_account_id,
                                                             std::optional<int32_t> max_account_id,
                                                             gendb::ScanOptions options = {}) const;
  // Entries with these values, ordered by what remains of the index and the primary key.
  gendb::IndexIterator<Position> GetPositionByAccountAndInstrumentEqual(
      int32_t account_id, gendb::ScanOptions options = {}) const;
  // Entries with these values, ordered by what remains of the index and the primary key.
  gendb::IndexIterator<Position> GetPositionByAccountAndInstrumentEqual(
      int32_t account_id, std::string_view instrument, gendb::ScanOptions options = {}) const;
  // Entries account_id in [min, max); a missing bound leaves that side open.
  gendb::IndexIterator<Position> GetPositionByAccountAndInstrumentRange(
      std::optional<int32_t> min_account_id, std::optional<int32_t> max_account_id,
      gendb::ScanOptions options = {}) const;
  // Entries with these values and instrument in [min, max); a missing bound leaves that side open.
  gendb::IndexIterator<Position> GetPositionByAccountAndInstrumentRange(
      int32_t account_id, std::optional<std::string_view> min_instrument,
      std::optional<std::string_view> max_instrument, gendb::ScanOptions options = {}) const;
  // Entries with these values, ordered by what remains of the index and the primary key.
  gendb::IndexIterator<Account> GetAccountByAddressEqual(std::string_view address,
                                                         gendb::ScanOptions options = {}) const;
  // Entries address in [min, max); a missing bound leaves that side open.
  gendb::IndexIterator<Account> GetAccountByAddressRange(
      std::optional<std::string_view> min_address, std::optional<std::string_view> max_address,
      gendb::ScanOptions options = {}) const;
  // The one record with these values.
  absl::Status GetAccountByAddress(std::string_view address, Account& out) const;

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
