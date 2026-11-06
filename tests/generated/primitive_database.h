// AUTO GENERATED. DO NOT EDIT.
//
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gendb/bytes.h"
#include "gendb/engine.h"
#include "gendb/key_codec.h"
#include "gendb/message.h"
#include "messageA.fbs.h"
#include "metadata.fbs.h"

namespace gendb::tests::primitive {

// Forward declarations.
class Snapshot;
class ScopedWrite;

enum CollectionId {
  MetadataValueCollId = 0,
  MessageACollId = 1,
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
inline std::array<uint8_t, 4> ToMessageAKey(gendb::tests::primitive::KeyEnum key) {
  std::array<uint8_t, 4> key_raw;
  internal::key_codec::EncodeTupleToView<std::tuple<gendb::tests::primitive::KeyEnum>>({key},
                                                                                       key_raw);
  return key_raw;
}

inline std::array<uint8_t, 4> ToMessageAKey(MessageA message_a) {
  return ToMessageAKey(message_a.key());
}

using DbOptions = gendb::EngineOptions;

// What a commit did to one record, valid only during the callback. Whole messages before and after
// are available, and the *_changed() methods compare one field across them. On insert or delete
// the missing side reads as an empty message.
class MessageAChange {
 public:
  explicit MessageAChange(const gendb::Change& change)
      : _change(&change),
        _before(change.before.value_or(gendb::BytesConstView{})),
        _after(change.after.value_or(gendb::BytesConstView{})) {}

  gendb::ChangeKind kind() const { return _change->kind(); }
  gendb::BytesConstView key() const { return _change->key; }
  const MessageA& before() const { return _before; }
  const MessageA& after() const { return _after; }
  bool key_changed() const {
    return _before.has_key() != _after.has_key() || _before.key() != _after.key();
  }
  bool data_changed() const {
    return _before.has_data() != _after.has_data() || _before.data() != _after.data();
  }

 private:
  const gendb::Change* _change;
  MessageA _before;
  MessageA _after;
};

// Set the handlers of the collections to follow. All run on one thread, in commit order, so changes
// to different collections keep their order.
struct ChangeHandlers {
  std::function<void(const MessageAChange&)> on_message_a;
  // After the changes of each commit that had any, for acting on a commit as a whole.
  std::function<void(uint64_t seq)> on_commit;
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
  // Delivers changes committed after this call until the returned handle is destroyed.
  gendb::Subscription Subscribe(ChangeHandlers handlers, gendb::SubscribeOptions options = {});
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
  absl::Status GetMessageA(gendb::tests::primitive::KeyEnum key, MessageA& message_a) const;
  gendb::ScanIterator<MessageA> ScanMessageA() const;

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
  absl::Status GetMessageA(gendb::tests::primitive::KeyEnum key, MessageA& message_a) const;
  absl::Status PutMessageA(gendb::tests::primitive::KeyEnum key, gendb::BytesConstView message_a);
  absl::Status UpdateMessageA(gendb::tests::primitive::KeyEnum key,
                              const gendb::MessagePatch& update);
  absl::Status DeleteMessageA(gendb::tests::primitive::KeyEnum key);

 public:
  absl::Status Commit(gendb::CommitOptions options = {}) { return _txn.Commit(options); }
  void Rollback() { _txn.Rollback(); }

 private:
  friend class Db;
  explicit ScopedWrite(gendb::Engine& engine) : _txn(engine) {}

  gendb::Transaction _txn;
};

}  // namespace gendb::tests::primitive
