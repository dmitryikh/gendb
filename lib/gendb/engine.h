#pragma once

#include <array>
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
#include "gendb/file_system.h"
#include "gendb/log_format.h"
#include "gendb/message.h"
#include "gendb/observer.h"
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
  // At most one record per value: a write that would make a second fails with AlreadyExists.
  bool unique = false;
};

struct EngineSchema {
  std::vector<CollectionDef> collections;
  std::vector<IndexDef> indices;
};

struct EngineOptions {
  // Database directory, created if missing. Open it from one process at a time.
  std::string path;
  // Where it lives; the real disk by default.
  std::shared_ptr<FileSystem> file_system;
  // Logs and metrics. See EngineObserver.
  std::shared_ptr<EngineObserver> observer;

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

  // Threads to build indices with when opening; zero means the hardware's, up to 8.
  unsigned recovery_threads = 0;

  // Follows a primary through a Replica: only ephemeral collections accept local commits.
  bool replica = false;
  // Durable data of a collection the schema does not have is normally kept, out of reach, until a
  // schema that has it opens the database; EngineStats::foreign_collections lists such collections.
  // With this set, their data is ignored, and the next checkpoint drops it for good. Not allowed on
  // a replica, which would then differ from its primary without knowing.
  bool discard_unknown_collections = false;
};

struct CommitOptions {
  // Overrides EngineOptions::sync_commits.
  std::optional<bool> sync;
};

enum class ChangeKind {
  kInsert,
  kUpdate,
  kDelete,
  // Catching up with SubscribeFrom: the record's current state, which may or may not be new to the
  // subscriber.
  kUpsert,
};

// One record changed by a commit: its whole state before and after. Comparing them tells what
// changed, for a Put as much as for an Update; generated code offers it per field.
struct Change {
  size_t collection;
  BytesConstView key;
  std::optional<BytesConstView> before;  // Absent on insert.
  std::optional<BytesConstView> after;   // Absent on delete.
  // From catching up: `before` is not known, even for a delete.
  bool state_only = false;

  ChangeKind kind() const noexcept {
    if (!after.has_value()) return ChangeKind::kDelete;
    if (before.has_value()) return ChangeKind::kUpdate;
    return state_only ? ChangeKind::kUpsert : ChangeKind::kInsert;
  }
};

// A point in the commit log, to resume following it with Engine::SubscribeFrom. Plain numbers, for
// the application to store.
struct ChangePosition {
  uint64_t history_high = 0;
  uint64_t history_low = 0;
  uint64_t lsn = 0;
  bool operator==(const ChangePosition&) const = default;
};

// The views are valid only during the callback.
struct CommitEvent {
  uint64_t seq;  // An ever-growing commit number within this process.
  std::span<const Change> changes;
  // Where to resume from to get what comes after this event.
  ChangePosition position;
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
  std::vector<std::string> foreign_collections;  // Held, but not in this schema.
};

struct BackupInfo {
  uint64_t lsn;
  uint64_t bytes;
};

class Engine {
 public:
  // In memory only: there is nothing to recover, and all data is ephemeral.
  explicit Engine(EngineSchema schema, std::shared_ptr<EngineObserver> observer = nullptr);
  // Loads the latest checkpoint from `options.path`, then replays the log after it.
  static absl::StatusOr<std::unique_ptr<Engine>> Open(EngineSchema schema, EngineOptions options);
  ~Engine();
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  // Calls `callback` with each commit that changes one of the chosen collections, leaving out the
  // others. Changes committed before this call are not delivered. The subscription ends with the
  // returned handle.
  Subscription Subscribe(ChangeCallback callback, SubscribeOptions options = {});
  // The same, starting with what changed since `position`, for a subscriber that was away.
  //
  // The log holds no history of each record that a subscriber could be given in full, so catching
  // up is compacted: one change per record that changed, kUpsert with its current state or kDelete
  // with only its key. Live changes follow, with nothing missed. Scans the log before returning,
  // and takes only Delivery::kQueued.
  //
  // OutOfRange if the log no longer reaches back that far, or the database is on another history,
  // as after a replica received a whole checkpoint: start over from a snapshot and Subscribe.
  // Ephemeral collections are not in the log, so nothing is reported from them.
  absl::StatusOr<Subscription> SubscribeFrom(const ChangePosition& position,
                                             ChangeCallback callback,
                                             SubscribeOptions options = {});
  // Where the log is now.
  ChangePosition CurrentChangePosition();

  // Makes every finished commit durable.
  absl::Status SyncWal();
  // Writes the current state to a new checkpoint and removes the log it supersedes. Writers and
  // readers keep running, though versions replaced meanwhile stay in memory until it ends. Blocks
  // until any open Transaction ends, so do not call it while holding one.
  absl::Status Checkpoint();
  // Writes the current state to a new checkpoint in `directory`, which must be empty or not exist,
  // without stopping writers or readers. Opening that directory restores the database as of then.
  // Works on a memory-only engine too.
  absl::StatusOr<BackupInfo> Backup(const std::string& directory);

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

  // Collections are numbered as in the schema; foreign ones, not in it, follow. They have no
  // indices, nobody can read them through the API, and subscribers do not hear of them, but they
  // are logged, checkpointed and replicated like any other.
  struct Collection {
    internal::RecordTable table;
    std::vector<size_t> indices;
    uint32_t id;  // In frames.
    std::string name;
    bool ephemeral;
  };
  static constexpr size_t kMaxForeignCollections = 256;
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
  // Fills all indices from the tables, while opening.
  void BuildIndices(uint64_t seq);
  // Returns the snapshot's sequence number, and its slot, or -1 if it took the overflow.
  uint64_t PinSnapshot(int32_t* slot) const;
  void UnpinSnapshot(uint64_t seq, int32_t slot) const noexcept;
  // The oldest pinned sequence number, or `newest` if none is older.
  uint64_t OldestPinned(uint64_t newest) const noexcept;
  std::optional<BytesConstView> ReadAt(size_t collection, uint64_t hash, std::string_view key,
                                       uint64_t seq) const noexcept;
  internal::RecordNode* NodeForWrite(size_t collection, uint64_t hash, std::string_view key,
                                     uint64_t seq);
  void Reclaim(uint64_t up_to_seq) noexcept;
  void ReclaimUnpinned(uint64_t seq) noexcept;
  // The collection whose durable data has this ID: one that is ephemeral here does not count, as
  // its records are local. Under the writer lock.
  std::optional<size_t> FindDurableCollection(uint32_t id) const noexcept;
  // Under the writer lock.
  absl::StatusOr<size_t> AddForeignCollection(uint32_t id, std::string_view name);
  size_t collection_count() const noexcept {
    return _collection_count.load(std::memory_order_acquire);
  }

  // Writer thread.
  std::unique_lock<std::mutex> AcquireWriter();
  // For brief maintenance, such as a checkpoint's start: goes ahead of the transactions waiting,
  // as a busy writer retaking the mutex could otherwise keep it away for good.
  std::unique_lock<std::mutex> AcquireWriterFirst();
  void Notify(uint64_t seq, std::span<const Change> changes);
  ChangePosition ChangePositionAt(uint64_t lsn) const noexcept;
  void AfterCommit(uint64_t wal_bytes);
  void UpdateIndexNow(size_t collection, std::string_view key, std::optional<BytesConstView> before,
                      std::optional<BytesConstView> after, uint64_t seq);
  // Replays one commit frame through `txn`.
  absl::Status ApplyCommitFrame(Transaction& txn, const internal::Frame& frame);

  // Compaction.
  absl::Status ExportCheckpoint(const EngineSnapshot& snapshot, const LogPosition& position,
                                const std::function<absl::Status(std::string_view)>& sink) const;
  // Writes a checkpoint file in `directory`, returning its size.
  absl::StatusOr<uint64_t> WriteCheckpointFile(const std::string& directory,
                                               const EngineSnapshot& snapshot,
                                               const LogPosition& position);
  absl::Status PurgeWal();
  void RemoveCheckpointsBelow(uint64_t lsn) const;
  void BackgroundLoop();
  void Log(LogLevel level, std::string_view message) const;
  void RecordBackgroundError(absl::Status status);

  // Subscriptions.
  std::shared_ptr<internal::Subscriber> NewSubscriber(ChangeCallback callback,
                                                      const SubscribeOptions& options);
  void Unsubscribe(const std::shared_ptr<internal::Subscriber>& subscriber);
  void WaitForSubscriberRoom();
  void StopSubscribers();

  // Replication, for ReplicationServer and Replica.
  void EnableCommitRing(size_t capacity_bytes);
  LogPosition Position();
  uint64_t history_generation() const noexcept {
    return _history_generation.load(std::memory_order_acquire);
  }
  // Makes sure there is somewhere to keep each of the primary's collections.
  absl::Status RegisterCollections(
      const std::vector<std::pair<uint32_t, std::string>>& collections);
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
  // Fixed in size, so that adding a foreign collection does not move what readers are using.
  std::vector<std::unique_ptr<Collection>> _collections;
  std::atomic<size_t> _collection_count{0};
  std::vector<std::pair<uint32_t, size_t>> _durable_ids;  // Sorted. Under the writer lock.
  std::vector<std::unique_ptr<internal::OrderedIndex>> _indices;

  std::mutex _writer_mutex;  // Held by a Transaction for its whole life.
  std::atomic<int> _writers_going_first{0};
  std::atomic<uint64_t> _committed_seq{0};
  std::deque<Garbage> _garbage;  // Writer only, ordered by seq.
  std::atomic<size_t> _garbage_size{0};
  std::string _commit_buffer;                  // Writer only.
  std::string _old_index_key, _new_index_key;  // Writer only: scratch for UpdateIndexNow.
  uint32_t _install_epoch = 0;                 // Writer only.

  std::atomic<uint64_t> _lsn{0};                 // Written under the writer lock.
  internal::HistoryId _history;                  // Under the writer lock.
  std::atomic<uint64_t> _history_generation{0};  // Changes with the history or the collections.
  std::atomic<bool> _replica;
  // A checkpoint failed to load halfway: memory holds records no commit may publish.
  std::atomic<bool> _broken{false};
  const std::shared_ptr<FileSystem> _file_system;
  std::unique_ptr<CommitRing> _ring;

  // Persistence; null in memory.
  std::unique_ptr<internal::Wal> _wal;
  std::unique_ptr<FileLock> _file_lock;
  std::mutex _checkpoint_mutex;  // Serializes checkpoints. Taken before the writer lock.
  std::atomic<uint64_t> _checkpoint_lsn{0};
  std::atomic<uint64_t> _checkpoint_bytes{0};
  std::atomic<uint64_t> _wal_bytes_since_checkpoint{0};
  std::atomic<bool> _rewrite_checkpoint{false};  // Something was discarded on open.
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

  // While opening, indices wait to be built in one go.
  bool _indices_deferred = false;

  // Pinned snapshots, one per slot, so that threads do not contend. A slot holds seq + 1, or 0 if
  // free. Pinning and reclaiming follow the same pattern from opposite sides, with sequentially
  // consistent atomics: the writer publishes a seq and then reads the slots, and a reader writes
  // its slot and then reads the seq again. So either the writer sees the pin, or the reader sees
  // the seq and moves its pin up to it.
  struct alignas(64) SnapshotSlot {
    std::atomic<uint64_t> pinned{0};
  };
  static constexpr size_t kSnapshotSlots = 64;
  mutable std::array<SnapshotSlot, kSnapshotSlots> _snapshot_slots;
  // No slot at or above this has been used, which spares the writer from reading them all.
  mutable std::atomic<size_t> _snapshot_slots_used{0};
  // For more snapshots at once than there are slots.
  mutable std::mutex _overflow_mutex;
  mutable absl::btree_map<uint64_t, uint32_t> _overflow_snapshots;  // Pinned seq to reader count.
  mutable std::atomic<size_t> _overflow_count{0};
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
  explicit EngineSnapshot(const Engine& engine)
      : _engine(&engine), _seq(engine.PinSnapshot(&_slot)) {}
  ~EngineSnapshot() {
    if (_engine != nullptr) _engine->UnpinSnapshot(_seq, _slot);
  }
  EngineSnapshot(EngineSnapshot&& other) noexcept
      : _engine(other._engine), _slot(other._slot), _seq(other._seq) {
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
  int32_t _slot = -1;  // Before _seq, which sets it.
  uint64_t _seq;
};

struct ScanOptions {
  bool reverse = false;  // Each step of a reverse scan costs a search rather than a pointer.
  size_t limit = 0;      // At most this many results; zero means no limit.
};

// Ordered scan of an index over [lo, hi); an empty `hi` means no upper bound. Over a transaction
// it includes the transaction's uncommitted changes.
class IndexCursor {
 public:
  IndexCursor(const EngineSnapshot& snapshot, size_t index, std::string_view lo, std::string hi,
              ScanOptions options = {});
  IndexCursor(const Transaction& txn, size_t index, std::string_view lo, std::string hi,
              ScanOptions options = {});
  bool Valid() const noexcept { return _value.has_value(); }
  BytesConstView value() const noexcept { return *_value; }
  std::string_view primary_key() const noexcept { return _primary_key; }
  void Next();

 private:
  struct Delta {
    std::string key;
    bool added;
    uint32_t secondary_size;
  };

  IndexCursor(const Engine& engine, size_t index, uint64_t seq, const Transaction* txn,
              std::string_view lo, std::string hi, ScanOptions options);
  const internal::IndexEntry* NextVisible(const internal::IndexEntry* e) const noexcept;
  // The last visible entry with key below `bound`, or the last of all if `bound` is empty.
  const internal::IndexEntry* PreviousVisible(std::string_view bound) const noexcept;
  std::optional<BytesConstView> Lookup(std::string_view primary_key) const noexcept;

  const Engine* _engine;
  size_t _collection;
  uint64_t _seq;
  const Transaction* _txn;
  const internal::OrderedIndex* _index;
  std::string _lo;
  std::string _hi;
  ScanOptions _options;
  size_t _returned = 0;
  std::string_view _primary_key;
  const internal::IndexEntry* _entry;
  std::vector<Delta> _deltas;  // Uncommitted changes in range, in key order.
  size_t _next_delta = 0;
  std::optional<BytesConstView> _value;
};

// All records of a collection, in no particular order. Over a transaction it includes the
// transaction's uncommitted changes, and writing or committing during the scan ends its validity.
class ScanCursor {
 public:
  ScanCursor(const EngineSnapshot& snapshot, size_t collection);
  ScanCursor(const Transaction& txn, size_t collection);
  bool Valid() const noexcept { return _value.has_value(); }
  std::string_view key() const noexcept { return _key; }
  BytesConstView value() const noexcept { return *_value; }
  void Next() noexcept;

 private:
  uint64_t _seq;
  const internal::RecordTable::Slots* _slots;
  size_t _next = 0;
  const Transaction* _txn = nullptr;  // Set for a scan with uncommitted changes.
  size_t _collection = 0;
  size_t _next_write = 0;
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
  absl::Status Put(size_t collection, BytesConstView key, BytesConstView value);
  absl::Status Update(size_t collection, BytesConstView key, const MessagePatch& patch);
  absl::Status Delete(size_t collection, BytesConstView key);

  // On failure nothing is applied and the transaction is rolled back. The transaction can be used
  // again afterwards.
  absl::Status Commit(CommitOptions options = {});
  void Rollback() noexcept;

 private:
  friend class Engine;
  friend class IndexCursor;
  friend class ScanCursor;

  struct PendingWrite {
    size_t collection;
    uint64_t hash;
    std::string key;
    internal::Version* version;  // Owned until the commit publishes it; null if it changes nothing.
    internal::RecordNode* node;  // Set when the commit starts; null for a record not yet in memory.
    // The whole change against the committed record, encoded, if it can be logged as a patch.
    std::string patch;
  };
  struct IndexDelta {
    bool added;
    uint32_t secondary_size;
  };
  using PendingMap = std::unordered_map<std::string_view, PendingWrite*>;

  PendingWrite* FindPending(size_t collection, std::string_view key) const noexcept;
  void SetPending(size_t collection, uint64_t hash, std::string_view key, internal::Version* value,
                  std::string patch = {});
  // Replaying a log does not check unique indices: the primary did, and its schema decides.
  void PutUnchecked(size_t collection, BytesConstView key, BytesConstView value);
  absl::Status CheckUnique(size_t collection, std::string_view key, BytesConstView value) const;
  // Whether the log would hold this collection's changes.
  bool IsLogged(size_t collection) const noexcept;
  // Replays a logged patch, and checks that it gives the record the primary had.
  absl::Status ApplyLoggedPatch(size_t collection, std::string_view key, const MessagePatch& patch,
                                uint32_t result_crc);
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
