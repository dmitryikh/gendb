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

Db::Db() : _engine(std::make_unique<gendb::Engine>(MakeSchema())) {}

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

gendb::Subscription Db::Subscribe(ChangeHandlers handlers, gendb::SubscribeOptions options) {
  options.collections.clear();
  if (handlers.on_message_a) options.collections.push_back(MessageACollId);
  if (options.collections.empty()) {
    options.collections.push_back(MessageACollId);
  }
  return _engine->Subscribe(
      [handlers = std::move(handlers)](const gendb::CommitEvent& event) {
        for (const gendb::Change& change : event.changes) {
          switch (change.collection) {
            case MessageACollId:
              if (handlers.on_message_a) handlers.on_message_a(MessageAChange(change));
              break;
            default:
              break;
          }
        }
        if (handlers.on_commit) handlers.on_commit(event.seq);
      },
      std::move(options));
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

absl::Status ScopedWrite::PutMessageA(gendb::tests::primitive::KeyEnum key,
                                      gendb::BytesConstView message_a) {
  _txn.Put(MessageACollId, ToMessageAKey(key), message_a);
  return absl::OkStatus();
}

absl::Status ScopedWrite::UpdateMessageA(gendb::tests::primitive::KeyEnum key,
                                         const gendb::MessagePatch& update) {
  return _txn.Update(MessageACollId, ToMessageAKey(key), update);
}

absl::Status ScopedWrite::DeleteMessageA(gendb::tests::primitive::KeyEnum key) {
  return _txn.Delete(MessageACollId, ToMessageAKey(key));
}

}  // namespace gendb::tests::primitive