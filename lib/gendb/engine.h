#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gendb/bytes.h"
#include "gendb/message.h"
#include "gendb/ordered_index.h"
#include "gendb/record_table.h"

// Transactional storage under the generated Db classes. docs/storage_engine.md is the design.
namespace gendb {

class RocksDbBackend;
class Transaction;

struct CollectionDef {
  std::string name;
  // Ephemeral collections behave like any other, but are never written to disk.
  bool ephemeral = false;
};

struct IndexDef {
  std::string name;
  size_t collection;
  // Appends the order-preserving encoding of the indexed field to `out`.
  // Returns false if the message has no such field, which leaves it out of the index.
  bool (*extract)(BytesConstView message, std::string& out);
};

struct EngineSchema {
  std::vector<CollectionDef> collections;
  std::vector<IndexDef> indices;
};

struct EngineOptions {
  // RocksDB directory, created if missing.
  std::string path;
  // Whether Commit returns only after an fsync. Without it, a crash can lose the latest commits
  // but keeps a consistent prefix of them; see Engine::SyncWal.
  bool sync_commits = true;
  // Keep every change in a durable log, for Engine::ReadChanges. Doubles the bytes written.
  bool change_log = false;
};

struct CommitOptions {
  // Overrides EngineOptions::sync_commits.
  std::optional<bool> sync;
};

// One record changed by a commit. The views are valid only during the callback.
struct Change {
  size_t collection;
  bool deleted;
  BytesConstView key;
  BytesConstView value;  // Empty if deleted.
};

struct CommitEvent {
  uint64_t seq;
  std::span<const Change> changes;
};

using ChangeListener = std::function<void(const CommitEvent&)>;

struct EngineStats {
  uint64_t committed_seq;
  size_t active_snapshots;
  size_t pending_garbage;  // Old versions and removed entries kept for active snapshots.
};

class Engine {
 public:
  // In memory only: there is nothing to recover, and all data is ephemeral.
  explicit Engine(EngineSchema schema);
  // Recovers committed state from `options.path`, rebuilding indices.
  static absl::StatusOr<std::unique_ptr<Engine>> Open(EngineSchema schema, EngineOptions options);
  ~Engine();
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  // Called during Commit, in commit order, while the writer lock is held. It can read through a
  // snapshot but cannot start a transaction, and a slow listener delays every writer.
  void AddChangeListener(ChangeListener listener);

  // With EngineOptions::change_log, calls `fn` for each durable change with seq > `after_seq`, in
  // commit order. Stops early if `fn` returns false. Ephemeral collections are not logged.
  absl::Status ReadChanges(uint64_t after_seq,
                           const std::function<bool(uint64_t seq, const Change&)>& fn) const;
  // Drops durable changes with seq <= `up_to_seq`, once every consumer has them.
  absl::Status TruncateChanges(uint64_t up_to_seq);

  // Makes every finished commit durable.
  absl::Status SyncWal();

  EngineStats Stats() const;

 private:
  friend class EngineSnapshot;
  friend class Transaction;
  friend class IndexCursor;
  friend class ScanCursor;

  struct Collection {
    internal::RecordTable table;
    std::vector<size_t> indices;
  };
  struct Garbage {
    enum Kind {
      kOlderVersion,        // ptr: a version; free what it superseded.
      kTombstone,           // ptr: a record whose head was a tombstone at seq; remove the record.
      kRecord,              // ptr: a removed record; free it.
      kSlots,               // ptr: a replaced slot array; free it.
      kIndexEntryToUnlink,  // ptr: a deleted index entry; unlink it.
      kIndexEntry,          // ptr: an unlinked index entry; free it.
    };
    uint64_t seq;  // Reclaimable once every snapshot is at this seq or later.
    Kind kind;
    void* ptr;
    size_t owner;
  };

  Engine(EngineSchema schema, std::unique_ptr<RocksDbBackend> backend, bool sync, bool change_log);
  absl::Status Recover();
  uint64_t PinSnapshot() const;
  void UnpinSnapshot(uint64_t seq) const noexcept;
  std::optional<BytesConstView> ReadAt(size_t collection, uint64_t hash, std::string_view key,
                                       uint64_t seq) const noexcept;
  internal::RecordNode* NodeForWrite(size_t collection, uint64_t hash, std::string_view key,
                                     uint64_t seq);
  void Reclaim(uint64_t up_to_seq) noexcept;

  const EngineSchema _schema;
  std::unique_ptr<RocksDbBackend> _backend;
  const bool _sync_commits;
  const bool _change_log;
  std::vector<std::unique_ptr<Collection>> _collections;
  std::vector<std::unique_ptr<internal::OrderedIndex>> _indices;

  std::mutex _writer_mutex;  // Held by a Transaction for its whole life.
  std::atomic<uint64_t> _committed_seq{0};
  std::deque<Garbage> _garbage;  // Writer only, ordered by seq.
  std::atomic<size_t> _garbage_size{0};
  std::vector<ChangeListener> _listeners;

  mutable std::mutex _snapshots_mutex;
  mutable absl::btree_map<uint64_t, uint32_t> _snapshots;  // Pinned seq to reader count.
};

// A consistent, repeatable view of committed state. Opening one takes a short mutex; reads take no
// locks and never block a commit. Views returned from it stay valid until it is destroyed, which
// keeps superseded versions in memory: do not hold one longer than needed.
class EngineSnapshot {
 public:
  explicit EngineSnapshot(const Engine& engine) : _engine(&engine), _seq(engine.PinSnapshot()) {}
  ~EngineSnapshot() {
    if (_engine != nullptr) _engine->UnpinSnapshot(_seq);
  }
  EngineSnapshot(EngineSnapshot&& other) noexcept : _engine(other._engine), _seq(other._seq) {
    other._engine = nullptr;
  }
  EngineSnapshot(const EngineSnapshot&) = delete;
  EngineSnapshot& operator=(const EngineSnapshot&) = delete;
  EngineSnapshot& operator=(EngineSnapshot&&) = delete;

  std::optional<BytesConstView> Get(size_t collection, BytesConstView key) const noexcept;

 private:
  friend class IndexCursor;
  friend class ScanCursor;

  const Engine* _engine;
  uint64_t _seq;
};

// Ordered scan of an index over [lo, hi); an empty `hi` means no upper bound. Over a transaction
// it includes the transaction's uncommitted changes.
class IndexCursor {
 public:
  IndexCursor(const EngineSnapshot& snapshot, size_t index, std::string_view lo, std::string hi);
  IndexCursor(const Transaction& txn, size_t index, std::string_view lo, std::string hi);
  bool Valid() const noexcept { return _value.has_value(); }
  BytesConstView value() const noexcept { return *_value; }
  void Next();

 private:
  struct Delta {
    std::string key;
    bool added;
    uint32_t secondary_size;
  };

  IndexCursor(const Engine& engine, size_t index, uint64_t seq, const Transaction* txn,
              std::string_view lo, std::string hi);
  const internal::IndexEntry* NextVisible(const internal::IndexEntry* e) const noexcept;
  std::optional<BytesConstView> Lookup(std::string_view primary_key) const noexcept;

  const Engine* _engine;
  size_t _collection;
  uint64_t _seq;
  const Transaction* _txn;
  std::string _hi;
  const internal::IndexEntry* _entry;
  std::vector<Delta> _deltas;  // Uncommitted changes in range, in key order.
  size_t _next_delta = 0;
  std::optional<BytesConstView> _value;
};

// All records of a collection in a snapshot, in no particular order.
class ScanCursor {
 public:
  ScanCursor(const EngineSnapshot& snapshot, size_t collection);
  bool Valid() const noexcept { return _value.has_value(); }
  BytesConstView value() const noexcept { return *_value; }
  void Next() noexcept;

 private:
  uint64_t _seq;
  const internal::RecordTable::Slots* _slots;
  size_t _next = 0;
  std::optional<BytesConstView> _value;
};

// The single writer. Changes are private until Commit, which applies all of them or none.
// Dropping a transaction rolls it back. It holds the writer lock, so other writers wait.
//
// A view returned by Get is invalidated by the next write to that key and by Commit.
class Transaction {
 public:
  explicit Transaction(Engine& engine);
  ~Transaction();
  Transaction(Transaction&&) noexcept;
  Transaction(const Transaction&) = delete;
  Transaction& operator=(const Transaction&) = delete;
  Transaction& operator=(Transaction&&) = delete;

  std::optional<BytesConstView> Get(size_t collection, BytesConstView key) const noexcept;
  void Put(size_t collection, BytesConstView key, BytesConstView value);
  absl::Status Update(size_t collection, BytesConstView key, const MessagePatch& patch);
  absl::Status Delete(size_t collection, BytesConstView key);

  // On failure nothing is applied and the transaction is rolled back. The transaction can be used
  // again afterwards.
  absl::Status Commit(CommitOptions options = {});
  void Rollback() noexcept;

 private:
  friend class IndexCursor;

  struct PendingWrite {
    size_t collection;
    uint64_t hash;
    std::string key;
    internal::Version* version;  // Owned until the commit publishes it.
  };
  struct IndexDelta {
    bool added;
    uint32_t secondary_size;
  };
  using PendingMap = std::unordered_map<std::string_view, PendingWrite*>;

  PendingWrite* FindPending(size_t collection, std::string_view key) const noexcept;
  void SetPending(size_t collection, uint64_t hash, std::string_view key, internal::Version* value);
  void UpdateIndices(size_t collection, std::string_view key, std::optional<BytesConstView> before,
                     std::optional<BytesConstView> after);

  Engine* _engine;
  std::unique_lock<std::mutex> _lock;
  std::deque<PendingWrite> _writes;  // Stable addresses, in first-write order.
  std::vector<PendingMap> _pending;  // Per collection; keys view PendingWrite::key.
  std::vector<absl::btree_map<std::string, IndexDelta>> _index_deltas;
  std::string _old_key, _new_key;  // Scratch for UpdateIndices.
};

template <typename T, typename Cursor>
class Iterator {
 public:
  explicit Iterator(Cursor cursor) : _cursor(std::move(cursor)) {}
  bool Valid() const noexcept { return _cursor.Valid(); }
  T Value() const noexcept { return T{_cursor.value()}; }
  void Next() { _cursor.Next(); }

 private:
  Cursor _cursor;
};

template <typename T>
using IndexIterator = Iterator<T, IndexCursor>;
template <typename T>
using ScanIterator = Iterator<T, ScanCursor>;

// The smallest string greater than every string starting with `prefix`; empty if there is none.
std::string PrefixSuccessor(std::string_view prefix);

}  // namespace gendb
