// AUTO GENERATED. DO NOT EDIT.
//
#include "database.h"

#include <cstdint>
#include <optional>

#include "Account.h"
#include "Config.h"
#include "Position.h"
#include "absl/status/status.h"
#include "gendb/bytes.h"
#include "gendb/index.h"
#include "gendb/iterator.h"
#include "gendb/message_patch.h"
#include "gendb/status.h"

namespace gendb::tests {

Guard Db::SharedLock() const {
  return {*this, std::shared_lock<std::shared_mutex>(_reader_mutex)};
}

ScopedWrite Db::CreateWriter() {
  return {*this, std::unique_lock<std::mutex>(_writer_mutex)};
}

absl::Status Guard::GetAccount(uint64_t account_id, Account& account) const {
  BytesConstView value;
  RETURN_IF_ERROR(_layered_storage.Get(AccountCollId, ToAccountKey(account_id), value));
  account = Account{value};
  return absl::OkStatus();
}

absl::Status ScopedWrite::GetAccount(uint64_t account_id, Account& account) const {
  BytesConstView value;
  RETURN_IF_ERROR(_layered_storage.Get(AccountCollId, ToAccountKey(account_id), value));
  account = Account{value};
  return absl::OkStatus();
}

absl::Status ScopedWrite::PutAccount(uint64_t account_id, Bytes account) {
  auto key = ToAccountKey(account_id);
  MaybeUpdateAccountByAgeIndex(key, account, /*update=*/nullptr);
  _temp_storage.Put(AccountCollId, key, std::move(account));
  return absl::OkStatus();
}

absl::Status ScopedWrite::UpdateAccount(uint64_t account_id, const MessagePatch& update) {
  Bytes* ptr = nullptr;
  auto key = ToAccountKey(account_id);
  RETURN_IF_ERROR(_layered_storage.EnsureInTempStorage(AccountCollId, key, &ptr));
  MaybeUpdateAccountByAgeIndex(key, *ptr, &update);
  gendb::ApplyPatch<Account>(update, *ptr);
  return absl::OkStatus();
}
absl::Status Guard::GetPosition(int32_t position_id, Position& position) const {
  BytesConstView value;
  RETURN_IF_ERROR(_layered_storage.Get(PositionCollId, ToPositionKey(position_id), value));
  position = Position{value};
  return absl::OkStatus();
}

absl::Status ScopedWrite::GetPosition(int32_t position_id, Position& position) const {
  BytesConstView value;
  RETURN_IF_ERROR(_layered_storage.Get(PositionCollId, ToPositionKey(position_id), value));
  position = Position{value};
  return absl::OkStatus();
}

absl::Status ScopedWrite::PutPosition(int32_t position_id, Bytes position) {
  auto key = ToPositionKey(position_id);
  MaybeUpdatePositionByAccountIdIndex(key, position, /*update=*/nullptr);
  _temp_storage.Put(PositionCollId, key, std::move(position));
  return absl::OkStatus();
}

absl::Status ScopedWrite::UpdatePosition(int32_t position_id, const MessagePatch& update) {
  Bytes* ptr = nullptr;
  auto key = ToPositionKey(position_id);
  RETURN_IF_ERROR(_layered_storage.EnsureInTempStorage(PositionCollId, key, &ptr));
  MaybeUpdatePositionByAccountIdIndex(key, *ptr, &update);
  gendb::ApplyPatch<Position>(update, *ptr);
  return absl::OkStatus();
}
absl::Status Guard::GetConfig(std::string_view config_name, Config& config) const {
  BytesConstView value;
  RETURN_IF_ERROR(_layered_storage.Get(ConfigCollId, ToConfigKey(config_name), value));
  config = Config{value};
  return absl::OkStatus();
}

absl::Status ScopedWrite::GetConfig(std::string_view config_name, Config& config) const {
  BytesConstView value;
  RETURN_IF_ERROR(_layered_storage.Get(ConfigCollId, ToConfigKey(config_name), value));
  config = Config{value};
  return absl::OkStatus();
}

absl::Status ScopedWrite::PutConfig(std::string_view config_name, Bytes config) {
  auto key = ToConfigKey(config_name);
  _temp_storage.Put(ConfigCollId, key, std::move(config));
  return absl::OkStatus();
}

absl::Status ScopedWrite::UpdateConfig(std::string_view config_name, const MessagePatch& update) {
  Bytes* ptr = nullptr;
  auto key = ToConfigKey(config_name);
  RETURN_IF_ERROR(_layered_storage.EnsureInTempStorage(ConfigCollId, key, &ptr));
  gendb::ApplyPatch<Config>(update, *ptr);
  return absl::OkStatus();
}

gendb::Iterator<Account> Guard::GetAccountByAgeRange(int32_t min_age, int32_t max_age) const {
  return gendb::MakeSecondaryIndexIterator<Account, Indices::AccountByAgeIndexType>(
      _layered_storage, AccountCollId, _db._indices.account_by_age.lower_bound(min_age),
      _db._indices.account_by_age.lower_bound(max_age));
}

gendb::Iterator<Account> Guard::GetAccountByAgeEqual(int32_t age) const {
  return gendb::MakeSecondaryIndexIterator<Account, Indices::AccountByAgeIndexType>(
      _layered_storage, AccountCollId, _db._indices.account_by_age.lower_bound(age),
      _db._indices.account_by_age.upper_bound(age));
}

gendb::Iterator<Account> ScopedWrite::GetAccountByAgeRange(int32_t min_age, int32_t max_age) const {
  return gendb::MakeSecondaryIndexIterator<Account, Indices::AccountByAgeIndexType>(
      _layered_storage, AccountCollId, _db._indices.account_by_age.lower_bound(min_age),
      _db._indices.account_by_age.lower_bound(max_age),
      _temp_indices.account_by_age.lower_bound(min_age),
      _temp_indices.account_by_age.lower_bound(max_age));
}

gendb::Iterator<Account> ScopedWrite::GetAccountByAgeEqual(int32_t age) const {
  return gendb::MakeSecondaryIndexIterator<Account, Indices::AccountByAgeIndexType>(
      _layered_storage, AccountCollId, _db._indices.account_by_age.lower_bound(age),
      _db._indices.account_by_age.upper_bound(age), _temp_indices.account_by_age.lower_bound(age),
      _temp_indices.account_by_age.upper_bound(age));
}

void ScopedWrite::MaybeUpdateAccountByAgeIndex(std::array<uint8_t, sizeof(uint64_t)> key,
                                               gendb::BytesConstView account_buffer,
                                               const MessagePatch* update) {
  std::optional<int32_t> age_before = std::nullopt;
  std::optional<int32_t> age_after = std::nullopt;
  if (update != nullptr && !DoModifyField(*update, Account::Age)) {
    // This is update op which doesn't touch the indexed field.
    return;
  }
  Account account{account_buffer};
  if (account.has_age()) {
    age_before = account.age();
  }
  if (update != nullptr) {
    Account account_update{update->buffer};
    if (account_update.has_age()) {
      age_after = account_update.age();
    }
  }
  if (age_before.has_value()) {
    _temp_indices.account_by_age.Insert(age_before.value(), key,
                                        /*is_deleted=*/update != nullptr);
  }
  if (age_after.has_value()) {
    _temp_indices.account_by_age.Insert(age_after.value(), key);
  }
}
gendb::Iterator<Position> Guard::GetPositionByAccountIdRange(int32_t min_account_id,
                                                             int32_t max_account_id) const {
  return gendb::MakeSecondaryIndexIterator<Position, Indices::PositionByAccountIdIndexType>(
      _layered_storage, PositionCollId,
      _db._indices.position_by_account_id.lower_bound(min_account_id),
      _db._indices.position_by_account_id.lower_bound(max_account_id));
}

gendb::Iterator<Position> Guard::GetPositionByAccountIdEqual(int32_t account_id) const {
  return gendb::MakeSecondaryIndexIterator<Position, Indices::PositionByAccountIdIndexType>(
      _layered_storage, PositionCollId, _db._indices.position_by_account_id.lower_bound(account_id),
      _db._indices.position_by_account_id.upper_bound(account_id));
}

gendb::Iterator<Position> ScopedWrite::GetPositionByAccountIdRange(int32_t min_account_id,
                                                                   int32_t max_account_id) const {
  return gendb::MakeSecondaryIndexIterator<Position, Indices::PositionByAccountIdIndexType>(
      _layered_storage, PositionCollId,
      _db._indices.position_by_account_id.lower_bound(min_account_id),
      _db._indices.position_by_account_id.lower_bound(max_account_id),
      _temp_indices.position_by_account_id.lower_bound(min_account_id),
      _temp_indices.position_by_account_id.lower_bound(max_account_id));
}

gendb::Iterator<Position> ScopedWrite::GetPositionByAccountIdEqual(int32_t account_id) const {
  return gendb::MakeSecondaryIndexIterator<Position, Indices::PositionByAccountIdIndexType>(
      _layered_storage, PositionCollId, _db._indices.position_by_account_id.lower_bound(account_id),
      _db._indices.position_by_account_id.upper_bound(account_id),
      _temp_indices.position_by_account_id.lower_bound(account_id),
      _temp_indices.position_by_account_id.upper_bound(account_id));
}

void ScopedWrite::MaybeUpdatePositionByAccountIdIndex(std::array<uint8_t, sizeof(int32_t)> key,
                                                      gendb::BytesConstView position_buffer,
                                                      const MessagePatch* update) {
  std::optional<int32_t> account_id_before = std::nullopt;
  std::optional<int32_t> account_id_after = std::nullopt;
  if (update != nullptr && !DoModifyField(*update, Position::AccountId)) {
    // This is update op which doesn't touch the indexed field.
    return;
  }
  Position position{position_buffer};
  if (position.has_account_id()) {
    account_id_before = position.account_id();
  }
  if (update != nullptr) {
    Position position_update{update->buffer};
    if (position_update.has_account_id()) {
      account_id_after = position_update.account_id();
    }
  }
  if (account_id_before.has_value()) {
    _temp_indices.position_by_account_id.Insert(account_id_before.value(), key,
                                                /*is_deleted=*/update != nullptr);
  }
  if (account_id_after.has_value()) {
    _temp_indices.position_by_account_id.Insert(account_id_after.value(), key);
  }
}

void ScopedWrite::Commit() {
  std::unique_lock lock(_db._reader_mutex);
  _layered_storage.MergeTempStorage();
  _db._indices.MergeTempIndices(std::move(_temp_indices));
}

}  // namespace gendb::tests