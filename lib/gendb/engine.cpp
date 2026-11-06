#include "gendb/engine.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include "absl/strings/str_cat.h"
#include "gendb/commit_ring.h"
#include "gendb/engine_internal.h"
#include "gendb/status.h"
#include "gendb/wal.h"

namespace gendb {

using internal::AsStringView;
using internal::HashKey;
using internal::HistoryId;
using internal::IndexEntry;
using internal::OrderedIndex;
using internal::RecordNode;
using internal::RecordTable;
using internal::Version;

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

Engine::Engine(EngineSchema schema, std::shared_ptr<EngineObserver> observer)
    : Engine(std::move(schema), EngineOptions{.observer = std::move(observer)}) {}

void Engine::Log(LogLevel level, std::string_view message) const {
  if (_options.observer != nullptr) _options.observer->OnLog(level, message);
}

Engine::Engine(EngineSchema schema, EngineOptions options)
    : _schema(std::move(schema)),
      _options(std::move(options)),
      _replica(_options.replica),
      _file_system(_options.file_system != nullptr ? _options.file_system : DefaultFileSystem()),
      _ring(std::make_unique<CommitRing>()) {
  const size_t schema_size = _schema.collections.size();
  _collections.resize(schema_size + kMaxForeignCollections);
  std::vector<std::pair<uint32_t, size_t>> ids;
  for (size_t i = 0; i < schema_size; ++i) {
    const CollectionDef& def = _schema.collections[i];
    _collections[i] = std::make_unique<Collection>();
    _collections[i]->id = internal::CollectionIdOf(def.name);
    _collections[i]->name = def.name;
    _collections[i]->ephemeral = def.ephemeral;
    ids.emplace_back(_collections[i]->id, i);
    if (!def.ephemeral) _durable_ids.emplace_back(_collections[i]->id, i);
  }
  _collection_count.store(schema_size, std::memory_order_release);
  std::sort(_durable_ids.begin(), _durable_ids.end());
  std::sort(ids.begin(), ids.end());
  for (size_t i = 1; i < ids.size(); ++i) {
    if (ids[i - 1].first == ids[i].first) {
      std::fprintf(stderr, "gendb: collections '%s' and '%s' have the same ID: rename one\n",
                   _schema.collections[ids[i - 1].second].name.c_str(),
                   _schema.collections[ids[i].second].name.c_str());
      std::abort();
    }
  }
  for (size_t i = 0; i < _schema.indices.size(); ++i) {
    _indices.push_back(std::make_unique<OrderedIndex>());
    _collections[_schema.indices[i].collection]->indices.push_back(i);
  }
  _history = HistoryId::Random();
}

Engine::~Engine() {
  {
    std::lock_guard lock(_background_mutex);
    _background_stop = true;
  }
  _background_cv.notify_all();
  if (_background.joinable()) _background.join();
  StopSubscribers();
  if (_wal != nullptr) {
    if (absl::Status status = _wal->Close(); !status.ok()) {
      Log(LogLevel::kError, absl::StrCat("Closing the log: ", status.ToString()));
    }
  }
  Reclaim(internal::kNeverDeleted);
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

absl::Status Engine::SyncWal() {
  return _wal == nullptr ? absl::OkStatus() : _wal->Sync();
}

EngineStats Engine::Stats() const {
  size_t active = 0;
  for (const SnapshotSlot& slot : _snapshot_slots) {
    if (slot.pinned.load(std::memory_order_relaxed) != 0) ++active;
  }
  active += _overflow_count.load(std::memory_order_relaxed);
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
  for (size_t c = _schema.collections.size(); c < collection_count(); ++c) {
    stats.foreign_collections.push_back(_collections[c]->name);
  }
  {
    std::lock_guard lock(_background_mutex);
    stats.background_error = _background_error;
  }
  return stats;
}

std::optional<size_t> Engine::FindDurableCollection(uint32_t id) const noexcept {
  const auto it = std::lower_bound(_durable_ids.begin(), _durable_ids.end(),
                                   std::pair<uint32_t, size_t>(id, 0));
  if (it == _durable_ids.end() || it->first != id) return std::nullopt;
  return it->second;
}

absl::StatusOr<size_t> Engine::AddForeignCollection(uint32_t id, std::string_view name) {
  const size_t index = collection_count();
  if (index == _collections.size()) {
    return absl::ResourceExhaustedError("Too many collections that the schema does not have");
  }
  auto collection = std::make_unique<Collection>();
  collection->id = id;
  collection->name = std::string(name);
  collection->ephemeral = false;
  _collections[index] = std::move(collection);
  _durable_ids.insert(std::lower_bound(_durable_ids.begin(), _durable_ids.end(),
                                       std::pair<uint32_t, size_t>(id, index)),
                      {id, index});
  _collection_count.store(index + 1, std::memory_order_release);
  // Sessions served from here told their replicas the old list: they must start over.
  _history_generation.fetch_add(1, std::memory_order_acq_rel);
  _ring->Wake();
  if (_wal != nullptr) RETURN_IF_ERROR(_wal->AddCollection(id, std::string(name)));
  return index;
}

uint64_t Engine::PinSnapshot(int32_t* slot) const {
  // Threads tend to keep to a slot of their own.
  static std::atomic<size_t> next_thread{0};
  thread_local const size_t preferred = next_thread.fetch_add(1, std::memory_order_relaxed);

  for (size_t i = 0; i < kSnapshotSlots; ++i) {
    const size_t index = (preferred + i) % kSnapshotSlots;
    std::atomic<uint64_t>& pinned = _snapshot_slots[index].pinned;
    if (pinned.load(std::memory_order_relaxed) != 0) continue;

    size_t used = _snapshot_slots_used.load(std::memory_order_seq_cst);
    while (used <= index && !_snapshot_slots_used.compare_exchange_weak(used, index + 1)) {
    }
    uint64_t seq = _committed_seq.load(std::memory_order_seq_cst);
    uint64_t expected = 0;
    if (!pinned.compare_exchange_strong(expected, seq + 1, std::memory_order_seq_cst)) continue;
    // See SnapshotSlot.
    for (uint64_t now = _committed_seq.load(std::memory_order_seq_cst); now != seq;
         now = _committed_seq.load(std::memory_order_seq_cst)) {
      seq = now;
      pinned.store(seq + 1, std::memory_order_seq_cst);
    }
    *slot = static_cast<int32_t>(index);
    return seq;
  }

  _overflow_count.fetch_add(1, std::memory_order_seq_cst);
  std::lock_guard lock(_overflow_mutex);
  // Inside the lock, so that a concurrent Reclaim sees this pin or a seq at least as new as it.
  const uint64_t seq = _committed_seq.load(std::memory_order_seq_cst);
  ++_overflow_snapshots[seq];
  *slot = -1;
  return seq;
}

void Engine::UnpinSnapshot(uint64_t seq, int32_t slot) const noexcept {
  if (slot >= 0) {
    _snapshot_slots[static_cast<size_t>(slot)].pinned.store(0, std::memory_order_release);
    return;
  }
  std::lock_guard lock(_overflow_mutex);
  auto it = _overflow_snapshots.find(seq);
  if (--it->second == 0) _overflow_snapshots.erase(it);
  _overflow_count.fetch_sub(1, std::memory_order_relaxed);
}

uint64_t Engine::OldestPinned(uint64_t newest) const noexcept {
  uint64_t oldest = newest;
  const size_t used = _snapshot_slots_used.load(std::memory_order_seq_cst);
  for (size_t i = 0; i < used; ++i) {
    const uint64_t pinned = _snapshot_slots[i].pinned.load(std::memory_order_seq_cst);
    if (pinned != 0 && pinned - 1 < oldest) oldest = pinned - 1;
  }
  if (_overflow_count.load(std::memory_order_seq_cst) != 0) {
    std::lock_guard lock(_overflow_mutex);
    if (!_overflow_snapshots.empty()) oldest = std::min(oldest, _overflow_snapshots.begin()->first);
  }
  return oldest;
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
  if (_garbage.empty()) return;
  Reclaim(OldestPinned(seq));
}

void Engine::UpdateIndexNow(size_t collection, std::string_view key,
                            std::optional<BytesConstView> before,
                            std::optional<BytesConstView> after, uint64_t seq) {
  if (_indices_deferred) return;
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

}  // namespace gendb
