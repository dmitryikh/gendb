#include "gendb/engine.h"

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <utility>

#include "absl/hash/hash.h"
#include "absl/strings/str_cat.h"
#include "gendb/checkpoint.h"
#include "gendb/status.h"
#include "gendb/wal.h"

namespace gendb {

using internal::AsStringView;
using internal::CheckpointStage;
using internal::CommitDecoder;
using internal::Frame;
using internal::FrameFileReader;
using internal::FrameType;
using internal::HistoryId;
using internal::IndexEntry;
using internal::NumberedPath;
using internal::OrderedIndex;
using internal::ParseStatus;
using internal::RecordNode;
using internal::RecordTable;
using internal::Version;
using internal::Wal;
using internal::WalCursor;

namespace {

constexpr std::string_view kCheckpointPrefix = "checkpoint-";
constexpr std::string_view kCheckpointSuffix = ".gdb";
constexpr std::string_view kTempSuffix = ".tmp";

uint64_t HashKey(std::string_view key) noexcept {
  return absl::HashOf(key);
}
BytesConstView AsBytes(std::string_view s) noexcept {
  return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}
bool SameBytes(BytesConstView a, std::string_view b) noexcept {
  return a.size() == b.size() && (b.empty() || std::memcmp(a.data(), b.data(), b.size()) == 0);
}

thread_local bool t_on_subscriber_thread = false;

}  // namespace

// Commits ready for replicas, kept in memory so that following the primary touches no disk.
class Engine::CommitRing {
 public:
  bool enabled() const noexcept { return _enabled.load(std::memory_order_relaxed); }

  void Enable(size_t capacity_bytes, uint64_t lsn) {
    std::lock_guard lock(_mutex);
    _capacity = std::max(_capacity, capacity_bytes);
    if (!_enabled.exchange(true)) _latest = lsn;
  }

  void Push(uint64_t lsn, std::string_view frame) {
    {
      std::lock_guard lock(_mutex);
      _entries.push_back({lsn, std::string(frame)});
      _bytes += frame.size();
      _latest = lsn;
      while (_bytes > _capacity && _entries.size() > 1) {
        _bytes -= _entries.front().frame.size();
        _entries.pop_front();
      }
    }
    _cv.notify_all();
  }

  // Forgets the commits before a new state.
  void Reset(uint64_t lsn) {
    {
      std::lock_guard lock(_mutex);
      _entries.clear();
      _bytes = 0;
      _latest = lsn;
    }
    _cv.notify_all();
  }

  uint64_t latest() const {
    std::lock_guard lock(_mutex);
    return _latest;
  }

  bool Contains(uint64_t lsn) const {
    std::lock_guard lock(_mutex);
    return !_entries.empty() && _entries.front().lsn <= lsn && lsn <= _entries.back().lsn;
  }

  // Appends frames with LSN in (*position, limit], at least one if any, until `out` grows by
  // `max_bytes`. False if the next commit is not held here.
  bool Read(uint64_t* position, uint64_t limit, size_t max_bytes, std::string* out) const {
    std::lock_guard lock(_mutex);
    if (_entries.empty() || *position + 1 < _entries.front().lsn) return false;
    const size_t start_size = out->size();
    size_t at = *position + 1 - _entries.front().lsn;
    while (at < _entries.size() && _entries[at].lsn <= limit &&
           out->size() - start_size < max_bytes) {
      out->append(_entries[at].frame);
      *position = _entries[at].lsn;
      ++at;
    }
    return true;
  }

  void Wait(uint64_t after, std::chrono::milliseconds timeout) const {
    std::unique_lock lock(_mutex);
    const uint64_t wakeups = _wakeups;
    _cv.wait_for(lock, timeout, [&] { return _latest > after || _wakeups != wakeups; });
  }

  void Wake() const {
    {
      std::lock_guard lock(_mutex);
      ++_wakeups;
    }
    _cv.notify_all();
  }

 private:
  struct Entry {
    uint64_t lsn;
    std::string frame;
  };

  std::atomic<bool> _enabled{false};
  mutable std::mutex _mutex;
  mutable std::condition_variable _cv;
  mutable uint64_t _wakeups = 0;
  std::deque<Entry> _entries;  // Contiguous LSNs.
  size_t _bytes = 0;
  size_t _capacity = 0;
  uint64_t _latest = 0;
};

namespace internal {

// One subscription's delivery state.
struct Subscriber {
  struct QueuedCommit {
    uint64_t seq;
    std::vector<Change> changes;  // Views into `arena`.
    std::string arena;
    size_t bytes;
  };

  Engine* engine;  // Null once the subscription or the engine has ended.
  ChangeCallback callback;
  std::vector<bool> wanted;  // By collection; empty means all.
  Delivery delivery;
  size_t max_queue_bytes;
  std::vector<Change> scratch;  // Writer only.

  mutable std::mutex mutex;
  mutable std::condition_variable has_work;
  mutable std::condition_variable delivered_cv;
  std::deque<QueuedCommit> queue;
  size_t queue_bytes = 0;
  uint64_t pushed = 0;
  uint64_t delivered = 0;
  bool over_limit = false;
  bool stop = false;
  std::thread thread;

  void Deliver(uint64_t seq, std::span<const Change> changes);
  void Enqueue(uint64_t seq, std::span<const Change> changes);
  void Run();
  void StopThread();
  void SetOverLimit(bool value);  // Holding `mutex`.
};

void Subscriber::Deliver(uint64_t seq, std::span<const Change> changes) {
  std::span<const Change> selected = changes;
  if (!wanted.empty()) {
    scratch.clear();
    for (const Change& change : changes) {
      if (wanted[change.collection]) scratch.push_back(change);
    }
    if (scratch.empty()) return;
    selected = scratch;
  }
  if (delivery == Delivery::kInline) {
    callback(CommitEvent{seq, selected});
  } else {
    Enqueue(seq, selected);
  }
}

void Subscriber::Enqueue(uint64_t seq, std::span<const Change> changes) {
  size_t arena_size = 0;
  for (const Change& change : changes) {
    arena_size += change.key.size();
    if (change.before.has_value()) arena_size += change.before->size();
    if (change.after.has_value()) arena_size += change.after->size();
  }
  QueuedCommit item{seq, {}, {}, arena_size + changes.size() * sizeof(Change)};
  item.arena.reserve(arena_size);  // Never reallocates below, so the views stay valid.
  item.changes.reserve(changes.size());
  const auto copy = [&item](BytesConstView bytes) {
    const size_t at = item.arena.size();
    item.arena.append(AsStringView(bytes));
    return AsBytes(std::string_view(item.arena).substr(at, bytes.size()));
  };
  for (const Change& change : changes) {
    Change owned{change.collection, copy(change.key), std::nullopt, std::nullopt};
    if (change.before.has_value()) owned.before = copy(*change.before);
    if (change.after.has_value()) owned.after = copy(*change.after);
    item.changes.push_back(owned);
  }
  {
    std::lock_guard lock(mutex);
    queue_bytes += item.bytes;
    queue.push_back(std::move(item));
    ++pushed;
    if (!over_limit && queue_bytes >= max_queue_bytes) SetOverLimit(true);
  }
  has_work.notify_one();
}

void Subscriber::Run() {
  t_on_subscriber_thread = true;
  std::unique_lock lock(mutex);
  for (;;) {
    has_work.wait(lock, [this] { return stop || !queue.empty(); });
    if (stop) return;
    const QueuedCommit item = std::move(queue.front());
    queue.pop_front();
    lock.unlock();
    callback(CommitEvent{item.seq, item.changes});
    lock.lock();
    queue_bytes -= item.bytes;
    ++delivered;
    if (over_limit && queue_bytes <= max_queue_bytes / 2) SetOverLimit(false);
    delivered_cv.notify_all();
  }
}

void Subscriber::SetOverLimit(bool value) {
  over_limit = value;
  if (value) {
    engine->_subscribers_over_limit.fetch_add(1, std::memory_order_acq_rel);
    return;
  }
  {
    std::lock_guard room(engine->_room_mutex);
    engine->_subscribers_over_limit.fetch_sub(1, std::memory_order_acq_rel);
  }
  engine->_room_cv.notify_all();
}

void Subscriber::StopThread() {
  {
    std::lock_guard lock(mutex);
    stop = true;
    if (over_limit) SetOverLimit(false);
    engine = nullptr;
  }
  has_work.notify_all();
  delivered_cv.notify_all();
  if (thread.joinable()) thread.join();
}

}  // namespace internal

std::string PrefixSuccessor(std::string_view prefix) {
  std::string next(prefix);
  while (!next.empty()) {
    if (static_cast<uint8_t>(next.back()) != 0xff) {
      next.back() = static_cast<char>(static_cast<uint8_t>(next.back()) + 1);
      return next;
    }
    next.pop_back();
  }
  return next;
}

Engine::Engine(EngineSchema schema) : Engine(std::move(schema), EngineOptions{}) {}

Engine::Engine(EngineSchema schema, EngineOptions options)
    : _schema(std::move(schema)),
      _options(std::move(options)),
      _replica(_options.replica),
      _ring(std::make_unique<CommitRing>()) {
  for (size_t i = 0; i < _schema.collections.size(); ++i) {
    _collections.push_back(std::make_unique<Collection>());
    _collections[i]->id = internal::CollectionIdOf(_schema.collections[i].name);
    _collection_ids.emplace_back(_collections[i]->id, i);
  }
  std::sort(_collection_ids.begin(), _collection_ids.end());
  for (size_t i = 1; i < _collection_ids.size(); ++i) {
    if (_collection_ids[i - 1].first == _collection_ids[i].first) {
      std::fprintf(stderr, "gendb: collections '%s' and '%s' have the same ID: rename one\n",
                   _schema.collections[_collection_ids[i - 1].second].name.c_str(),
                   _schema.collections[_collection_ids[i].second].name.c_str());
      std::abort();
    }
  }
  for (size_t i = 0; i < _schema.indices.size(); ++i) {
    _indices.push_back(std::make_unique<OrderedIndex>());
    _collections[_schema.indices[i].collection]->indices.push_back(i);
  }
  _history = HistoryId::Random();
}

absl::StatusOr<std::unique_ptr<Engine>> Engine::Open(EngineSchema schema, EngineOptions options) {
  if (options.path.empty()) return absl::InvalidArgumentError("EngineOptions::path is empty");
  std::unique_ptr<Engine> engine(new Engine(std::move(schema), std::move(options)));
  RETURN_IF_ERROR(engine->Recover());
  const EngineOptions& opened = engine->_options;
  if (opened.checkpoint_wal_bytes > 0 || opened.background_sync_interval.count() > 0) {
    engine->_background = std::thread([raw = engine.get()] { raw->BackgroundLoop(); });
  }
  return engine;
}

// Loads a checkpoint into the live tables under one sequence number, so that readers keep seeing
// the old state until it is published. A record that did not change keeps its version, and any
// durable record the checkpoint lacks is removed.
class Engine::CheckpointLoader final : public internal::CheckpointHandler {
 public:
  // With `seq` 0, the engine is recovering, and uses the checkpoint's LSN.
  CheckpointLoader(Engine& engine, uint64_t seq, std::vector<Change>* changes)
      : _engine(engine), _seq(seq), _epoch(++engine._install_epoch), _changes(changes) {}

  uint64_t seq() const noexcept { return _seq; }
  const LogPosition& position() const noexcept { return _position; }

  absl::Status OnHeader(const HistoryId& history, uint64_t lsn) override {
    _position = {history, lsn};
    if (_seq == 0) _seq = lsn;
    return absl::OkStatus();
  }

  absl::Status OnCollection(uint32_t id, std::string_view name, uint64_t expected) override {
    _current = _engine.FindCollection(id);
    if (!_current.has_value() && !_engine._options.discard_unknown_collections) {
      return absl::FailedPreconditionError(
          absl::StrCat("The database holds collection '", name,
                       "' that the schema does not have. Restore it, or set "
                       "discard_unknown_collections to drop it."));
    }
    if (_current.has_value() && _engine._schema.collections[*_current].ephemeral) {
      _current.reset();
    }
    if (_current.has_value()) {
      const RecordTable::Slots* replaced = _engine._collections[*_current]->table.Reserve(expected);
      if (replaced != nullptr) {
        _engine._garbage.push_back(
            {_seq, Garbage::kSlots, const_cast<RecordTable::Slots*>(replaced), 0});
      }
    }
    return absl::OkStatus();
  }

  void OnRecord(std::string_view key, std::string_view value) override {
    if (!_current.has_value()) return;
    const size_t c = *_current;
    const uint64_t hash = HashKey(key);
    RecordNode* node = _engine._collections[c]->table.Find(hash, key);
    Version* head = node == nullptr ? nullptr : node->head.load(std::memory_order_relaxed);
    const bool existed = head != nullptr && !head->tombstone;
    if (existed && SameBytes(head->bytes(), value)) {
      node->install_epoch = _epoch;
      return;
    }
    if (node == nullptr) node = _engine.NodeForWrite(c, hash, key, _seq);
    Version* version = Version::Copy(AsBytes(value));
    Link(c, node, version, head);
    if (existed) {
      _engine.UpdateIndexNow(c, node->key(), head->bytes(), version->bytes(), _seq);
    } else {
      _engine.UpdateIndexNow(c, node->key(), std::nullopt, version->bytes(), _seq);
    }
  }

  // Removes durable records that were not in the checkpoint.
  void RemoveMissing() {
    for (size_t c = 0; c < _engine._collections.size(); ++c) {
      if (_engine._schema.collections[c].ephemeral) continue;
      const RecordTable::Slots* slots = _engine._collections[c]->table.LoadSlots();
      for (size_t i = 0; i <= slots->mask; ++i) {
        RecordNode* node = slots->slots[i].load(std::memory_order_relaxed);
        if (node == nullptr || node == RecordTable::Erased()) continue;
        if (node->install_epoch == _epoch) continue;
        Version* head = node->head.load(std::memory_order_relaxed);
        if (head == nullptr || head->tombstone) continue;
        Link(c, node, Version::Create(0, true), head);
        _engine.UpdateIndexNow(c, node->key(), head->bytes(), std::nullopt, _seq);
      }
    }
  }

 private:
  void Link(size_t collection, RecordNode* node, Version* version, Version* head) {
    version->seq = _seq;
    version->older = head;
    node->head.store(version, std::memory_order_release);
    node->install_epoch = _epoch;
    if (head != nullptr) _engine._garbage.push_back({_seq, Garbage::kOlderVersion, version, 0});
    if (version->tombstone) {
      _engine._garbage.push_back({_seq, Garbage::kTombstone, node, collection});
    }
    if (_changes != nullptr) {
      const bool had = head != nullptr && !head->tombstone;
      _changes->push_back(
          {collection, AsBytes(node->key()),
           had ? std::optional<BytesConstView>(head->bytes()) : std::nullopt,
           version->tombstone ? std::nullopt : std::optional<BytesConstView>(version->bytes())});
    }
  }

  Engine& _engine;
  uint64_t _seq;
  uint32_t _epoch;
  std::vector<Change>* _changes;
  LogPosition _position{};
  std::optional<size_t> _current;
};

absl::Status Engine::Recover() {
  const std::string& path = _options.path;
  std::error_code error;
  std::filesystem::create_directories(path, error);
  if (error) return absl::InternalError(absl::StrCat("create ", path, ": ", error.message()));

  const std::string lock_path = absl::StrCat(path, "/LOCK");
  _lock_fd = open(lock_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
  if (_lock_fd < 0) return internal::ErrnoStatus("open", lock_path);
  if (flock(_lock_fd, LOCK_EX | LOCK_NB) != 0) {
    return absl::FailedPreconditionError(absl::StrCat(path, " is open in another engine"));
  }

  // Unfinished work of a previous run.
  for (const auto& entry : std::filesystem::directory_iterator(path, error)) {
    if (entry.path().string().ends_with(kTempSuffix)) std::filesystem::remove(entry.path(), error);
  }

  internal::CollectionNames names;
  for (size_t c = 0; c < _collections.size(); ++c) {
    if (!_schema.collections[c].ephemeral) {
      names.emplace_back(_collections[c]->id, _schema.collections[c].name);
    }
  }
  _wal = std::make_unique<Wal>(Wal::Options{path, _options.wal_segment_bytes, std::move(names)});

  auto checkpoints = internal::ListNumbered(path, kCheckpointPrefix, kCheckpointSuffix);
  if (!checkpoints.ok()) return checkpoints.status();
  std::optional<HistoryId> history;
  uint64_t lsn = 0;
  if (!checkpoints->empty()) {
    lsn = checkpoints->back();
    const std::string file = NumberedPath(path, kCheckpointPrefix, lsn, kCheckpointSuffix);
    auto reader = FrameFileReader::Open(file);
    if (!reader.ok()) return reader.status();
    CheckpointLoader loader(*this, /*seq=*/0, /*changes=*/nullptr);
    if (absl::Status status = internal::ReadCheckpoint(**reader, loader); !status.ok()) {
      return absl::Status(status.code(), absl::StrCat(file, ": ", status.message()));
    }
    if (loader.position().lsn != lsn) return absl::DataLossError(absl::StrCat("LSN of ", file));
    history = loader.position().history;
    _lsn.store(lsn, std::memory_order_relaxed);
    _committed_seq.store(lsn, std::memory_order_relaxed);
    _checkpoint_lsn.store(lsn, std::memory_order_relaxed);
    _checkpoint_bytes.store(std::filesystem::file_size(file, error), std::memory_order_relaxed);
    Reclaim(lsn);  // Nothing has read this engine yet.
  }

  {
    Transaction txn(*this);
    auto recovered = _wal->Recover(
        lsn, [this, &txn](const Frame& frame) { return ApplyCommitFrame(txn, frame); });
    if (!recovered.ok()) return recovered.status();
    lsn = recovered->last_lsn;
    if (recovered->history.has_value()) history = recovered->history;
    _wal_bytes_since_checkpoint.store(recovered->bytes_replayed, std::memory_order_relaxed);
  }
  _history = history.value_or(_history);
  RETURN_IF_ERROR(_wal->StartSegment(_history, lsn + 1));
  return PurgeWal();
}

Engine::~Engine() {
  {
    std::lock_guard lock(_background_mutex);
    _background_stop = true;
  }
  _background_cv.notify_all();
  if (_background.joinable()) _background.join();
  for (const auto& subscriber : _subscribers) subscriber->StopThread();
  if (_wal != nullptr) (void)_wal->Sync();
  Reclaim(internal::kNeverDeleted);
  if (_lock_fd >= 0) close(_lock_fd);
}

std::unique_lock<std::mutex> Engine::AcquireWriter() {
  WaitForSubscriberRoom();
  while (_writers_going_first.load(std::memory_order_acquire) > 0) {
    std::this_thread::sleep_for(std::chrono::microseconds(50));
  }
  return std::unique_lock(_writer_mutex);
}

std::unique_lock<std::mutex> Engine::AcquireWriterFirst() {
  _writers_going_first.fetch_add(1, std::memory_order_acq_rel);
  std::unique_lock lock(_writer_mutex);
  _writers_going_first.fetch_sub(1, std::memory_order_acq_rel);
  return lock;
}

void Engine::WaitForSubscriberRoom() {
  if (_subscribers_over_limit.load(std::memory_order_acquire) == 0 || t_on_subscriber_thread) {
    return;
  }
  std::unique_lock lock(_room_mutex);
  _room_cv.wait(lock,
                [this] { return _subscribers_over_limit.load(std::memory_order_acquire) == 0; });
}

Subscription Engine::Subscribe(ChangeCallback callback, SubscribeOptions options) {
  auto subscriber = std::make_shared<internal::Subscriber>();
  subscriber->engine = this;
  subscriber->callback = std::move(callback);
  subscriber->delivery = options.delivery;
  subscriber->max_queue_bytes = options.max_queue_bytes;
  if (!options.collections.empty()) {
    subscriber->wanted.resize(_collections.size(), false);
    for (size_t collection : options.collections) subscriber->wanted[collection] = true;
  }
  if (options.delivery == Delivery::kQueued) {
    subscriber->thread = std::thread([raw = subscriber.get()] { raw->Run(); });
  }
  auto writer = AcquireWriterFirst();
  _subscribers.push_back(subscriber);
  return Subscription(std::move(subscriber));
}

void Engine::Unsubscribe(const std::shared_ptr<internal::Subscriber>& subscriber) {
  {
    auto writer = AcquireWriterFirst();
    std::erase(_subscribers, subscriber);
  }
  subscriber->StopThread();
}

void Subscription::Cancel() {
  if (_subscriber == nullptr) return;
  Engine* engine;
  {
    std::lock_guard lock(_subscriber->mutex);
    engine = _subscriber->engine;
  }
  if (engine != nullptr) engine->Unsubscribe(_subscriber);
  _subscriber.reset();
}

void Subscription::WaitUntilDelivered() const {
  if (_subscriber == nullptr || _subscriber->delivery == Delivery::kInline) return;
  std::unique_lock lock(_subscriber->mutex);
  const uint64_t target = _subscriber->pushed;
  _subscriber->delivered_cv.wait(
      lock, [&] { return _subscriber->stop || _subscriber->delivered >= target; });
}

void Engine::Notify(uint64_t seq, std::span<const Change> changes) {
  for (const auto& subscriber : _subscribers) subscriber->Deliver(seq, changes);
}

absl::Status Engine::SyncWal() {
  return _wal == nullptr ? absl::OkStatus() : _wal->Sync();
}

absl::Status Engine::ExportCheckpoint(
    const EngineSnapshot& snapshot, const LogPosition& position,
    const std::function<absl::Status(std::string_view)>& sink) const {
  internal::CheckpointWriter writer(sink);
  RETURN_IF_ERROR(writer.Begin(position.history, position.lsn));
  for (size_t c = 0; c < _collections.size(); ++c) {
    if (_schema.collections[c].ephemeral) continue;
    RETURN_IF_ERROR(writer.BeginCollection(_collections[c]->id, _schema.collections[c].name,
                                           _collections[c]->table.live()));
    for (ScanCursor cursor(snapshot, c); cursor.Valid(); cursor.Next()) {
      RETURN_IF_ERROR(writer.Add(cursor.key(), AsStringView(cursor.value())));
    }
  }
  return writer.Finish();
}

absl::Status Engine::Checkpoint() {
  if (_wal == nullptr) return absl::FailedPreconditionError("The engine is in memory only");
  std::lock_guard serial(_checkpoint_mutex);

  std::optional<EngineSnapshot> snapshot;
  LogPosition position;
  uint64_t log_bytes;
  {
    // The snapshot's sequence number and the LSN must describe the same commit.
    auto writer = AcquireWriterFirst();
    position = {_history, _lsn.load(std::memory_order_relaxed)};
    if (position.lsn == _checkpoint_lsn.load(std::memory_order_relaxed)) return absl::OkStatus();
    RETURN_IF_ERROR(_wal->Rotate());
    snapshot.emplace(*this);
    log_bytes = _wal_bytes_since_checkpoint.exchange(0, std::memory_order_relaxed);
  }
  const auto failed = [&](absl::Status status) {
    _wal_bytes_since_checkpoint.fetch_add(log_bytes, std::memory_order_relaxed);
    return status;
  };

  const std::string& path = _options.path;
  const std::string final_path =
      NumberedPath(path, kCheckpointPrefix, position.lsn, kCheckpointSuffix);
  const std::string temp_path = absl::StrCat(final_path, kTempSuffix);
  auto file = internal::AppendFile::Create(temp_path);
  if (!file.ok()) return failed(file.status());
  absl::Status status = ExportCheckpoint(
      *snapshot, position, [&file](std::string_view frames) { return (*file)->Append(frames); });
  snapshot.reset();  // Lets go of the old versions before the slow sync.
  if (status.ok()) status = (*file)->Sync();
  if (status.ok() && std::rename(temp_path.c_str(), final_path.c_str()) != 0) {
    status = internal::ErrnoStatus("rename", final_path);
  }
  if (status.ok()) status = internal::SyncDirectory(path);
  if (!status.ok()) {
    std::error_code ignored;
    std::filesystem::remove(temp_path, ignored);
    return failed(std::move(status));
  }

  _checkpoint_bytes.store((*file)->size(), std::memory_order_relaxed);
  _checkpoint_lsn.store(position.lsn, std::memory_order_relaxed);
  RemoveCheckpointsBelow(position.lsn);
  return PurgeWal();
}

void Engine::RemoveCheckpointsBelow(uint64_t lsn) const {
  auto checkpoints = internal::ListNumbered(_options.path, kCheckpointPrefix, kCheckpointSuffix);
  if (!checkpoints.ok()) return;  // Leftovers are harmless: the newest one is used.
  for (uint64_t old : *checkpoints) {
    if (old == lsn) continue;
    std::error_code ignored;
    std::filesystem::remove(NumberedPath(_options.path, kCheckpointPrefix, old, kCheckpointSuffix),
                            ignored);
  }
}

absl::Status Engine::PurgeWal() {
  uint64_t keep = _checkpoint_lsn.load(std::memory_order_relaxed);
  if (_wal->total_bytes() <= _options.wal_replica_limit_bytes) {
    std::lock_guard lock(_wal_pins_mutex);
    for (const auto& [pin, lsn] : _wal_pins) keep = std::min(keep, lsn);
  }
  return _wal->Purge(keep, _options.wal_retain_bytes);
}

void Engine::AfterCommit(uint64_t wal_bytes) {
  if (_wal == nullptr || _options.checkpoint_wal_bytes == 0) return;
  const uint64_t total =
      _wal_bytes_since_checkpoint.fetch_add(wal_bytes, std::memory_order_relaxed) + wal_bytes;
  const uint64_t threshold =
      std::max(_options.checkpoint_wal_bytes, _checkpoint_bytes.load(std::memory_order_relaxed));
  if (total < threshold || _checkpoint_requested.exchange(true)) return;
  { std::lock_guard lock(_background_mutex); }
  _background_cv.notify_all();
}

void Engine::RecordBackgroundError(absl::Status status) {
  if (status.ok()) return;
  std::fprintf(stderr, "gendb: background work failed: %s\n", status.ToString().c_str());
  std::lock_guard lock(_background_mutex);
  _background_error = std::move(status);
}

void Engine::BackgroundLoop() {
  const auto interval = _options.background_sync_interval;
  std::unique_lock lock(_background_mutex);
  while (!_background_stop) {
    if (interval.count() > 0) {
      _background_cv.wait_for(lock, interval);
    } else {
      _background_cv.wait(lock, [this] {
        return _background_stop || _checkpoint_requested.load(std::memory_order_acquire);
      });
    }
    if (_background_stop) break;
    lock.unlock();
    if (interval.count() > 0) RecordBackgroundError(_wal->Sync());
    if (_checkpoint_requested.exchange(false)) RecordBackgroundError(Checkpoint());
    lock.lock();
  }
}

absl::Status Engine::PromoteToPrimary() {
  auto writer = AcquireWriterFirst();
  _replica.store(false, std::memory_order_release);
  _history = HistoryId::Random();
  _history_generation.fetch_add(1, std::memory_order_acq_rel);
  _ring->Wake();
  if (_wal == nullptr) return absl::OkStatus();
  return _wal->StartSegment(_history, _lsn.load(std::memory_order_relaxed) + 1);
}

EngineStats Engine::Stats() const {
  size_t active = 0;
  {
    std::lock_guard lock(_snapshots_mutex);
    for (const auto& [seq, count] : _snapshots) active += count;
  }
  const uint64_t lsn = _lsn.load(std::memory_order_acquire);
  EngineStats stats{};
  stats.committed_seq = _committed_seq.load(std::memory_order_acquire);
  stats.lsn = lsn;
  stats.synced_lsn = _wal == nullptr ? lsn : _wal->synced_lsn();
  stats.checkpoint_lsn = _checkpoint_lsn.load(std::memory_order_relaxed);
  stats.checkpoint_bytes = _checkpoint_bytes.load(std::memory_order_relaxed);
  stats.wal_bytes = _wal == nullptr ? 0 : _wal->total_bytes();
  stats.active_snapshots = active;
  stats.pending_garbage = _garbage_size.load(std::memory_order_relaxed);
  {
    std::lock_guard lock(_background_mutex);
    stats.background_error = _background_error;
  }
  return stats;
}

std::optional<size_t> Engine::FindCollection(uint32_t id) const noexcept {
  const auto it = std::lower_bound(_collection_ids.begin(), _collection_ids.end(),
                                   std::pair<uint32_t, size_t>(id, 0));
  if (it == _collection_ids.end() || it->first != id) return std::nullopt;
  return it->second;
}

absl::Status Engine::ApplyCommitFrame(Transaction& txn, const Frame& frame) {
  CommitDecoder commit(frame.body);
  for (uint32_t i = 0; i < commit.count; ++i) {
    const internal::CommitEntry entry = commit.Next();
    if (!commit.ok()) return absl::DataLossError("Malformed commit");
    const std::optional<size_t> collection = FindCollection(entry.collection_id);
    if (!collection.has_value()) {
      if (_options.discard_unknown_collections) continue;
      txn.Rollback();
      return absl::FailedPreconditionError(
          absl::StrCat("The database holds collection '",
                       _wal != nullptr ? _wal->NameOf(entry.collection_id)
                                       : absl::StrCat("#", entry.collection_id),
                       "' that the schema does not have. Restore it, or set "
                       "discard_unknown_collections to drop it."));
    }
    if (_schema.collections[*collection].ephemeral) continue;
    if (entry.deleted) {
      if (!txn.Delete(*collection, AsBytes(entry.key)).ok()) {
        txn.Rollback();
        return absl::DataLossError("The log deletes a record that does not exist");
      }
    } else {
      txn.Put(*collection, AsBytes(entry.key), AsBytes(entry.value));
    }
  }
  txn.CommitReplayed(commit.lsn, frame.raw);
  return absl::OkStatus();
}

void Engine::EnableCommitRing(size_t capacity_bytes) {
  auto writer = AcquireWriterFirst();
  _ring->Enable(capacity_bytes, _lsn.load(std::memory_order_relaxed));
}

Engine::LogPosition Engine::Position() {
  auto writer = AcquireWriterFirst();
  return {_history, _lsn.load(std::memory_order_relaxed)};
}

bool Engine::CanReadLogAfter(uint64_t lsn) const {
  if (lsn == _lsn.load(std::memory_order_acquire) || _ring->Contains(lsn + 1)) return true;
  return _wal != nullptr && _wal->SegmentFor(lsn + 1).has_value();
}

uint64_t Engine::PinWal(uint64_t lsn) {
  std::lock_guard lock(_wal_pins_mutex);
  _wal_pins.emplace_back(_next_wal_pin, lsn);
  return _next_wal_pin++;
}

void Engine::UpdateWalPin(uint64_t pin, uint64_t lsn) {
  std::lock_guard lock(_wal_pins_mutex);
  for (auto& [id, kept] : _wal_pins) {
    if (id == pin) kept = lsn;
  }
}

void Engine::UnpinWal(uint64_t pin) {
  std::lock_guard lock(_wal_pins_mutex);
  std::erase_if(_wal_pins, [pin](const auto& entry) { return entry.first == pin; });
}

absl::StatusOr<bool> Engine::ReadLog(uint64_t* position, size_t max_bytes, std::string* out,
                                     std::unique_ptr<WalCursor>* cursor) {
  const uint64_t latest = _ring->latest();
  if (latest <= *position) return false;
  uint64_t limit = latest;
  if (_wal != nullptr) {
    // A commit leaves the primary only once it is durable here, so that a replica can never be
    // ahead of what this engine recovers after a power loss.
    RETURN_IF_ERROR(_wal->Sync());
    limit = std::min(latest, _wal->synced_lsn());
    if (limit <= *position) return false;
  }
  if (_ring->Read(position, limit, max_bytes, out)) return true;
  if (_wal != nullptr) {
    if (*cursor == nullptr) *cursor = std::make_unique<WalCursor>(*_wal);
    auto read = (*cursor)->Read(position, limit, max_bytes, out);
    if (!read.ok() || *read) return read;
  }
  return absl::OutOfRangeError(absl::StrCat("Commit ", *position + 1, " is no longer available"));
}

void Engine::WaitForLog(uint64_t after, std::chrono::milliseconds timeout) const {
  _ring->Wait(after, timeout);
}

void Engine::WakeLogWaiters() const {
  _ring->Wake();
}

absl::StatusOr<std::unique_ptr<CheckpointStage>> Engine::NewCheckpointStage() const {
  if (_wal == nullptr) return CheckpointStage::Create("");
  return CheckpointStage::Create(absl::StrCat(_options.path, "/incoming", kTempSuffix));
}

absl::Status Engine::InstallCheckpoint(CheckpointStage& stage, const LogPosition& position) {
  std::lock_guard serial(_checkpoint_mutex);
  auto writer = AcquireWriterFirst();

  std::string final_path;
  if (_wal != nullptr) {
    // A crash at any point leaves a state from which the replica recovers and asks again.
    final_path = NumberedPath(_options.path, kCheckpointPrefix, position.lsn, kCheckpointSuffix);
    RETURN_IF_ERROR(_wal->RemoveAll());
    RETURN_IF_ERROR(stage.Publish(final_path));
    RemoveCheckpointsBelow(position.lsn);
    RETURN_IF_ERROR(_wal->StartSegment(position.history, position.lsn + 1));
    std::error_code error;
    _checkpoint_bytes.store(std::filesystem::file_size(final_path, error),
                            std::memory_order_relaxed);
    _checkpoint_lsn.store(position.lsn, std::memory_order_relaxed);
    _wal_bytes_since_checkpoint.store(0, std::memory_order_relaxed);
  }
  auto source = stage.OpenSource(final_path);
  if (!source.ok()) return source.status();

  const uint64_t seq = _committed_seq.load(std::memory_order_relaxed) + 1;
  std::vector<Change> changes;
  CheckpointLoader loader(*this, seq, _subscribers.empty() ? nullptr : &changes);
  // Every frame was verified on arrival and the schema checked up front, so this can fail only if
  // the disk fails right now, with the old state already gone: restart and recover from the file.
  if (absl::Status status = internal::ReadCheckpoint(**source, loader); !status.ok()) {
    std::fprintf(stderr, "gendb: cannot install a checkpoint: %s\n", status.ToString().c_str());
    std::abort();
  }
  loader.RemoveMissing();

  _history = position.history;
  _history_generation.fetch_add(1, std::memory_order_acq_rel);
  _ring->Reset(position.lsn);
  _lsn.store(position.lsn, std::memory_order_release);
  _committed_seq.store(seq, std::memory_order_release);
  Notify(seq, changes);
  ReclaimUnpinned(seq);
  return absl::OkStatus();
}

absl::Status Engine::ApplyReplicated(std::string_view frames) {
  Transaction txn(*this);
  // Check everything first: what reaches the log must apply.
  uint64_t next = _lsn.load(std::memory_order_relaxed) + 1;
  std::string_view rest = frames;
  while (!rest.empty()) {
    Frame frame;
    if (internal::ParseFrame(rest, &frame) != ParseStatus::kOk ||
        frame.type != FrameType::kCommit) {
      return absl::DataLossError("Damaged commit from the primary");
    }
    CommitDecoder commit(frame.body);
    if (!commit.ok() || commit.lsn != next) {
      return absl::DataLossError(absl::StrCat("Expected commit ", next, " from the primary"));
    }
    ++next;
    rest.remove_prefix(frame.raw.size());
  }
  if (_wal != nullptr) {
    RETURN_IF_ERROR(_wal->Append(frames, next - 1, _options.sync_commits));
  }
  rest = frames;
  while (!rest.empty()) {
    Frame frame;
    (void)internal::ParseFrame(rest, &frame);
    RETURN_IF_ERROR(ApplyCommitFrame(txn, frame));
    rest.remove_prefix(frame.raw.size());
  }
  AfterCommit(frames.size());
  return absl::OkStatus();
}

uint64_t Engine::PinSnapshot() const {
  std::lock_guard lock(_snapshots_mutex);
  // Inside the lock, so that a concurrent Reclaim sees this pin or a seq at least as new as it.
  const uint64_t seq = _committed_seq.load(std::memory_order_acquire);
  ++_snapshots[seq];
  return seq;
}

void Engine::UnpinSnapshot(uint64_t seq) const noexcept {
  std::lock_guard lock(_snapshots_mutex);
  auto it = _snapshots.find(seq);
  if (--it->second == 0) _snapshots.erase(it);
}

std::optional<BytesConstView> Engine::ReadAt(size_t collection, uint64_t hash, std::string_view key,
                                             uint64_t seq) const noexcept {
  const RecordNode* node = _collections[collection]->table.Find(hash, key);
  if (node == nullptr) return std::nullopt;
  const Version* v = node->head.load(std::memory_order_acquire);
  // A version newer than this snapshot cannot have been reclaimed past: Reclaim only cuts a
  // chain behind a version that every snapshot can see.
  while (v != nullptr && v->seq > seq) v = v->older;
  if (v == nullptr || v->tombstone) return std::nullopt;
  return v->bytes();
}

// The record must not be in the table.
RecordNode* Engine::NodeForWrite(size_t collection, uint64_t hash, std::string_view key,
                                 uint64_t seq) {
  RecordTable& table = _collections[collection]->table;
  const RecordTable::Slots* replaced = nullptr;
  RecordNode* node = table.Insert(hash, key, &replaced);
  if (replaced != nullptr) {
    _garbage.push_back({seq, Garbage::kSlots, const_cast<RecordTable::Slots*>(replaced), 0});
  }
  return node;
}

// Frees what no snapshot at `up_to_seq` or newer can reach. Removing something from a structure
// is a separate first step: a reader already inside it stays pinned below the next seq, so the
// memory itself is queued for one commit later.
void Engine::Reclaim(uint64_t up_to_seq) noexcept {
  const uint64_t next_seq = up_to_seq == internal::kNeverDeleted
                                ? internal::kNeverDeleted
                                : _committed_seq.load(std::memory_order_relaxed) + 1;
  while (!_garbage.empty() && _garbage.front().seq <= up_to_seq) {
    const Garbage g = _garbage.front();
    _garbage.pop_front();
    switch (g.kind) {
      case Garbage::kOlderVersion: {
        auto* newer = static_cast<Version*>(g.ptr);
        Version::Destroy(newer->older);
        newer->older = nullptr;
        break;
      }
      case Garbage::kTombstone: {
        auto* node = static_cast<RecordNode*>(g.ptr);
        const Version* head = node->head.load(std::memory_order_relaxed);
        // Otherwise the key was written again, and a newer version now owns the tombstone.
        if (head->seq == g.seq) {
          _collections[g.owner]->table.Erase(node);
          _garbage.push_back({next_seq, Garbage::kRecord, node, 0});
        }
        break;
      }
      case Garbage::kRecord: {
        auto* node = static_cast<RecordNode*>(g.ptr);
        Version::Destroy(node->head.load(std::memory_order_relaxed));
        RecordTable::DestroyNode(node);
        break;
      }
      case Garbage::kSlots:
        RecordTable::DestroySlots(static_cast<RecordTable::Slots*>(g.ptr));
        break;
      case Garbage::kIndexEntryToUnlink: {
        auto* entry = static_cast<IndexEntry*>(g.ptr);
        _indices[g.owner]->Unlink(entry);
        _garbage.push_back({next_seq, Garbage::kIndexEntry, entry, 0});
        break;
      }
      case Garbage::kIndexEntry:
        OrderedIndex::Destroy(static_cast<IndexEntry*>(g.ptr));
        break;
    }
  }
  _garbage_size.store(_garbage.size(), std::memory_order_relaxed);
}

void Engine::ReclaimUnpinned(uint64_t seq) noexcept {
  uint64_t oldest = seq;
  {
    std::lock_guard lock(_snapshots_mutex);
    if (!_snapshots.empty()) oldest = _snapshots.begin()->first;
  }
  Reclaim(oldest);
}

void Engine::UpdateIndexNow(size_t collection, std::string_view key,
                            std::optional<BytesConstView> before,
                            std::optional<BytesConstView> after, uint64_t seq) {
  std::string& old_key = _old_index_key;
  std::string& new_key = _new_index_key;
  for (size_t i : _collections[collection]->indices) {
    const auto extract = _schema.indices[i].extract;
    old_key.clear();
    new_key.clear();
    const bool had = before.has_value() && extract(*before, old_key);
    const bool has = after.has_value() && extract(*after, new_key);
    if (had == has && old_key == new_key) continue;
    OrderedIndex& index = *_indices[i];
    if (had) {
      old_key.append(key);
      if (IndexEntry* entry = index.FindLive(old_key)) {
        entry->deleted.store(seq, std::memory_order_relaxed);
        _garbage.push_back({seq, Garbage::kIndexEntryToUnlink, entry, i});
      }
    }
    if (has) {
      const auto secondary_size = static_cast<uint32_t>(new_key.size());
      new_key.append(key);
      index.Insert(new_key, secondary_size, seq);
    }
  }
}

std::optional<BytesConstView> EngineSnapshot::Get(size_t collection,
                                                  BytesConstView key) const noexcept {
  const std::string_view key_view = AsStringView(key);
  return _engine->ReadAt(collection, HashKey(key_view), key_view, _seq);
}

ScanCursor::ScanCursor(const EngineSnapshot& snapshot, size_t collection)
    : _seq(snapshot._seq), _slots(snapshot._engine->_collections[collection]->table.LoadSlots()) {
  Next();
}

void ScanCursor::Next() noexcept {
  _value.reset();
  while (_next <= _slots->mask) {
    const RecordNode* node = _slots->slots[_next++].load(std::memory_order_acquire);
    if (node == nullptr || node == RecordTable::Erased()) continue;
    const Version* v = node->head.load(std::memory_order_acquire);
    while (v != nullptr && v->seq > _seq) v = v->older;
    if (v != nullptr && !v->tombstone) {
      _key = node->key();
      _value = v->bytes();
      return;
    }
  }
}

IndexCursor::IndexCursor(const EngineSnapshot& snapshot, size_t index, std::string_view lo,
                         std::string hi)
    : IndexCursor(*snapshot._engine, index, snapshot._seq, nullptr, lo, std::move(hi)) {}

IndexCursor::IndexCursor(const Transaction& txn, size_t index, std::string_view lo, std::string hi)
    : IndexCursor(*txn._engine, index, txn._engine->_committed_seq.load(std::memory_order_relaxed),
                  &txn, lo, std::move(hi)) {}

IndexCursor::IndexCursor(const Engine& engine, size_t index, uint64_t seq, const Transaction* txn,
                         std::string_view lo, std::string hi)
    : _engine(&engine),
      _collection(engine._schema.indices[index].collection),
      _seq(seq),
      _txn(txn),
      _hi(std::move(hi)) {
  if (txn != nullptr) {
    const auto& deltas = txn->_index_deltas[index];
    for (auto it = deltas.lower_bound(lo); it != deltas.end() && (_hi.empty() || it->first < _hi);
         ++it) {
      _deltas.push_back({it->first, it->second.added, it->second.secondary_size});
    }
  }
  _entry = NextVisible(engine._indices[index]->LowerBound(lo));
  Next();
}

const IndexEntry* IndexCursor::NextVisible(const IndexEntry* e) const noexcept {
  while (e != nullptr && (_hi.empty() || e->key() < _hi)) {
    if (e->VisibleAt(_seq)) return e;
    e = OrderedIndex::Next(e);
  }
  return nullptr;
}

std::optional<BytesConstView> IndexCursor::Lookup(std::string_view primary_key) const noexcept {
  if (_txn != nullptr) return _txn->Get(_collection, AsBytes(primary_key));
  return _engine->ReadAt(_collection, HashKey(primary_key), primary_key, _seq);
}

void IndexCursor::Next() {
  _value.reset();
  while (!_value.has_value()) {
    const Delta* delta = _next_delta < _deltas.size() ? &_deltas[_next_delta] : nullptr;
    if (_entry == nullptr && delta == nullptr) return;
    if (delta != nullptr && (_entry == nullptr || delta->key <= _entry->key())) {
      // A delta for a committed entry replaces it.
      if (_entry != nullptr && delta->key == _entry->key()) {
        _entry = NextVisible(OrderedIndex::Next(_entry));
      }
      if (delta->added) _value = Lookup(std::string_view(delta->key).substr(delta->secondary_size));
      ++_next_delta;
    } else {
      _value = Lookup(_entry->primary_key());
      _entry = NextVisible(OrderedIndex::Next(_entry));
    }
  }
}

Transaction::Transaction(Engine& engine)
    : _engine(&engine),
      _lock(engine.AcquireWriter()),
      _pending(engine._collections.size()),
      _index_deltas(engine._indices.size()) {}

Transaction::~Transaction() {
  Rollback();
}

Transaction::Transaction(Transaction&&) noexcept = default;

void Transaction::Rollback() noexcept {
  for (const PendingWrite& write : _writes) Version::Destroy(write.version);
  _writes.clear();
  for (auto& pending : _pending) pending.clear();
  for (auto& delta : _index_deltas) delta.clear();
}

Transaction::PendingWrite* Transaction::FindPending(size_t collection,
                                                    std::string_view key) const noexcept {
  const auto& pending = _pending[collection];
  auto it = pending.find(key);
  return it == pending.end() ? nullptr : it->second;
}

std::optional<BytesConstView> Transaction::Get(size_t collection,
                                               BytesConstView key) const noexcept {
  const std::string_view key_view = AsStringView(key);
  if (const PendingWrite* write = FindPending(collection, key_view)) {
    if (write->version->tombstone) return std::nullopt;
    return write->version->bytes();
  }
  return _engine->ReadAt(collection, HashKey(key_view), key_view,
                         _engine->_committed_seq.load(std::memory_order_relaxed));
}

void Transaction::SetPending(size_t collection, uint64_t hash, std::string_view key,
                             Version* value) {
  if (PendingWrite* write = FindPending(collection, key)) {
    Version::Destroy(write->version);
    write->version = value;
    return;
  }
  PendingWrite& write =
      _writes.emplace_back(PendingWrite{collection, hash, std::string(key), value, nullptr});
  _pending[collection].emplace(write.key, &write);
}

void Transaction::UpdateIndices(size_t collection, std::string_view key,
                                std::optional<BytesConstView> before,
                                std::optional<BytesConstView> after) {
  for (size_t i : _engine->_collections[collection]->indices) {
    const auto extract = _engine->_schema.indices[i].extract;
    _old_key.clear();
    _new_key.clear();
    const bool had = before.has_value() && extract(*before, _old_key);
    const bool has = after.has_value() && extract(*after, _new_key);
    if (had == has && _old_key == _new_key) continue;
    auto& delta = _index_deltas[i];
    // A change and its opposite within one transaction cancel out.
    const auto record = [&delta, key](std::string& secondary, bool added) {
      const auto secondary_size = static_cast<uint32_t>(secondary.size());
      secondary.append(key);
      auto [it, inserted] = delta.try_emplace(secondary, IndexDelta{added, secondary_size});
      if (!inserted && it->second.added != added) delta.erase(it);
    };
    if (had) record(_old_key, false);
    if (has) record(_new_key, true);
  }
}

void Transaction::Put(size_t collection, BytesConstView key, BytesConstView value) {
  const std::string_view key_view = AsStringView(key);
  if (!_engine->_collections[collection]->indices.empty()) {
    UpdateIndices(collection, key_view, Get(collection, key), value);
  }
  SetPending(collection, HashKey(key_view), key_view, Version::Copy(value));
}

absl::Status Transaction::Update(size_t collection, BytesConstView key, const MessagePatch& patch) {
  const std::string_view key_view = AsStringView(key);
  const std::optional<BytesConstView> current = Get(collection, key);
  if (!current.has_value()) return absl::NotFoundError("Key not found");
  auto view = MessageView::Open(*current);
  if (!view.has_value()) return absl::DataLossError("Malformed stored message");

  Version* next;
  if (CanApplyPatchInplace(patch, *view)) {
    next = Version::Copy(*current);
    ApplyPatchInplace(patch, std::span<uint8_t>(next->data(), next->size));
  } else {
    next = Version::Copy(Rebuild(*view, patch));
  }
  if (!_engine->_collections[collection]->indices.empty()) {
    UpdateIndices(collection, key_view, current, next->bytes());
  }
  SetPending(collection, HashKey(key_view), key_view, next);
  return absl::OkStatus();
}

absl::Status Transaction::Delete(size_t collection, BytesConstView key) {
  const std::string_view key_view = AsStringView(key);
  const std::optional<BytesConstView> current = Get(collection, key);
  if (!current.has_value()) return absl::NotFoundError("Key not found");
  if (!_engine->_collections[collection]->indices.empty()) {
    UpdateIndices(collection, key_view, current, std::nullopt);
  }
  SetPending(collection, HashKey(key_view), key_view, Version::Create(0, true));
  return absl::OkStatus();
}

std::pair<bool, bool> Transaction::ResolveWrites() noexcept {
  bool any = false;
  bool logged = false;
  for (PendingWrite& write : _writes) {
    write.node = _engine->_collections[write.collection]->table.Find(write.hash, write.key);
    if (write.version->tombstone) {
      const Version* head =
          write.node == nullptr ? nullptr : write.node->head.load(std::memory_order_relaxed);
      if (head == nullptr || head->tombstone) {
        // The key never existed outside this transaction.
        Version::Destroy(write.version);
        write.version = nullptr;
        continue;
      }
    }
    any = true;
    logged = logged || !_engine->_schema.collections[write.collection].ephemeral;
  }
  return {any, logged};
}

absl::Status Transaction::Commit(CommitOptions options) {
  Engine& engine = *_engine;
  const auto [any, logged] = ResolveWrites();
  if (!any) {
    Rollback();
    return absl::OkStatus();
  }

  uint64_t lsn = engine._lsn.load(std::memory_order_relaxed);
  std::string_view frame;
  if (logged) {
    if (engine._replica.load(std::memory_order_acquire)) {
      Rollback();
      return absl::FailedPreconditionError(
          "A replica takes local commits only to ephemeral collections");
    }
    ++lsn;
    // Durable first: nothing is visible until the commit is in the log.
    if (engine._wal != nullptr || engine._ring->enabled()) {
      std::string& buffer = engine._commit_buffer;
      buffer.clear();
      const size_t begin = internal::BeginCommit(buffer, lsn);
      uint32_t count = 0;
      for (const PendingWrite& write : _writes) {
        if (write.version == nullptr || engine._schema.collections[write.collection].ephemeral) {
          continue;
        }
        internal::AddCommitEntry(buffer, engine._collections[write.collection]->id,
                                 write.version->tombstone, write.key,
                                 AsStringView(write.version->bytes()));
        ++count;
      }
      internal::EndCommit(buffer, begin, count);
      frame = buffer;
    }
    if (engine._wal != nullptr) {
      absl::Status status =
          engine._wal->Append(frame, lsn, options.sync.value_or(engine._options.sync_commits));
      if (!status.ok()) {
        Rollback();
        return status;
      }
    }
  }
  Publish(logged, lsn, frame);
  engine.AfterCommit(frame.size());
  return absl::OkStatus();
}

void Transaction::CommitReplayed(uint64_t lsn, std::string_view frame) {
  ResolveWrites();
  Publish(/*logged=*/true, lsn, frame);
}

void Transaction::Publish(bool logged, uint64_t lsn, std::string_view frame) {
  Engine& engine = *_engine;
  const uint64_t seq = engine._committed_seq.load(std::memory_order_relaxed) + 1;
  std::vector<Change> changes;
  const bool notify = !engine._subscribers.empty();
  if (notify) changes.reserve(_writes.size());

  // Readers may run throughout, but ignore seq until it is published below.
  for (PendingWrite& write : _writes) {
    Version* version = write.version;
    if (version == nullptr) continue;
    RecordNode* node = write.node;
    if (node == nullptr) node = engine.NodeForWrite(write.collection, write.hash, write.key, seq);
    version->seq = seq;
    Version* older = node->head.load(std::memory_order_relaxed);
    version->older = older;
    node->head.store(version, std::memory_order_release);
    if (older != nullptr) {
      engine._garbage.push_back({seq, Engine::Garbage::kOlderVersion, version, 0});
    }
    if (version->tombstone) {
      engine._garbage.push_back({seq, Engine::Garbage::kTombstone, node, write.collection});
    }
    if (notify) {
      // Both stay alive until the reclamation below.
      changes.push_back(
          {write.collection, AsBytes(node->key()),
           older != nullptr && !older->tombstone ? std::optional<BytesConstView>(older->bytes())
                                                 : std::nullopt,
           version->tombstone ? std::nullopt : std::optional<BytesConstView>(version->bytes())});
    }
  }
  for (size_t i = 0; i < _index_deltas.size(); ++i) {
    OrderedIndex& index = *engine._indices[i];
    for (const auto& [key, delta] : _index_deltas[i]) {
      if (delta.added) {
        index.Insert(key, delta.secondary_size, seq);
      } else if (IndexEntry* entry = index.FindLive(key)) {
        entry->deleted.store(seq, std::memory_order_relaxed);
        engine._garbage.push_back({seq, Engine::Garbage::kIndexEntryToUnlink, entry, i});
      }
    }
    _index_deltas[i].clear();
  }
  if (logged) {
    if (engine._ring->enabled()) engine._ring->Push(lsn, frame);
    engine._lsn.store(lsn, std::memory_order_release);
  }
  engine._committed_seq.store(seq, std::memory_order_release);

  if (notify && !changes.empty()) engine.Notify(seq, changes);

  _writes.clear();
  for (auto& pending : _pending) pending.clear();
  engine.ReclaimUnpinned(seq);
}

}  // namespace gendb
