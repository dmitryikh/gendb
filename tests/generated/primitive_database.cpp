// AUTO GENERATED. DO NOT EDIT.
//
#include "primitive_database.h"

#include <cstdint>

#include "absl/status/status.h"
#include "gendb/bytes.h"
#include "gendb/iterator.h"
#include "gendb/message_patch.h"
#include "gendb/status.h"
#include "messageA.fbs.h"
#include "metadata.fbs.h"

namespace gendb::tests::primitive {

Guard Db::SharedLock() const {
  return {*this, std::shared_lock<std::shared_mutex>(_reader_mutex)};
}

ScopedWrite Db::CreateWriter() {
  return {*this, std::unique_lock<std::mutex>(_writer_mutex)};
}

absl::Status Guard::GetMetadataValue(const MetadataValueKey& key,
                                     MetadataValue& metadata_value) const {
  BytesConstView value;
  RETURN_IF_ERROR(_layered_storage.Get(MetadataValueCollId, ToMetadataValueKey(key), value));
  metadata_value = MetadataValue{value};
  return absl::OkStatus();
}

absl::Status ScopedWrite::GetMetadataValue(const MetadataValueKey& key,
                                           MetadataValue& metadata_value) const {
  BytesConstView value;
  RETURN_IF_ERROR(_layered_storage.Get(MetadataValueCollId, ToMetadataValueKey(key), value));
  metadata_value = MetadataValue{value};
  return absl::OkStatus();
}

absl::Status ScopedWrite::PutMetadataValue(const MetadataValueKey& key, Bytes metadata_value) {
  auto key_ = ToMetadataValueKey(key);
  _temp_storage.Put(MetadataValueCollId, key_, std::move(metadata_value));
  return absl::OkStatus();
}

absl::Status ScopedWrite::UpdateMetadataValue(const MetadataValueKey& key,
                                              const MessagePatch& update) {
  Bytes* ptr = nullptr;
  auto key_ = ToMetadataValueKey(key);
  RETURN_IF_ERROR(_layered_storage.EnsureInTempStorage(MetadataValueCollId, key_, &ptr));
  gendb::ApplyPatch<MetadataValue>(update, *ptr);
  return absl::OkStatus();
}
absl::Status Guard::GetMessageA(gendb::tests::primitive::KeyEnum key, MessageA& message_a) const {
  BytesConstView value;
  RETURN_IF_ERROR(_layered_storage.Get(MessageACollId, ToMessageAKey(key), value));
  message_a = MessageA{value};
  return absl::OkStatus();
}

absl::Status ScopedWrite::GetMessageA(gendb::tests::primitive::KeyEnum key,
                                      MessageA& message_a) const {
  BytesConstView value;
  RETURN_IF_ERROR(_layered_storage.Get(MessageACollId, ToMessageAKey(key), value));
  message_a = MessageA{value};
  return absl::OkStatus();
}

absl::Status ScopedWrite::PutMessageA(gendb::tests::primitive::KeyEnum key, Bytes message_a) {
  auto key_ = ToMessageAKey(key);
  _temp_storage.Put(MessageACollId, key_, std::move(message_a));
  return absl::OkStatus();
}

absl::Status ScopedWrite::UpdateMessageA(gendb::tests::primitive::KeyEnum key,
                                         const MessagePatch& update) {
  Bytes* ptr = nullptr;
  auto key_ = ToMessageAKey(key);
  RETURN_IF_ERROR(_layered_storage.EnsureInTempStorage(MessageACollId, key_, &ptr));
  gendb::ApplyPatch<MessageA>(update, *ptr);
  return absl::OkStatus();
}

void ScopedWrite::Commit() {
  std::unique_lock lock(_db._reader_mutex);
  _layered_storage.MergeTempStorage();
}

}  // namespace gendb::tests::primitive