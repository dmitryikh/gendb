#include "gendb/engine.h"

#include <utility>

#include "absl/hash/hash.h"
#include "gendb/rocksdb_backend.h"

namespace gendb {

using internal::AsStringView;
using internal::IndexEntry;
using internal::OrderedIndex;
using internal::RecordNode;
using internal::RecordTable;
using internal::Version;

namespace {

uint64_t HashKey(std::string_view key) noexcept {
  return absl::HashOf(key);
}
BytesConstView AsBytes(std::string_view s) noexcept {
  return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

}  // namespace

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

Engine::Engine(EngineSchema schema) : Engine(std::move(schema), nullptr, false, false) {}

Engine::Engine(EngineSchema schema, std::unique_ptr<RocksDbBackend> backend, bool sync,
               bool change_log)
    : _schema(std::move(schema)),
      _backend(std::move(backend)),
      _sync_commits(sync),
      _change_log(change_log) {
  for (size_t i = 0; i < _schema.collections.size(); ++i) {
    _collections.push_back(std::make_unique<Collection>());
  }
  for (size_t i = 0; i < _schema.indices.size(); ++i) {
    _indices.push_back(std::make_unique<OrderedIndex>());
    _collections[_schema.indices[i].collection]->indices.push_back(i);
  }
}

absl::StatusOr<std::unique_ptr<Engine>> Engine::Open(EngineSchema schema, EngineOptions options) {
  std::vector<std::string> names;
  for (const CollectionDef& collection : schema.collections) {
    names.push_back(collection.ephemeral ? std::string() : collection.name);
  }
  auto backend = RocksDbBackend::Open(options.path, std::move(names), options.change_log);
  if (!backend.ok()) return backend.status();
  std::unique_ptr<Engine> engine(
      new Engine(std::move(schema), std::move(*backend), options.sync_commits, options.change_log));
  if (absl::Status status = engine->Recover(); !status.ok()) return status;
  return engine;
}

absl::Status Engine::Recover() {
  const uint64_t seq = _backend->last_seq();
  std::string secondary;
  for (size_t c = 0; c < _collections.size(); ++c) {
    if (_schema.collections[c].ephemeral) continue;
    absl::Status status = _backend->Load(c, [&](BytesConstView key, BytesConstView value) {
      const std::string_view key_view = AsStringView(key);
      RecordNode* node = NodeForWrite(c, HashKey(key_view), key_view, seq);
      Version* version = Version::Copy(value);
      version->seq = seq;
      node->head.store(version, std::memory_order_relaxed);
      for (size_t i : _collections[c]->indices) {
        secondary.clear();
        if (_schema.indices[i].extract(value, secondary)) {
          const auto secondary_size = static_cast<uint32_t>(secondary.size());
          secondary.append(key_view);
          _indices[i]->Insert(secondary, secondary_size, seq);
        }
      }
    });
    if (!status.ok()) return status;
  }
  // Slot arrays replaced while loading are unreachable: nothing has read this engine yet.
  Reclaim(seq);
  _committed_seq.store(seq, std::memory_order_release);
  return absl::OkStatus();
}

Engine::~Engine() {
  Reclaim(internal::kNeverDeleted);
}

void Engine::AddChangeListener(ChangeListener listener) {
  std::lock_guard lock(_writer_mutex);
  _listeners.push_back(std::move(listener));
}

absl::Status Engine::ReadChanges(uint64_t after_seq,
                                 const std::function<bool(uint64_t, const Change&)>& fn) const {
  if (_backend == nullptr || !_change_log) {
    return absl::FailedPreconditionError("The change log is not enabled");
  }
  return _backend->ReadChanges(after_seq, [&fn](uint64_t seq, const RocksDbBackend::Write& write) {
    return fn(seq, Change{write.collection, write.deleted, write.key, write.value});
  });
}

absl::Status Engine::TruncateChanges(uint64_t up_to_seq) {
  if (_backend == nullptr || !_change_log) {
    return absl::FailedPreconditionError("The change log is not enabled");
  }
  return _backend->TruncateChanges(up_to_seq);
}

absl::Status Engine::SyncWal() {
  return _backend == nullptr ? absl::OkStatus() : _backend->SyncWal();
}

EngineStats Engine::Stats() const {
  size_t active = 0;
  {
    std::lock_guard lock(_snapshots_mutex);
    for (const auto& [seq, count] : _snapshots) active += count;
  }
  return {_committed_seq.load(std::memory_order_acquire), active,
          _garbage_size.load(std::memory_order_relaxed)};
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

RecordNode* Engine::NodeForWrite(size_t collection, uint64_t hash, std::string_view key,
                                 uint64_t seq) {
  RecordTable& table = _collections[collection]->table;
  if (RecordNode* node = table.Find(hash, key)) return node;
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
      _lock(engine._writer_mutex),
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
      _writes.emplace_back(PendingWrite{collection, hash, std::string(key), value});
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

absl::Status Transaction::Commit(CommitOptions options) {
  if (_writes.empty()) return absl::OkStatus();
  Engine& engine = *_engine;
  const uint64_t seq = engine._committed_seq.load(std::memory_order_relaxed) + 1;

  // Durable first: nothing is visible until the write is in the log.
  if (engine._backend != nullptr) {
    std::vector<RocksDbBackend::Write> durable;
    durable.reserve(_writes.size());
    for (const PendingWrite& write : _writes) {
      if (engine._schema.collections[write.collection].ephemeral) continue;
      durable.push_back(
          {write.collection, write.version->tombstone, AsBytes(write.key), write.version->bytes()});
    }
    if (!durable.empty()) {
      absl::Status status =
          engine._backend->Commit(seq, durable, options.sync.value_or(engine._sync_commits));
      if (!status.ok()) {
        Rollback();
        return status;
      }
    }
  }

  // Readers may run throughout, but ignore seq until it is published below.
  for (PendingWrite& write : _writes) {
    RecordTable& table = engine._collections[write.collection]->table;
    RecordNode* node = table.Find(write.hash, write.key);
    Version* version = write.version;
    if (version->tombstone) {
      Version* head = node == nullptr ? nullptr : node->head.load(std::memory_order_relaxed);
      if (head == nullptr || head->tombstone) {
        // The key never existed outside this transaction.
        Version::Destroy(version);
        write.version = nullptr;
        continue;
      }
    }
    if (node == nullptr) node = engine.NodeForWrite(write.collection, write.hash, write.key, seq);
    version->seq = seq;
    version->older = node->head.load(std::memory_order_relaxed);
    node->head.store(version, std::memory_order_release);
    if (version->older != nullptr) {
      engine._garbage.push_back({seq, Engine::Garbage::kOlderVersion, version, 0});
    }
    if (version->tombstone) {
      engine._garbage.push_back({seq, Engine::Garbage::kTombstone, node, write.collection});
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
  engine._committed_seq.store(seq, std::memory_order_release);

  if (!engine._listeners.empty()) {
    std::vector<Change> changes;
    changes.reserve(_writes.size());
    for (const PendingWrite& write : _writes) {
      if (write.version == nullptr) continue;
      changes.push_back(
          {write.collection, write.version->tombstone, AsBytes(write.key), write.version->bytes()});
    }
    const CommitEvent event{seq, changes};
    for (const ChangeListener& listener : engine._listeners) listener(event);
  }

  _writes.clear();
  for (auto& pending : _pending) pending.clear();

  uint64_t oldest = seq;
  {
    std::lock_guard lock(engine._snapshots_mutex);
    if (!engine._snapshots.empty()) oldest = engine._snapshots.begin()->first;
  }
  engine.Reclaim(oldest);
  return absl::OkStatus();
}

}  // namespace gendb
