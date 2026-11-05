// AUTO GENERATED. DO NOT EDIT.
//
#include "database.h"

#include <cstdint>

#include "absl/status/status.h"
#include "account.fbs.h"
#include "config.fbs.h"
#include "gendb/status.h"
#include "metadata.fbs.h"
#include "position.fbs.h"
#include "session.fbs.h"

namespace gendb::tests {

namespace {

gendb::EngineSchema MakeSchema() {
  gendb::EngineSchema schema;
  schema.collections.push_back({"metadata", /*ephemeral=*/false});
  schema.collections.push_back({"accounts", /*ephemeral=*/false});
  schema.collections.push_back({"positions", /*ephemeral=*/false});
  schema.collections.push_back({"configs", /*ephemeral=*/false});
  schema.collections.push_back({"sessions", /*ephemeral=*/true});
  schema.indices.push_back(
      {"account_by_age", AccountCollId, [](gendb::BytesConstView bytes, std::string& out) {
         const Account message{bytes};
         if (!message.has_age()) return false;
         internal::key_codec::AppendField(message.age(), out);
         return true;
       }});
  schema.indices.push_back(
      {"position_by_account_id", PositionCollId, [](gendb::BytesConstView bytes, std::string& out) {
         const Position message{bytes};
         if (!message.has_account_id()) return false;
         internal::key_codec::AppendField(message.account_id(), out);
         return true;
       }});
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

Db::Db() : _engine(std::make_unique<gendb::Engine>(MakeSchema())) {}

absl::StatusOr<std::unique_ptr<Db>> Db::Open(const DbOptions& options) {
  auto engine =
      gendb::Engine::Open(MakeSchema(), {options.path, options.sync_commits, options.change_log});
  if (!engine.ok()) return engine.status();
  return std::unique_ptr<Db>(new Db(std::move(*engine)));
}

Snapshot Db::CreateSnapshot() const {
  return Snapshot(*_engine);
}

ScopedWrite Db::CreateWriter() {
  return ScopedWrite(*_engine);
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

absl::Status ScopedWrite::PutMetadataValue(const MetadataValueKey& key,
                                           gendb::BytesConstView metadata_value) {
  _txn.Put(MetadataValueCollId, ToMetadataValueKey(key), metadata_value);
  return absl::OkStatus();
}

absl::Status ScopedWrite::UpdateMetadataValue(const MetadataValueKey& key,
                                              const gendb::MessagePatch& update) {
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

absl::Status ScopedWrite::PutAccount(uint64_t account_id, gendb::BytesConstView account) {
  _txn.Put(AccountCollId, ToAccountKey(account_id), account);
  return absl::OkStatus();
}

absl::Status ScopedWrite::UpdateAccount(uint64_t account_id, const gendb::MessagePatch& update) {
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

absl::Status ScopedWrite::PutPosition(int32_t position_id, gendb::BytesConstView position) {
  _txn.Put(PositionCollId, ToPositionKey(position_id), position);
  return absl::OkStatus();
}

absl::Status ScopedWrite::UpdatePosition(int32_t position_id, const gendb::MessagePatch& update) {
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

absl::Status ScopedWrite::PutConfig(std::string_view config_name, gendb::BytesConstView config) {
  _txn.Put(ConfigCollId, ToConfigKey(config_name), config);
  return absl::OkStatus();
}

absl::Status ScopedWrite::UpdateConfig(std::string_view config_name,
                                       const gendb::MessagePatch& update) {
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

absl::Status ScopedWrite::PutSession(uint64_t session_id, gendb::BytesConstView session) {
  _txn.Put(SessionCollId, ToSessionKey(session_id), session);
  return absl::OkStatus();
}

absl::Status ScopedWrite::UpdateSession(uint64_t session_id, const gendb::MessagePatch& update) {
  return _txn.Update(SessionCollId, ToSessionKey(session_id), update);
}

absl::Status ScopedWrite::DeleteSession(uint64_t session_id) {
  return _txn.Delete(SessionCollId, ToSessionKey(session_id));
}

gendb::IndexIterator<Account> Snapshot::GetAccountByAgeRange(int32_t min_age,
                                                             int32_t max_age) const {
  std::string lo, hi;
  internal::key_codec::AppendField(min_age, lo);
  internal::key_codec::AppendField(max_age, hi);
  return gendb::IndexIterator<Account>(
      gendb::IndexCursor(_snapshot, AccountByAgeIndexId, lo, std::move(hi)));
}

gendb::IndexIterator<Account> Snapshot::GetAccountByAgeEqual(int32_t age) const {
  std::string lo;
  internal::key_codec::AppendField(age, lo);
  return gendb::IndexIterator<Account>(
      gendb::IndexCursor(_snapshot, AccountByAgeIndexId, lo, gendb::PrefixSuccessor(lo)));
}

gendb::IndexIterator<Account> ScopedWrite::GetAccountByAgeRange(int32_t min_age,
                                                                int32_t max_age) const {
  std::string lo, hi;
  internal::key_codec::AppendField(min_age, lo);
  internal::key_codec::AppendField(max_age, hi);
  return gendb::IndexIterator<Account>(
      gendb::IndexCursor(_txn, AccountByAgeIndexId, lo, std::move(hi)));
}

gendb::IndexIterator<Account> ScopedWrite::GetAccountByAgeEqual(int32_t age) const {
  std::string lo;
  internal::key_codec::AppendField(age, lo);
  return gendb::IndexIterator<Account>(
      gendb::IndexCursor(_txn, AccountByAgeIndexId, lo, gendb::PrefixSuccessor(lo)));
}

gendb::IndexIterator<Position> Snapshot::GetPositionByAccountIdRange(int32_t min_account_id,
                                                                     int32_t max_account_id) const {
  std::string lo, hi;
  internal::key_codec::AppendField(min_account_id, lo);
  internal::key_codec::AppendField(max_account_id, hi);
  return gendb::IndexIterator<Position>(
      gendb::IndexCursor(_snapshot, PositionByAccountIdIndexId, lo, std::move(hi)));
}

gendb::IndexIterator<Position> Snapshot::GetPositionByAccountIdEqual(int32_t account_id) const {
  std::string lo;
  internal::key_codec::AppendField(account_id, lo);
  return gendb::IndexIterator<Position>(
      gendb::IndexCursor(_snapshot, PositionByAccountIdIndexId, lo, gendb::PrefixSuccessor(lo)));
}

gendb::IndexIterator<Position> ScopedWrite::GetPositionByAccountIdRange(
    int32_t min_account_id, int32_t max_account_id) const {
  std::string lo, hi;
  internal::key_codec::AppendField(min_account_id, lo);
  internal::key_codec::AppendField(max_account_id, hi);
  return gendb::IndexIterator<Position>(
      gendb::IndexCursor(_txn, PositionByAccountIdIndexId, lo, std::move(hi)));
}

gendb::IndexIterator<Position> ScopedWrite::GetPositionByAccountIdEqual(int32_t account_id) const {
  std::string lo;
  internal::key_codec::AppendField(account_id, lo);
  return gendb::IndexIterator<Position>(
      gendb::IndexCursor(_txn, PositionByAccountIdIndexId, lo, gendb::PrefixSuccessor(lo)));
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
  RETURN_IF_ERROR(PutMetadataValue(key, builder.Build()));
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
  RETURN_IF_ERROR(PutMetadataValue(key, builder.Build()));
  next_id = static_cast<int32_t>(next);
  return absl::OkStatus();
}

}  // namespace gendb::tests