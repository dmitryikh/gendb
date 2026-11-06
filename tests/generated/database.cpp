// AUTO GENERATED. DO NOT EDIT.
//
#include "database.h"

#include <cstdint>

#include "absl/status/status.h"
#include "account.gendb.h"
#include "config.gendb.h"
#include "gendb/status.h"
#include "metadata.gendb.h"
#include "position.gendb.h"
#include "session.gendb.h"

namespace gendb::tests {

namespace {

gendb::EngineSchema MakeSchema() {
  gendb::EngineSchema schema;
  schema.collections.push_back({"metadata", /*ephemeral=*/false});
  schema.collections.push_back({"accounts", /*ephemeral=*/false});
  schema.collections.push_back({"positions", /*ephemeral=*/false});
  schema.collections.push_back({"configs", /*ephemeral=*/false});
  schema.collections.push_back({"sessions", /*ephemeral=*/true});
  schema.indices.push_back({"account_by_age", AccountCollId,
                            [](gendb::BytesConstView bytes, std::string& out) {
                              const Account message{bytes};
                              if (!message.has_age()) return false;
                              gendb::internal::key_codec::AppendField(message.age(), out);
                              return true;
                            },
                            /*unique=*/false});
  schema.indices.push_back({"position_by_account_id", PositionCollId,
                            [](gendb::BytesConstView bytes, std::string& out) {
                              const Position message{bytes};
                              if (!message.has_account_id()) return false;
                              gendb::internal::key_codec::AppendField(message.account_id(), out);
                              return true;
                            },
                            /*unique=*/false});
  schema.indices.push_back({"position_by_account_and_instrument", PositionCollId,
                            [](gendb::BytesConstView bytes, std::string& out) {
                              const Position message{bytes};
                              if (!message.has_account_id()) return false;
                              if (!message.has_instrument()) return false;
                              gendb::internal::key_codec::AppendField(message.account_id(), out);
                              gendb::internal::key_codec::AppendField(message.instrument(), out);
                              return true;
                            },
                            /*unique=*/false});
  schema.indices.push_back({"account_by_address", AccountCollId,
                            [](gendb::BytesConstView bytes, std::string& out) {
                              const Account message{bytes};
                              if (!message.has_address()) return false;
                              gendb::internal::key_codec::AppendField(message.address(), out);
                              return true;
                            },
                            /*unique=*/true});
  return schema;
}

// Returns `value` if present, as message type T.
template <typename T>
absl::Status ToMessage(std::optional<gendb::BytesConstView> value, T& message) {
  if (!value.has_value()) return absl::NotFoundError("Key not found");
  message = T{*value};
  return absl::OkStatus();
}

}  // namespace

Db::Db(std::shared_ptr<gendb::EngineObserver> observer)
    : _engine(std::make_unique<gendb::Engine>(MakeSchema(), std::move(observer))) {}

absl::StatusOr<std::unique_ptr<Db>> Db::Open(const DbOptions& options) {
  auto engine = gendb::Engine::Open(MakeSchema(), options);
  if (!engine.ok()) return engine.status();
  return std::unique_ptr<Db>(new Db(std::move(*engine)));
}

Snapshot Db::CreateSnapshot() const {
  return Snapshot(*_engine);
}

ScopedWrite Db::CreateWriter() {
  return ScopedWrite(*_engine);
}

namespace {

std::vector<size_t> FollowedCollections(const ChangeHandlers& handlers) {
  std::vector<size_t> collections;
  if (handlers.on_account) collections.push_back(AccountCollId);
  if (handlers.on_position) collections.push_back(PositionCollId);
  if (handlers.on_config) collections.push_back(ConfigCollId);
  if (handlers.on_session) collections.push_back(SessionCollId);
  if (collections.empty()) {
    collections.push_back(AccountCollId);
    collections.push_back(PositionCollId);
    collections.push_back(ConfigCollId);
    collections.push_back(SessionCollId);
  }
  return collections;
}

gendb::ChangeCallback ToCallback(ChangeHandlers handlers) {
  return [handlers = std::move(handlers)](const gendb::CommitEvent& event) {
    for (const gendb::Change& change : event.changes) {
      switch (change.collection) {
        case AccountCollId:
          if (handlers.on_account) handlers.on_account(AccountChange(change));
          break;
        case PositionCollId:
          if (handlers.on_position) handlers.on_position(PositionChange(change));
          break;
        case ConfigCollId:
          if (handlers.on_config) handlers.on_config(ConfigChange(change));
          break;
        case SessionCollId:
          if (handlers.on_session) handlers.on_session(SessionChange(change));
          break;
        default:
          break;
      }
    }
    if (handlers.on_commit) handlers.on_commit(event.position);
  };
}

}  // namespace

gendb::Subscription Db::Subscribe(ChangeHandlers handlers, gendb::SubscribeOptions options) {
  options.collections = FollowedCollections(handlers);
  return _engine->Subscribe(ToCallback(std::move(handlers)), std::move(options));
}

absl::StatusOr<gendb::Subscription> Db::SubscribeFrom(const gendb::ChangePosition& position,
                                                      ChangeHandlers handlers,
                                                      gendb::SubscribeOptions options) {
  options.collections = FollowedCollections(handlers);
  return _engine->SubscribeFrom(position, ToCallback(std::move(handlers)), std::move(options));
}

absl::Status Snapshot::GetMetadataValue(const MetadataValueKey& key,
                                        MetadataValue& metadata_value) const {
  return ToMessage(_snapshot.Get(MetadataValueCollId, ToMetadataValueKey(key)), metadata_value);
}

gendb::ScanIterator<MetadataValue> Snapshot::ScanMetadataValue() const {
  return gendb::ScanIterator<MetadataValue>(gendb::ScanCursor(_snapshot, MetadataValueCollId));
}

absl::Status ScopedWrite::GetMetadataValue(const MetadataValueKey& key,
                                           MetadataValue& metadata_value) const {
  return ToMessage(_txn.Get(MetadataValueCollId, ToMetadataValueKey(key)), metadata_value);
}

gendb::ScanIterator<MetadataValue> ScopedWrite::ScanMetadataValue() const {
  return gendb::ScanIterator<MetadataValue>(gendb::ScanCursor(_txn, MetadataValueCollId));
}

absl::Status ScopedWrite::PutMetadataValue(gendb::BytesConstView metadata_value) {
  // Malformed bytes would read as an empty message here, but they would also be stored and sent on.
  if (!gendb::MessageView::Verify(metadata_value))
    return absl::InvalidArgumentError("MetadataValue: not a valid message");
  const MetadataValue message{metadata_value};
  if (!message.has_type())
    return absl::InvalidArgumentError("MetadataValue: the primary key field type is not set");
  if (!message.has_id())
    return absl::InvalidArgumentError("MetadataValue: the primary key field id is not set");
  return _txn.Put(MetadataValueCollId, ToMetadataValueKey(message), metadata_value);
}

absl::Status ScopedWrite::UpdateMetadataValue(const MetadataValueKey& key,
                                              const gendb::MessagePatch& update) {
  if (update.Modifies(MetadataValue::Type))
    return absl::InvalidArgumentError(
        "MetadataValue: a patch cannot change the primary key field type");
  if (update.Modifies(MetadataValue::Id))
    return absl::InvalidArgumentError(
        "MetadataValue: a patch cannot change the primary key field id");
  return _txn.Update(MetadataValueCollId, ToMetadataValueKey(key), update);
}

absl::Status ScopedWrite::DeleteMetadataValue(const MetadataValueKey& key) {
  return _txn.Delete(MetadataValueCollId, ToMetadataValueKey(key));
}

absl::Status Snapshot::GetAccount(uint64_t account_id, Account& account) const {
  return ToMessage(_snapshot.Get(AccountCollId, ToAccountKey(account_id)), account);
}

gendb::ScanIterator<Account> Snapshot::ScanAccount() const {
  return gendb::ScanIterator<Account>(gendb::ScanCursor(_snapshot, AccountCollId));
}

absl::Status ScopedWrite::GetAccount(uint64_t account_id, Account& account) const {
  return ToMessage(_txn.Get(AccountCollId, ToAccountKey(account_id)), account);
}

gendb::ScanIterator<Account> ScopedWrite::ScanAccount() const {
  return gendb::ScanIterator<Account>(gendb::ScanCursor(_txn, AccountCollId));
}

absl::Status ScopedWrite::PutAccount(gendb::BytesConstView account) {
  // Malformed bytes would read as an empty message here, but they would also be stored and sent on.
  if (!gendb::MessageView::Verify(account))
    return absl::InvalidArgumentError("Account: not a valid message");
  const Account message{account};
  if (!message.has_account_id())
    return absl::InvalidArgumentError("Account: the primary key field account_id is not set");
  return _txn.Put(AccountCollId, ToAccountKey(message), account);
}

absl::Status ScopedWrite::UpdateAccount(uint64_t account_id, const gendb::MessagePatch& update) {
  if (update.Modifies(Account::AccountId))
    return absl::InvalidArgumentError(
        "Account: a patch cannot change the primary key field account_id");
  return _txn.Update(AccountCollId, ToAccountKey(account_id), update);
}

absl::Status ScopedWrite::DeleteAccount(uint64_t account_id) {
  return _txn.Delete(AccountCollId, ToAccountKey(account_id));
}

absl::Status Snapshot::GetPosition(int32_t position_id, Position& position) const {
  return ToMessage(_snapshot.Get(PositionCollId, ToPositionKey(position_id)), position);
}

gendb::ScanIterator<Position> Snapshot::ScanPosition() const {
  return gendb::ScanIterator<Position>(gendb::ScanCursor(_snapshot, PositionCollId));
}

absl::Status ScopedWrite::GetPosition(int32_t position_id, Position& position) const {
  return ToMessage(_txn.Get(PositionCollId, ToPositionKey(position_id)), position);
}

gendb::ScanIterator<Position> ScopedWrite::ScanPosition() const {
  return gendb::ScanIterator<Position>(gendb::ScanCursor(_txn, PositionCollId));
}

absl::Status ScopedWrite::PutPosition(gendb::BytesConstView position) {
  // Malformed bytes would read as an empty message here, but they would also be stored and sent on.
  if (!gendb::MessageView::Verify(position))
    return absl::InvalidArgumentError("Position: not a valid message");
  const Position message{position};
  if (!message.has_position_id())
    return absl::InvalidArgumentError("Position: the primary key field position_id is not set");
  return _txn.Put(PositionCollId, ToPositionKey(message), position);
}

absl::Status ScopedWrite::UpdatePosition(int32_t position_id, const gendb::MessagePatch& update) {
  if (update.Modifies(Position::PositionId))
    return absl::InvalidArgumentError(
        "Position: a patch cannot change the primary key field position_id");
  return _txn.Update(PositionCollId, ToPositionKey(position_id), update);
}

absl::Status ScopedWrite::DeletePosition(int32_t position_id) {
  return _txn.Delete(PositionCollId, ToPositionKey(position_id));
}

absl::Status Snapshot::GetConfig(std::string_view config_name, Config& config) const {
  return ToMessage(_snapshot.Get(ConfigCollId, ToConfigKey(config_name)), config);
}

gendb::ScanIterator<Config> Snapshot::ScanConfig() const {
  return gendb::ScanIterator<Config>(gendb::ScanCursor(_snapshot, ConfigCollId));
}

absl::Status ScopedWrite::GetConfig(std::string_view config_name, Config& config) const {
  return ToMessage(_txn.Get(ConfigCollId, ToConfigKey(config_name)), config);
}

gendb::ScanIterator<Config> ScopedWrite::ScanConfig() const {
  return gendb::ScanIterator<Config>(gendb::ScanCursor(_txn, ConfigCollId));
}

absl::Status ScopedWrite::PutConfig(gendb::BytesConstView config) {
  // Malformed bytes would read as an empty message here, but they would also be stored and sent on.
  if (!gendb::MessageView::Verify(config))
    return absl::InvalidArgumentError("Config: not a valid message");
  const Config message{config};
  if (!message.has_config_name())
    return absl::InvalidArgumentError("Config: the primary key field config_name is not set");
  return _txn.Put(ConfigCollId, ToConfigKey(message), config);
}

absl::Status ScopedWrite::UpdateConfig(std::string_view config_name,
                                       const gendb::MessagePatch& update) {
  if (update.Modifies(Config::ConfigName))
    return absl::InvalidArgumentError(
        "Config: a patch cannot change the primary key field config_name");
  return _txn.Update(ConfigCollId, ToConfigKey(config_name), update);
}

absl::Status ScopedWrite::DeleteConfig(std::string_view config_name) {
  return _txn.Delete(ConfigCollId, ToConfigKey(config_name));
}

absl::Status Snapshot::GetSession(uint64_t session_id, Session& session) const {
  return ToMessage(_snapshot.Get(SessionCollId, ToSessionKey(session_id)), session);
}

gendb::ScanIterator<Session> Snapshot::ScanSession() const {
  return gendb::ScanIterator<Session>(gendb::ScanCursor(_snapshot, SessionCollId));
}

absl::Status ScopedWrite::GetSession(uint64_t session_id, Session& session) const {
  return ToMessage(_txn.Get(SessionCollId, ToSessionKey(session_id)), session);
}

gendb::ScanIterator<Session> ScopedWrite::ScanSession() const {
  return gendb::ScanIterator<Session>(gendb::ScanCursor(_txn, SessionCollId));
}

absl::Status ScopedWrite::PutSession(gendb::BytesConstView session) {
  // Malformed bytes would read as an empty message here, but they would also be stored and sent on.
  if (!gendb::MessageView::Verify(session))
    return absl::InvalidArgumentError("Session: not a valid message");
  const Session message{session};
  if (!message.has_session_id())
    return absl::InvalidArgumentError("Session: the primary key field session_id is not set");
  return _txn.Put(SessionCollId, ToSessionKey(message), session);
}

absl::Status ScopedWrite::UpdateSession(uint64_t session_id, const gendb::MessagePatch& update) {
  if (update.Modifies(Session::SessionId))
    return absl::InvalidArgumentError(
        "Session: a patch cannot change the primary key field session_id");
  return _txn.Update(SessionCollId, ToSessionKey(session_id), update);
}

absl::Status ScopedWrite::DeleteSession(uint64_t session_id) {
  return _txn.Delete(SessionCollId, ToSessionKey(session_id));
}

gendb::IndexIterator<Account> Snapshot::GetAccountByAgeEqual(int32_t age,
                                                             gendb::ScanOptions options) const {
  std::string lo;
  gendb::internal::key_codec::AppendField(age, lo);
  return gendb::IndexIterator<Account>(
      gendb::IndexCursor(_snapshot, AccountByAgeIndexId, lo, gendb::PrefixSuccessor(lo), options));
}

gendb::IndexIterator<Account> Snapshot::GetAccountByAgeRange(std::optional<int32_t> min_age,
                                                             std::optional<int32_t> max_age,
                                                             gendb::ScanOptions options) const {
  std::string lo;
  std::string hi;
  if (max_age.has_value()) {
    hi = lo;
    gendb::internal::key_codec::AppendField(*max_age, hi);
  } else {
    hi = gendb::PrefixSuccessor(lo);
  }
  if (min_age.has_value()) gendb::internal::key_codec::AppendField(*min_age, lo);
  return gendb::IndexIterator<Account>(
      gendb::IndexCursor(_snapshot, AccountByAgeIndexId, lo, std::move(hi), options));
}

gendb::IndexIterator<Account> ScopedWrite::GetAccountByAgeEqual(int32_t age,
                                                                gendb::ScanOptions options) const {
  std::string lo;
  gendb::internal::key_codec::AppendField(age, lo);
  return gendb::IndexIterator<Account>(
      gendb::IndexCursor(_txn, AccountByAgeIndexId, lo, gendb::PrefixSuccessor(lo), options));
}

gendb::IndexIterator<Account> ScopedWrite::GetAccountByAgeRange(std::optional<int32_t> min_age,
                                                                std::optional<int32_t> max_age,
                                                                gendb::ScanOptions options) const {
  std::string lo;
  std::string hi;
  if (max_age.has_value()) {
    hi = lo;
    gendb::internal::key_codec::AppendField(*max_age, hi);
  } else {
    hi = gendb::PrefixSuccessor(lo);
  }
  if (min_age.has_value()) gendb::internal::key_codec::AppendField(*min_age, lo);
  return gendb::IndexIterator<Account>(
      gendb::IndexCursor(_txn, AccountByAgeIndexId, lo, std::move(hi), options));
}

gendb::IndexIterator<Position> Snapshot::GetPositionByAccountIdEqual(
    int32_t account_id, gendb::ScanOptions options) const {
  std::string lo;
  gendb::internal::key_codec::AppendField(account_id, lo);
  return gendb::IndexIterator<Position>(gendb::IndexCursor(
      _snapshot, PositionByAccountIdIndexId, lo, gendb::PrefixSuccessor(lo), options));
}

gendb::IndexIterator<Position> Snapshot::GetPositionByAccountIdRange(
    std::optional<int32_t> min_account_id, std::optional<int32_t> max_account_id,
    gendb::ScanOptions options) const {
  std::string lo;
  std::string hi;
  if (max_account_id.has_value()) {
    hi = lo;
    gendb::internal::key_codec::AppendField(*max_account_id, hi);
  } else {
    hi = gendb::PrefixSuccessor(lo);
  }
  if (min_account_id.has_value()) gendb::internal::key_codec::AppendField(*min_account_id, lo);
  return gendb::IndexIterator<Position>(
      gendb::IndexCursor(_snapshot, PositionByAccountIdIndexId, lo, std::move(hi), options));
}

gendb::IndexIterator<Position> ScopedWrite::GetPositionByAccountIdEqual(
    int32_t account_id, gendb::ScanOptions options) const {
  std::string lo;
  gendb::internal::key_codec::AppendField(account_id, lo);
  return gendb::IndexIterator<Position>(gendb::IndexCursor(_txn, PositionByAccountIdIndexId, lo,
                                                           gendb::PrefixSuccessor(lo), options));
}

gendb::IndexIterator<Position> ScopedWrite::GetPositionByAccountIdRange(
    std::optional<int32_t> min_account_id, std::optional<int32_t> max_account_id,
    gendb::ScanOptions options) const {
  std::string lo;
  std::string hi;
  if (max_account_id.has_value()) {
    hi = lo;
    gendb::internal::key_codec::AppendField(*max_account_id, hi);
  } else {
    hi = gendb::PrefixSuccessor(lo);
  }
  if (min_account_id.has_value()) gendb::internal::key_codec::AppendField(*min_account_id, lo);
  return gendb::IndexIterator<Position>(
      gendb::IndexCursor(_txn, PositionByAccountIdIndexId, lo, std::move(hi), options));
}

gendb::IndexIterator<Position> Snapshot::GetPositionByAccountAndInstrumentEqual(
    int32_t account_id, gendb::ScanOptions options) const {
  std::string lo;
  gendb::internal::key_codec::AppendField(account_id, lo);
  return gendb::IndexIterator<Position>(gendb::IndexCursor(
      _snapshot, PositionByAccountAndInstrumentIndexId, lo, gendb::PrefixSuccessor(lo), options));
}

gendb::IndexIterator<Position> Snapshot::GetPositionByAccountAndInstrumentEqual(
    int32_t account_id, std::string_view instrument, gendb::ScanOptions options) const {
  std::string lo;
  gendb::internal::key_codec::AppendField(account_id, lo);
  gendb::internal::key_codec::AppendField(instrument, lo);
  return gendb::IndexIterator<Position>(gendb::IndexCursor(
      _snapshot, PositionByAccountAndInstrumentIndexId, lo, gendb::PrefixSuccessor(lo), options));
}

gendb::IndexIterator<Position> Snapshot::GetPositionByAccountAndInstrumentRange(
    std::optional<int32_t> min_account_id, std::optional<int32_t> max_account_id,
    gendb::ScanOptions options) const {
  std::string lo;
  std::string hi;
  if (max_account_id.has_value()) {
    hi = lo;
    gendb::internal::key_codec::AppendField(*max_account_id, hi);
  } else {
    hi = gendb::PrefixSuccessor(lo);
  }
  if (min_account_id.has_value()) gendb::internal::key_codec::AppendField(*min_account_id, lo);
  return gendb::IndexIterator<Position>(gendb::IndexCursor(
      _snapshot, PositionByAccountAndInstrumentIndexId, lo, std::move(hi), options));
}

gendb::IndexIterator<Position> Snapshot::GetPositionByAccountAndInstrumentRange(
    int32_t account_id, std::optional<std::string_view> min_instrument,
    std::optional<std::string_view> max_instrument, gendb::ScanOptions options) const {
  std::string lo;
  gendb::internal::key_codec::AppendField(account_id, lo);
  std::string hi;
  if (max_instrument.has_value()) {
    hi = lo;
    gendb::internal::key_codec::AppendField(*max_instrument, hi);
  } else {
    hi = gendb::PrefixSuccessor(lo);
  }
  if (min_instrument.has_value()) gendb::internal::key_codec::AppendField(*min_instrument, lo);
  return gendb::IndexIterator<Position>(gendb::IndexCursor(
      _snapshot, PositionByAccountAndInstrumentIndexId, lo, std::move(hi), options));
}

gendb::IndexIterator<Position> ScopedWrite::GetPositionByAccountAndInstrumentEqual(
    int32_t account_id, gendb::ScanOptions options) const {
  std::string lo;
  gendb::internal::key_codec::AppendField(account_id, lo);
  return gendb::IndexIterator<Position>(gendb::IndexCursor(
      _txn, PositionByAccountAndInstrumentIndexId, lo, gendb::PrefixSuccessor(lo), options));
}

gendb::IndexIterator<Position> ScopedWrite::GetPositionByAccountAndInstrumentEqual(
    int32_t account_id, std::string_view instrument, gendb::ScanOptions options) const {
  std::string lo;
  gendb::internal::key_codec::AppendField(account_id, lo);
  gendb::internal::key_codec::AppendField(instrument, lo);
  return gendb::IndexIterator<Position>(gendb::IndexCursor(
      _txn, PositionByAccountAndInstrumentIndexId, lo, gendb::PrefixSuccessor(lo), options));
}

gendb::IndexIterator<Position> ScopedWrite::GetPositionByAccountAndInstrumentRange(
    std::optional<int32_t> min_account_id, std::optional<int32_t> max_account_id,
    gendb::ScanOptions options) const {
  std::string lo;
  std::string hi;
  if (max_account_id.has_value()) {
    hi = lo;
    gendb::internal::key_codec::AppendField(*max_account_id, hi);
  } else {
    hi = gendb::PrefixSuccessor(lo);
  }
  if (min_account_id.has_value()) gendb::internal::key_codec::AppendField(*min_account_id, lo);
  return gendb::IndexIterator<Position>(
      gendb::IndexCursor(_txn, PositionByAccountAndInstrumentIndexId, lo, std::move(hi), options));
}

gendb::IndexIterator<Position> ScopedWrite::GetPositionByAccountAndInstrumentRange(
    int32_t account_id, std::optional<std::string_view> min_instrument,
    std::optional<std::string_view> max_instrument, gendb::ScanOptions options) const {
  std::string lo;
  gendb::internal::key_codec::AppendField(account_id, lo);
  std::string hi;
  if (max_instrument.has_value()) {
    hi = lo;
    gendb::internal::key_codec::AppendField(*max_instrument, hi);
  } else {
    hi = gendb::PrefixSuccessor(lo);
  }
  if (min_instrument.has_value()) gendb::internal::key_codec::AppendField(*min_instrument, lo);
  return gendb::IndexIterator<Position>(
      gendb::IndexCursor(_txn, PositionByAccountAndInstrumentIndexId, lo, std::move(hi), options));
}

gendb::IndexIterator<Account> Snapshot::GetAccountByAddressEqual(std::string_view address,
                                                                 gendb::ScanOptions options) const {
  std::string lo;
  gendb::internal::key_codec::AppendField(address, lo);
  return gendb::IndexIterator<Account>(gendb::IndexCursor(_snapshot, AccountByAddressIndexId, lo,
                                                          gendb::PrefixSuccessor(lo), options));
}

gendb::IndexIterator<Account> Snapshot::GetAccountByAddressRange(
    std::optional<std::string_view> min_address, std::optional<std::string_view> max_address,
    gendb::ScanOptions options) const {
  std::string lo;
  std::string hi;
  if (max_address.has_value()) {
    hi = lo;
    gendb::internal::key_codec::AppendField(*max_address, hi);
  } else {
    hi = gendb::PrefixSuccessor(lo);
  }
  if (min_address.has_value()) gendb::internal::key_codec::AppendField(*min_address, lo);
  return gendb::IndexIterator<Account>(
      gendb::IndexCursor(_snapshot, AccountByAddressIndexId, lo, std::move(hi), options));
}

absl::Status Snapshot::GetAccountByAddress(std::string_view address, Account& out) const {
  auto it = GetAccountByAddressEqual(address, {.limit = 1});
  if (!it.Valid()) return absl::NotFoundError("No such value in account_by_address");
  out = it.Value();
  return absl::OkStatus();
}

gendb::IndexIterator<Account> ScopedWrite::GetAccountByAddressEqual(
    std::string_view address, gendb::ScanOptions options) const {
  std::string lo;
  gendb::internal::key_codec::AppendField(address, lo);
  return gendb::IndexIterator<Account>(
      gendb::IndexCursor(_txn, AccountByAddressIndexId, lo, gendb::PrefixSuccessor(lo), options));
}

gendb::IndexIterator<Account> ScopedWrite::GetAccountByAddressRange(
    std::optional<std::string_view> min_address, std::optional<std::string_view> max_address,
    gendb::ScanOptions options) const {
  std::string lo;
  std::string hi;
  if (max_address.has_value()) {
    hi = lo;
    gendb::internal::key_codec::AppendField(*max_address, hi);
  } else {
    hi = gendb::PrefixSuccessor(lo);
  }
  if (min_address.has_value()) gendb::internal::key_codec::AppendField(*min_address, lo);
  return gendb::IndexIterator<Account>(
      gendb::IndexCursor(_txn, AccountByAddressIndexId, lo, std::move(hi), options));
}

absl::Status ScopedWrite::GetAccountByAddress(std::string_view address, Account& out) const {
  auto it = GetAccountByAddressEqual(address, {.limit = 1});
  if (!it.Valid()) return absl::NotFoundError("No such value in account_by_address");
  out = it.Value();
  return absl::OkStatus();
}

absl::Status ScopedWrite::NextAccountIdSequence(uint64_t& next_id) {
  const MetadataValueKey key{.type = MetadataType::kSequence,
                             .id = static_cast<uint32_t>(SequenceMetadataId::AccountIdSequence)};
  MetadataValue value;
  uint64_t next = 1;
  absl::Status status = GetMetadataValue(key, value);
  if (status.ok()) {
    next = value.uint_value() + 1;
  } else if (status.code() != absl::StatusCode::kNotFound) {
    return status;
  }
  MetadataValueBuilder builder;
  builder.set_type(key.type).set_id(key.id).set_uint_value(next);
  RETURN_IF_ERROR(PutMetadataValue(builder.Build()));
  next_id = static_cast<uint64_t>(next);
  return absl::OkStatus();
}

absl::Status ScopedWrite::NextPositionIdSequence(int32_t& next_id) {
  const MetadataValueKey key{.type = MetadataType::kSequence,
                             .id = static_cast<uint32_t>(SequenceMetadataId::PositionIdSequence)};
  MetadataValue value;
  uint64_t next = 1;
  absl::Status status = GetMetadataValue(key, value);
  if (status.ok()) {
    next = value.uint_value() + 1;
  } else if (status.code() != absl::StatusCode::kNotFound) {
    return status;
  }
  MetadataValueBuilder builder;
  builder.set_type(key.type).set_id(key.id).set_uint_value(next);
  RETURN_IF_ERROR(PutMetadataValue(builder.Build()));
  next_id = static_cast<int32_t>(next);
  return absl::OkStatus();
}

}  // namespace gendb::tests