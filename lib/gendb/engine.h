#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gendb/bytes.h"
#include "gendb/log_format.h"
#include "gendb/message.h"
#include "gendb/ordered_index.h"
#include "gendb/record_table.h"

// Transactional storage under the generated Db classes. docs/storage_engine.md is the design.
namespace gendb {

class EngineSnapshot;
class Transaction;
class Subscription;
class ReplicationServer;
class Replica;

namespace internal {
class CheckpointStage;
class FrameSource;
class Wal;
class WalCursor;
struct Subscriber;
}  // namespace internal

struct CollectionDef {
  std::string name;
  // Ephemeral collections behave like any other, but stay out of the WAL, checkpoints and
  // replication.
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
  // Database directory, created if missing. Open it from one process at a time.
  std::string path;

  // True: durable. Commit returns after an fsync, and a commit survives power loss.
  // False: non-durable. Commit returns after handing the data to the OS: a commit survives the
  // process dying, but power loss can lose the latest ones, always leaving a consistent prefix.
  // CommitOptions::sync overrides this per commit.
  bool sync_commits = true;
  // With non-durable commits, how often a background thread fsyncs the log, which bounds what
  // power loss can take. Zero leaves it to Engine::SyncWal.
  std::chrono::milliseconds background_sync_interval{0};

  uint64_t wal_segment_bytes = uint64_t{64} << 20;
  // Automatic compaction: a background checkpoint starts once the log grows by this many bytes, or
  // by the size of the last checkpoint if larger, which keeps total disk writes below about twice
  // the log's size. Zero leaves it to Engine::Checkpoint.
  uint64_t checkpoint_wal_bytes = uint64_t{256} << 20;
  // Log kept past the latest checkpoint, so that a replica reconnecting after a short absence does
  // not need a whole checkpoint.
  uint64_t wal_retain_bytes = uint64_t{64} << 20;
  // Log kept for connected replicas that fall behind. Past it, they are sent a new checkpoint.
  uint64_t wal_replica_limit_bytes = uint64_t{4} << 30;

  // Follows a primary through a Replica: only ephemeral collections accept local commits.
  bool replica = false;
  // Open fails if the database holds a collection the schema does not have, unless this is set, in
  // which case that collection is dropped for good by the next checkpoint.
  bool discard_unknown_collections = false;
};

struct CommitOptions {
  // Overrides EngineOptions::sync_commits.
  std::optional<bool> sync;
};

enum class ChangeKind { kInsert, kUpdate, kDelete };

// One record changed by a commit: its whole state before and after. Comparing them tells what
// changed, for a Put as much as for an Update; generated code offers it per field.
struct Change {
  size_t collection;
  BytesConstView key;
  std::optional<BytesConstView> before;  // Absent on insert.
  std::optional<BytesConstView> after;   // Absent on delete.

  ChangeKind kind() const noexcept {
    if (!before.has_value()) return ChangeKind::kInsert;
    return after.has_value() ? ChangeKind::kUpdate : ChangeKind::kDelete;
  }
};

// The views are valid only during the callback.
struct CommitEvent {
  uint64_t seq;  // An ever-growing commit number within this process.
  std::span<const Change> changes;
};

using ChangeCallback = std::function<void(const CommitEvent&)>;

enum class Delivery {
  // A dedicated thread, in commit order, after Commit returned or soon before. The callback can do
  // anything, including commit. Each change is copied. Once the queue is full, new transactions
  // wait for it to drain, so a slow subscriber slows writers but never loses a change.
  kQueued,
  // During Commit, on the writer's thread, with the writer lock held: no copies and no lag, but a
  // slow callback delays every writer, and a callback must not start a transaction.
  kInline,
};

struct SubscribeOptions {
  std::vector<size_t> collections;  // Empty means all of them.
  Delivery delivery = Delivery::kQueued;
  size_t max_queue_bytes = size_t{64} << 20;
};

struct EngineStats {
  uint64_t committed_seq;
  uint64_t lsn;             // Commits that reached the log; ephemeral ones do not count.
  uint64_t synced_lsn;      // Commits known to be on stable storage.
  uint64_t checkpoint_lsn;  // The latest checkpoint's position.
  uint64_t checkpoint_bytes;
  uint64_t wal_bytes;
  size_t active_snapshots;
  size_t pending_garbage;         // Old versions and removed entries kept for active snapshots.
  absl::Status background_error;  // The latest background checkpoint or sync failure.
};

class Engine {
 public:
  // In memory only: there is nothing to recover, and all data is ephemeral.
  explicit Engine(EngineSchema schema);
  // Loads the latest checkpoint from `options.path`, then replays the log after it.
  static absl::StatusOr<std::unique_ptr<Engine>> Open(EngineSchema schema, EngineOptions options);
  ~Engine();
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  // Calls `callback` with each commit that changes one of the chosen collections, leaving out the
  // others. Changes committed before this call are not delivered. The subscription ends with the
  // returned handle.
  Subscription Subscribe(ChangeCallback callback, SubscribeOptions options = {});

  // Makes every finished commit durable.
  absl::Status SyncWal();
  // Writes the current state to a new checkpoint and removes the log it supersedes. Writers and
  // readers keep running, though versions replaced meanwhile stay in memory until it ends. Blocks
  // until any open Transaction ends, so do not call it while holding one.
  absl::Status Checkpoint();

  // Ends follower mode, for failover: stop the Replica first. Starts a new history, so anyone
  // still following the old one gets a full checkpoint from here.
  absl::Status PromoteToPrimary();

  EngineStats Stats() const;

 private:
  friend class EngineSnapshot;
  friend class Transaction;
  friend class IndexCursor;
  friend class ScanCursor;
  friend class Subscription;
  friend class ReplicationServer;
  friend class Replica;
  friend struct internal::Subscriber;

  class CheckpointLoader;
  class CommitRing;

  struct Collection {
    internal::RecordTable table;
    std::vector<size_t> indices;
    uint32_t id;  // In frames.
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
  struct LogPosition {
    internal::HistoryId history;
    uint64_t lsn;
  };

  Engine(EngineSchema schema, EngineOptions options);
  absl::Status Recover();
  uint64_t PinSnapshot() const;
  void UnpinSnapshot(uint64_t seq) const noexcept;
  std::optional<BytesConstView> ReadAt(size_t collection, uint64_t hash, std::string_view key,
                                       uint64_t seq) const noexcept;
  internal::RecordNode* NodeForWrite(size_t collection, uint64_t hash, std::string_view key,
                                     uint64_t seq);
  void Reclaim(uint64_t up_to_seq) noexcept;
  void ReclaimUnpinned(uint64_t seq) noexcept;
  std::optional<size_t> FindCollection(uint32_t id) const noexcept;

  // Writer thread.
  std::unique_lock<std::mutex> AcquireWriter();
  // For brief maintenance, such as a checkpoint's start: goes ahead of the transactions waiting,
  // as a busy writer retaking the mutex could otherwise keep it away for good.
  std::unique_lock<std::mutex> AcquireWriterFirst();
  void Notify(uint64_t seq, std::span<const Change> changes);
  void AfterCommit(uint64_t wal_bytes);
  void UpdateIndexNow(size_t collection, std::string_view key, std::optional<BytesConstView> before,
                      std::optional<BytesConstView> after, uint64_t seq);
  // Replays one commit frame through `txn`.
  absl::Status ApplyCommitFrame(Transaction& txn, const internal::Frame& frame);

  // Compaction.
  absl::Status ExportCheckpoint(const EngineSnapshot& snapshot, const LogPosition& position,
                                const std::function<absl::Status(std::string_view)>& sink) const;
  absl::Status PurgeWal();
  void RemoveCheckpointsBelow(uint64_t lsn) const;
  void BackgroundLoop();
  void RecordBackgroundError(absl::Status status);

  // Subscriptions.
  void Unsubscribe(const std::shared_ptr<internal::Subscriber>& subscriber);
  void WaitForSubscriberRoom();

  // Replication, for ReplicationServer and Replica.
  void EnableCommitRing(size_t capacity_bytes);
  LogPosition Position();
  uint64_t history_generation() const noexcept {
    return _history_generation.load(std::memory_order_acquire);
  }
  bool CanReadLogAfter(uint64_t lsn) const;
  uint64_t PinWal(uint64_t lsn);
  void UpdateWalPin(uint64_t pin, uint64_t lsn);
  void UnpinWal(uint64_t pin);
  // Copies commits after `*position`, if any and synced, and advances it. OutOfRange if the next
  // commit is no longer available.
  absl::StatusOr<bool> ReadLog(uint64_t* position, size_t max_bytes, std::string* out,
                               std::unique_ptr<internal::WalCursor>* cursor);
  void WaitForLog(uint64_t after, std::chrono::milliseconds timeout) const;
  void WakeLogWaiters() const;
  void BecomeReplica() noexcept { _replica.store(true, std::memory_order_release); }
  absl::StatusOr<std::unique_ptr<internal::CheckpointStage>> NewCheckpointStage() const;
  // Atomically replaces all durable collections with the checkpoint in `stage`. Readers see the
  // old state until the new one is whole.
  absl::Status InstallCheckpoint(internal::CheckpointStage& stage, const LogPosition& position);
  // Logs and applies whole commit frames from a primary, which must continue this engine's log.
  absl::Status ApplyReplicated(std::string_view frames);

  const EngineSchema _schema;
  const EngineOptions _options;
  std::vector<std::unique_ptr<Collection>> _collections;
  std::vector<std::pair<uint32_t, size_t>> _collection_ids;  // Sorted.
  std::vector<std::unique_ptr<internal::OrderedIndex>> _indices;

  std::mutex _writer_mutex;  // Held by a Transaction for its whole life.
  std::atomic<int> _writers_going_first{0};
  std::atomic<uint64_t> _committed_seq{0};
  std::deque<Garbage> _garbage;  // Writer only, ordered by seq.
  std::atomic<size_t> _garbage_size{0};
  std::string _commit_buffer;                  // Writer only.
  std::string _old_index_key, _new_index_key;  // Writer only: scratch for UpdateIndexNow.
  uint32_t _install_epoch = 0;                 // Writer only.

  std::atomic<uint64_t> _lsn{0};  // Written under the writer lock.
  internal::HistoryId _history;   // Under the writer lock.
  std::atomic<uint64_t> _history_generation{0};
  std::atomic<bool> _replica;
  std::unique_ptr<CommitRing> _ring;

  // Persistence; null in memory.
  std::unique_ptr<internal::Wal> _wal;
  int _lock_fd = -1;
  std::mutex _checkpoint_mutex;  // Serializes checkpoints. Taken before the writer lock.
  std::atomic<uint64_t> _checkpoint_lsn{0};
  std::atomic<uint64_t> _checkpoint_bytes{0};
  std::atomic<uint64_t> _wal_bytes_since_checkpoint{0};
  mutable std::mutex _wal_pins_mutex;
  std::vector<std::pair<uint64_t, uint64_t>> _wal_pins;  // Pin ID to the LSN it keeps.
  uint64_t _next_wal_pin = 1;

  // The background thread.
  std::thread _background;
  mutable std::mutex _background_mutex;
  std::condition_variable _background_cv;
  bool _background_stop = false;
  std::atomic<bool> _checkpoint_requested{false};
  absl::Status _background_error;

  // Under the writer lock.
  std::vector<std::shared_ptr<internal::Subscriber>> _subscribers;
  // How many queued subscribers are over their limit; new writers wait while it is above zero.
  std::atomic<int> _subscribers_over_limit{0};
  std::mutex _room_mutex;
  std::condition_variable _room_cv;

  mutable std::mutex _snapshots_mutex;
  mutable absl::btree_map<uint64_t, uint32_t> _snapshots;  // Pinned seq to reader count.
};

// A live subscription: destroying it cancels it. Do that outside its own callback.
class Subscription {
 public:
  Subscription() = default;
  ~Subscription() { Cancel(); }
  Subscription(Subscription&&) noexcept = default;
  Subscription& operator=(Subscription&& other) noexcept {
    Cancel();
    _subscriber = std::move(other._subscriber);
    return *this;
  }

  // Waits for a callback in progress. Undelivered queued changes are dropped.
  void Cancel();
  // Blocks until the callback has seen every commit made before this call.
  void WaitUntilDelivered() const;

 private:
  friend class Engine;
  explicit Subscription(std::shared_ptr<internal::Subscriber> subscriber)
      : _subscriber(std::move(subscriber)) {}

  std::shared_ptr<internal::Subscriber> _subscriber;
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
  std::string_view key() const noexcept { return _key; }
  BytesConstView value() const noexcept { return *_value; }
  void Next() noexcept;

 private:
  uint64_t _seq;
  const internal::RecordTable::Slots* _slots;
  size_t _next = 0;
  std::string_view _key;
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
  friend class Engine;
  friend class IndexCursor;

  struct PendingWrite {
    size_t collection;
    uint64_t hash;
    std::string key;
    internal::Version* version;  // Owned until the commit publishes it; null if it changes nothing.
    internal::RecordNode* node;  // Set when the commit starts; null for a record not yet in memory.
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
  // Drops deletions of keys that do not exist. Returns whether anything is left to commit, and
  // whether it reaches the log.
  std::pair<bool, bool> ResolveWrites() noexcept;
  // Links the writes into memory and publishes them. `frame` is the encoded commit, if logged.
  void Publish(bool logged, uint64_t lsn, std::string_view frame);
  // Commits writes that came from a log, already durable or about to be.
  void CommitReplayed(uint64_t lsn, std::string_view frame);

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
