// AUTO GENERATED. DO NOT EDIT.
//
#include "database.h"

#include <cstdint>

#include "Account.h"
#include "Position.h"
#include "absl/status/status.h"
#include "gendb/status.h"

namespace gendb::tests {

Guard Db::SharedLock() const {
  return {*this, std::shared_lock<std::shared_mutex>(_reader_mutex)};
}

ScopedWrite Db::CreateWriter() {
  return {*this, std::unique_lock<std::mutex>(_writer_mutex)};
}

absl::Status Guard::GetAccount(int account_id, Account& account) const {
  BytesConstView value;
  RETURN_IF_ERROR(_layered_storage.Get(AccountCollId, ToAccountKey(account_id), value));
  account = Account{value};
  return absl::OkStatus();
}

absl::Status ScopedWrite::GetAccount(int account_id, Account& account) const {
  BytesConstView value;
  RETURN_IF_ERROR(_layered_storage.Get(AccountCollId, ToAccountKey(account_id), value));
  account = Account{value};
  return absl::OkStatus();
}

absl::Status ScopedWrite::PutAccount(int account_id, std::vector<uint8_t> obj) {
  _temp_storage.Put(AccountCollId, ToAccountKey(account_id), std::move(obj));
  return absl::OkStatus();
}

absl::Status ScopedWrite::UpdateAccount(int account_id, const MessagePatch& update) {
  Bytes* ptr = nullptr;
  RETURN_IF_ERROR(
      _layered_storage.EnsureInTempStorage(AccountCollId, ToAccountKey(account_id), &ptr));
  gendb::ApplyPatch<Account>(update, *ptr);
  return absl::OkStatus();
}
absl::Status Guard::GetPosition(int position_id, Position& position) const {
  BytesConstView value;
  RETURN_IF_ERROR(_layered_storage.Get(PositionCollId, ToPositionKey(position_id), value));
  position = Position{value};
  return absl::OkStatus();
}

absl::Status ScopedWrite::GetPosition(int position_id, Position& position) const {
  BytesConstView value;
  RETURN_IF_ERROR(_layered_storage.Get(PositionCollId, ToPositionKey(position_id), value));
  position = Position{value};
  return absl::OkStatus();
}

absl::Status ScopedWrite::PutPosition(int position_id, std::vector<uint8_t> obj) {
  _temp_storage.Put(PositionCollId, ToPositionKey(position_id), std::move(obj));
  return absl::OkStatus();
}

absl::Status ScopedWrite::UpdatePosition(int position_id, const MessagePatch& update) {
  Bytes* ptr = nullptr;
  RETURN_IF_ERROR(
      _layered_storage.EnsureInTempStorage(PositionCollId, ToPositionKey(position_id), &ptr));
  gendb::ApplyPatch<Position>(update, *ptr);
  return absl::OkStatus();
}

void ScopedWrite::Commit() {
  std::unique_lock lock(_db._reader_mutex);
  _layered_storage.MergeTempStorage();
}

}  // namespace gendb::tests