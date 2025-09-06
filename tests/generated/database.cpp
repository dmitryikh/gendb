#include "database.h"

#include <cstdint>

#include "AccountFb.h"
#include "absl/status/status.h"
#include "gendb/status.h"

namespace gendb::tests {

Guard Db::SharedLock() const {
  return {*this, std::shared_lock<std::shared_mutex>(_reader_mutex)};
}

ScopedWrite Db::CreateWriter() {
  return {*this, std::unique_lock<std::mutex>(_writer_mutex)};
}

absl::Status Guard::GetAccount(uint32_t account_id, AccountFb& account) const {
  // absl::Status Guard::GetAccount(uint32_t account_id, AccountFb& account) const {
  BytesConstView value;
  RETURN_IF_ERROR(_layered_storage.Get(AccountCollId, ToAccountKey(account_id), value));
  account = AccountFb{value};
  return absl::OkStatus();
}

absl::Status Guard::GetPosition(uint32_t position_id, PositionFb& position) const {
  BytesConstView value;
  RETURN_IF_ERROR(_layered_storage.Get(PositionCollId, ToPositionKey(position_id), value));
  position = PositionFb{value};
  return absl::OkStatus();
}

absl::Status ScopedWrite::GetAccount(uint32_t account_id, AccountFb& account) const {
  BytesConstView value;
  RETURN_IF_ERROR(_layered_storage.Get(AccountCollId, ToAccountKey(account_id), value));
  account = AccountFb{value};
  return absl::OkStatus();
}

absl::Status ScopedWrite::GetPosition(uint32_t position_id, PositionFb& position) const {
  BytesConstView value;
  RETURN_IF_ERROR(_layered_storage.Get(PositionCollId, ToPositionKey(position_id), value));
  position = PositionFb{value};
  return absl::OkStatus();
}

absl::Status ScopedWrite::PutAccount(uint32_t account_id, std::vector<uint8_t> account) {
  _temp_storage.Put(AccountCollId, ToAccountKey(account_id), std::move(account));
  return absl::OkStatus();
}

absl::Status ScopedWrite::PutPosition(uint32_t position_id, std::vector<uint8_t> position) {
  _temp_storage.Put(PositionCollId, ToPositionKey(position_id), std::move(position));
  return absl::OkStatus();
}

absl::Status ScopedWrite::UpdateAccount(uint32_t account_id, const MessagePatch& update) {
  Bytes* account = nullptr;
  RETURN_IF_ERROR(
      _layered_storage.EnsureInTempStorage(AccountCollId, ToAccountKey(account_id), &account));
  gendb::ApplyPatch<AccountFb>(update, *account);
  return absl::OkStatus();
}

absl::Status ScopedWrite::UpdatePosition(uint32_t position_id, const MessagePatch& update) {
  Bytes* position = nullptr;
  RETURN_IF_ERROR(
      _layered_storage.EnsureInTempStorage(PositionCollId, ToPositionKey(position_id), &position));
  gendb::ApplyPatch<PositionFb>(update, *position);
  return absl::OkStatus();
}

void ScopedWrite::Commit() {
  std::unique_lock lock(_db._reader_mutex);
  _layered_storage.MergeTempStorage();
}

}  // namespace gendb::tests