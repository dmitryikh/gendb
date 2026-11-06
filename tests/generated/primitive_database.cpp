// AUTO GENERATED. DO NOT EDIT.
//
#include "primitive_database.h"

#include <cstdint>

#include "absl/status/status.h"
#include "gendb/status.h"
#include "messageA.fbs.h"
#include "metadata.fbs.h"

namespace gendb::tests::primitive {

namespace {

gendb::EngineSchema MakeSchema() {
  gendb::EngineSchema schema;
  schema.collections.push_back({"metadata", /*ephemeral=*/false});
  schema.collections.push_back({"Table_A", /*ephemeral=*/false});
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
  if (handlers.on_message_a) collections.push_back(MessageACollId);
  if (collections.empty()) {
    collections.push_back(MessageACollId);
  }
  return collections;
}

gendb::ChangeCallback ToCallback(ChangeHandlers handlers) {
  return [handlers = std::move(handlers)](const gendb::CommitEvent& event) {
    for (const gendb::Change& change : event.changes) {
      switch (change.collection) {
        case MessageACollId:
          if (handlers.on_message_a) handlers.on_message_a(MessageAChange(change));
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

absl::Status Snapshot::GetMessageA(gendb::tests::primitive::KeyEnum key,
                                   MessageA& message_a) const {
  return ToMessage(_snapshot.Get(MessageACollId, ToMessageAKey(key)), message_a);
}

gendb::ScanIterator<MessageA> Snapshot::ScanMessageA() const {
  return gendb::ScanIterator<MessageA>(gendb::ScanCursor(_snapshot, MessageACollId));
}

absl::Status ScopedWrite::GetMessageA(gendb::tests::primitive::KeyEnum key,
                                      MessageA& message_a) const {
  return ToMessage(_txn.Get(MessageACollId, ToMessageAKey(key)), message_a);
}

gendb::ScanIterator<MessageA> ScopedWrite::ScanMessageA() const {
  return gendb::ScanIterator<MessageA>(gendb::ScanCursor(_txn, MessageACollId));
}

absl::Status ScopedWrite::PutMessageA(gendb::BytesConstView message_a) {
  // Malformed bytes would read as an empty message here, but they would also be stored and sent on.
  if (!gendb::MessageView::Verify(message_a))
    return absl::InvalidArgumentError("MessageA: not a valid message");
  const MessageA message{message_a};
  if (!message.has_key())
    return absl::InvalidArgumentError("MessageA: the primary key field key is not set");
  return _txn.Put(MessageACollId, ToMessageAKey(message), message_a);
}

absl::Status ScopedWrite::UpdateMessageA(gendb::tests::primitive::KeyEnum key,
                                         const gendb::MessagePatch& update) {
  if (update.Modifies(MessageA::Key))
    return absl::InvalidArgumentError("MessageA: a patch cannot change the primary key field key");
  return _txn.Update(MessageACollId, ToMessageAKey(key), update);
}

absl::Status ScopedWrite::DeleteMessageA(gendb::tests::primitive::KeyEnum key) {
  return _txn.Delete(MessageACollId, ToMessageAKey(key));
}

}  // namespace gendb::tests::primitive