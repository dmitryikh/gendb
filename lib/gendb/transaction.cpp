#include <algorithm>
#include <bit>
#include <chrono>
#include <cstring>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "gendb/commit_ring.h"
#include "gendb/engine.h"
#include "gendb/engine_internal.h"
#include "gendb/status.h"
#include "gendb/wal.h"

namespace gendb {

using internal::AsBytes;
using internal::AsStringView;
using internal::CrcOf;
using internal::HashKey;
using internal::IndexEntry;
using internal::OrderedIndex;
using internal::PatchedVersion;
using internal::RecordNode;
using internal::Version;

void Transaction::IndexDeltas::Record(std::string_view key, const IndexDelta& delta, bool unique) {
  // A change and its opposite within one transaction cancel out.
  bool cancelled = false;
  if (_ordered) {
    auto [it, inserted] = _tree.try_emplace(std::string(key), delta);
    if (!inserted && it->second.added != delta.added) {
      _tree.erase(it);
      cancelled = true;
    }
  } else {
    const auto [it, inserted] = _hash.try_emplace(key, delta);
    if (!inserted && it->second.added != delta.added) {
      _hash.erase(it);
      cancelled = true;
    }
  }
  if (!unique) return;
  const std::string_view secondary = key.substr(0, delta.secondary_size);
  const std::string_view primary = key.substr(delta.secondary_size);
  if (delta.added && !cancelled) {
    _added_by_secondary.insert_or_assign(std::string(secondary), std::string(primary));
  } else if (!delta.added && cancelled) {
    const auto it = _added_by_secondary.find(secondary);
    if (it != _added_by_secondary.end() && it->second == primary) _added_by_secondary.erase(it);
  }
}

const Transaction::IndexDelta* Transaction::IndexDeltas::Find(std::string_view key) const noexcept {
  if (_ordered) {
    const auto it = _tree.find(key);
    return it == _tree.end() ? nullptr : &it->second;
  }
  const auto it = _hash.find(key);
  return it == _hash.end() ? nullptr : &it->second;
}

const std::string* Transaction::IndexDeltas::AddedFor(std::string_view secondary) const noexcept {
  const auto it = _added_by_secondary.find(secondary);
  return it == _added_by_secondary.end() ? nullptr : &it->second;
}

void Transaction::IndexDeltas::clear() noexcept {
  _hash.clear();
  _tree.clear();
  _added_by_secondary.clear();
  _ordered = false;
}

const absl::btree_map<std::string, Transaction::IndexDelta>& Transaction::IndexDeltas::Ordered()
    const {
  if (!_ordered) {
    for (const auto& [key, delta] : _hash) _tree.emplace(key, delta);
    _hash.clear();
    _ordered = true;
  }
  return _tree;
}

namespace {
// Big-endian, so that it orders as the bytes do, which settles most comparisons of a sort without
// looking at the keys in memory.
uint64_t KeyPrefix(std::string_view key) noexcept {
  uint64_t prefix = 0;
  std::memcpy(&prefix, key.data(), std::min(key.size(), sizeof(prefix)));
  return std::endian::native == std::endian::little ? __builtin_bswap64(prefix) : prefix;
}
}  // namespace

template <typename Fn>
void Transaction::IndexDeltas::Drain(Fn&& fn) {
  if (_ordered) {
    for (const auto& [key, delta] : _tree) fn(std::string_view(key), delta);
  } else {
    struct Item {
      uint64_t prefix;
      std::string_view key;
      IndexDelta delta;
    };
    std::vector<Item> items;
    items.reserve(_hash.size());
    for (const auto& [key, delta] : _hash) items.push_back({KeyPrefix(key), key, delta});
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
      return a.prefix != b.prefix ? a.prefix < b.prefix : a.key < b.key;
    });
    for (const Item& item : items) fn(item.key, item.delta);
  }
  clear();
}

Transaction::Transaction(Engine& engine)
    : _engine(&engine),
      _lock(engine.AcquireWriter()),
      _pending(engine.collection_count()),
      _index_deltas(engine._indices.size()) {}

Transaction::~Transaction() {
  Rollback();
}

Transaction::Transaction(Transaction&&) noexcept = default;

void Transaction::Rollback() noexcept {
  for (const PendingWrite& write : _writes) Version::Destroy(_engine->_pool, write.version);
  _writes.clear();
  for (auto& pending : _pending) pending.clear();
  for (auto& delta : _index_deltas) delta.clear();
}

std::optional<BytesConstView> Transaction::Get(size_t collection,
                                               BytesConstView key) const noexcept {
  const std::string_view key_view = AsStringView(key);
  return GetHashed(collection, HashKey(key_view), key_view);
}

std::optional<BytesConstView> Transaction::GetHashed(size_t collection, uint64_t hash,
                                                     std::string_view key) const noexcept {
  if (const PendingWrite* write = FindPending(collection, hash, key)) {
    if (write->version->tombstone) return std::nullopt;
    return write->version->bytes();
  }
  return _engine->ReadAt(collection, hash, key,
                         _engine->_committed_seq.load(std::memory_order_relaxed));
}

void Transaction::SetPending(size_t collection, uint64_t hash, std::string_view key, Version* value,
                             std::string patch) {
  if (PendingWrite* write = FindPending(collection, hash, key)) {
    Version::Destroy(_engine->_pool, write->version);
    write->version = value;
    write->patch = std::move(patch);
    return;
  }
  PendingWrite& write = _writes.emplace_back(
      PendingWrite{collection, hash, std::string(key), value, nullptr, std::move(patch)});
  _pending[collection].Insert(&write);
}

namespace {
bool PatchReaches(const IndexDef& index, const MessagePatch& patch) noexcept {
  if (index.fields.empty()) return true;
  for (const FieldId field : index.fields) {
    if (patch.Modifies(field)) return true;
  }
  return false;
}
}  // namespace

void Transaction::UpdateIndices(size_t collection, uint64_t hash, std::string_view key,
                                std::optional<BytesConstView> before,
                                std::optional<BytesConstView> after, const MessagePatch* patch) {
  if (_engine->_indices_deferred) return;
  for (size_t i : _engine->_collections[collection]->indices) {
    if (patch != nullptr && !PatchReaches(_engine->_schema.indices[i], *patch)) continue;
    const auto extract = _engine->_schema.indices[i].extract;
    _old_key.clear();
    _new_key.clear();
    const bool had = before.has_value() && extract(*before, _old_key);
    const bool has = after.has_value() && extract(*after, _new_key);
    if (had == has && _old_key == _new_key) continue;
    IndexDeltas& deltas = _index_deltas[i];
    const bool unique = _engine->_schema.indices[i].unique;
    const auto record = [&deltas, hash, key, unique](std::string& secondary, bool added) {
      const auto secondary_size = static_cast<uint32_t>(secondary.size());
      secondary.append(key);
      deltas.Record(secondary, {added, secondary_size, hash}, unique);
    };
    if (had) record(_old_key, false);
    if (has) record(_new_key, true);
  }
}

absl::Status Transaction::CheckUnique(size_t collection, std::string_view key, BytesConstView value,
                                      const MessagePatch* patch) const {
  std::string secondary;
  for (size_t i : _engine->_collections[collection]->indices) {
    const IndexDef& index = _engine->_schema.indices[i];
    if (!index.unique) continue;
    if (patch != nullptr && !PatchReaches(index, *patch)) continue;
    secondary.clear();
    if (!index.extract(value, secondary)) continue;
    const IndexDeltas& deltas = _index_deltas[i];
    bool taken = false;
    if (const std::string* added = deltas.AddedFor(secondary)) {
      // It may have since gone again. Then the record is what says so.
      std::string full = secondary;
      full.append(*added);
      const IndexDelta* delta = deltas.Find(full);
      taken = delta != nullptr && delta->added && *added != key;
    }
    const uint64_t committed = _engine->_committed_seq.load(std::memory_order_relaxed);
    for (const IndexEntry* e = _engine->_indices[i]->LowerBound(secondary);
         !taken && e != nullptr && e->key().starts_with(secondary); e = OrderedIndex::Next(e)) {
      if (!e->VisibleAt(committed) || e->primary_key() == key) continue;
      const IndexDelta* delta = deltas.Find(e->key());
      taken = delta == nullptr || delta->added;
    }
    if (taken) {
      return absl::AlreadyExistsError(
          absl::StrCat("Another record has the same value in unique index ", index.name));
    }
  }
  return absl::OkStatus();
}

absl::Status Transaction::Put(size_t collection, BytesConstView key, BytesConstView value) {
  RETURN_IF_ERROR(CheckUnique(collection, AsStringView(key), value));
  PutUnchecked(collection, key, value);
  return absl::OkStatus();
}

void Transaction::PutUnchecked(size_t collection, BytesConstView key, BytesConstView value) {
  const std::string_view key_view = AsStringView(key);
  const uint64_t hash = HashKey(key_view);
  if (!_engine->_collections[collection]->indices.empty()) {
    UpdateIndices(collection, hash, key_view, GetHashed(collection, hash, key_view), value);
  }
  SetPending(collection, hash, key_view, Version::Copy(_engine->_pool, value));
}

bool Transaction::LogsPatches(size_t collection) const noexcept {
  return !_engine->_collections[collection]->ephemeral &&
         (_engine->_wal != nullptr || _engine->_ring->enabled());
}

absl::Status Transaction::Update(size_t collection, BytesConstView key, const MessagePatch& patch) {
  const std::string_view key_view = AsStringView(key);
  const uint64_t hash = HashKey(key_view);
  const std::optional<BytesConstView> current = GetHashed(collection, hash, key_view);
  if (!current.has_value()) return absl::NotFoundError("Key not found");

  Version* next = nullptr;
  std::string encoded;
  PendingWrite* pending = FindPending(collection, hash, key_view);
  if (!LogsPatches(collection) || (pending != nullptr && pending->patch.empty())) {
    // Nothing to log, or the record is new in this transaction and is logged whole.
    next = PatchedVersion(_engine->_pool, *current, patch);
  } else if (pending == nullptr) {
    next = PatchedVersion(_engine->_pool, *current, patch);
    patch.AppendEncoded(encoded);
  } else {
    // Updated before in this transaction. The log holds one patch per record, so the new state is
    // derived from the committed record just as a replay will, not from the previous update.
    std::optional<MessagePatch> merged = MessagePatch::Decode(pending->patch);
    const std::optional<BytesConstView> committed =
        _engine->ReadAt(collection, pending->hash, key_view,
                        _engine->_committed_seq.load(std::memory_order_relaxed));
    if (merged.has_value() && committed.has_value()) {
      merged->MergeFrom(patch);
      next = PatchedVersion(_engine->_pool, *committed, *merged);
      merged->AppendEncoded(encoded);
    }
  }
  if (next == nullptr) return absl::DataLossError("Malformed stored message");

  if (absl::Status unique = CheckUnique(collection, key_view, next->bytes(), &patch);
      !unique.ok()) {
    Version::Destroy(_engine->_pool, next);
    return unique;
  }
  if (!_engine->_collections[collection]->indices.empty()) {
    UpdateIndices(collection, hash, key_view, current, next->bytes(), &patch);
  }
  SetPending(collection, hash, key_view, next, std::move(encoded));
  return absl::OkStatus();
}

absl::Status Transaction::ApplyLoggedPatch(size_t collection, std::string_view key,
                                           const MessagePatch& patch, uint32_t result_crc) {
  const std::optional<BytesConstView> current = Get(collection, AsBytes(key));
  if (!current.has_value()) return absl::DataLossError("The log patches a missing record");
  Version* next = PatchedVersion(_engine->_pool, *current, patch);
  if (next == nullptr) return absl::DataLossError("The log patches a malformed record");
  if (CrcOf(next->bytes()) != result_crc) {
    Version::Destroy(_engine->_pool, next);
    return absl::DataLossError(
        "A patch from the log gave a different record: states have diverged");
  }
  const uint64_t hash = HashKey(key);
  if (!_engine->_collections[collection]->indices.empty()) {
    UpdateIndices(collection, hash, key, current, next->bytes(), &patch);
  }
  SetPending(collection, hash, key, next);
  return absl::OkStatus();
}

absl::Status Transaction::Delete(size_t collection, BytesConstView key) {
  const std::string_view key_view = AsStringView(key);
  const uint64_t hash = HashKey(key_view);
  const std::optional<BytesConstView> current = GetHashed(collection, hash, key_view);
  if (!current.has_value()) return absl::NotFoundError("Key not found");
  if (!_engine->_collections[collection]->indices.empty()) {
    UpdateIndices(collection, hash, key_view, current, std::nullopt);
  }
  SetPending(collection, hash, key_view, Version::Create(_engine->_pool, 0, true));
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
        Version::Destroy(_engine->_pool, write.version);
        write.version = nullptr;
        continue;
      }
    }
    any = true;
    logged = logged || !_engine->_collections[write.collection]->ephemeral;
  }
  return {any, logged};
}

absl::Status Transaction::Commit(CommitOptions options) {
  Engine& engine = *_engine;
  if (engine._broken.load(std::memory_order_acquire)) {
    Rollback();
    return absl::FailedPreconditionError("A checkpoint failed to install: reopen the database");
  }
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
        if (write.version == nullptr || engine._collections[write.collection]->ephemeral) {
          continue;
        }
        const uint32_t id = engine._collections[write.collection]->id;
        const BytesConstView value = write.version->bytes();
        if (write.version->tombstone) {
          internal::AddCommitEntry(buffer, id, internal::CommitOp::kDelete, write.key, {});
        } else if (!write.patch.empty() &&
                   internal::kPatchEntryOverhead + write.patch.size() < value.size()) {
          internal::AddCommitPatch(buffer, id, write.key, CrcOf(value), write.patch);
        } else {
          internal::AddCommitEntry(buffer, id, internal::CommitOp::kPut, write.key,
                                   AsStringView(value));
        }
        ++count;
      }
      internal::EndCommit(buffer, begin, count);
      frame = buffer;
    }
    if (engine._wal != nullptr) {
      const bool sync = options.sync.value_or(engine._options.sync_commits);
      const EngineObserver* observer = engine._options.observer.get();
      const auto started = observer != nullptr ? std::chrono::steady_clock::now()
                                               : std::chrono::steady_clock::time_point{};
      absl::Status status = engine._wal->Append(frame, lsn, sync);
      if (!status.ok()) {
        Rollback();
        return status;
      }
      if (observer != nullptr) {
        engine._options.observer->OnCommit(
            {lsn, _writes.size(), frame.size(), sync, std::chrono::steady_clock::now() - started});
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
  const bool track_digest = engine._digest_tracked.load(std::memory_order_relaxed);
  uint64_t digest = track_digest ? engine._state_digest.load(std::memory_order_relaxed) : 0;

  // Readers may run throughout, but ignore seq until it is published below.
  for (PendingWrite& write : _writes) {
    Version* version = write.version;
    if (version == nullptr) continue;
    RecordNode* node = write.node;
    if (node == nullptr) {
      node = engine.NodeForWrite(write.collection, write.hash, write.key, seq);
      write.node = node;
    }
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
    if (track_digest && !engine._collections[write.collection]->ephemeral) {
      const uint32_t id = engine._collections[write.collection]->id;
      if (older != nullptr && !older->tombstone) {
        digest -= internal::RecordDigest(id, write.key, older->bytes());
      }
      if (!version->tombstone) digest += internal::RecordDigest(id, write.key, version->bytes());
    }
    if (notify && write.collection < engine._schema.collections.size()) {
      // Both stay alive until the reclamation below.
      changes.push_back(
          {write.collection, AsBytes(node->key()),
           older != nullptr && !older->tombstone ? std::optional<BytesConstView>(older->bytes())
                                                 : std::nullopt,
           version->tombstone ? std::nullopt : std::optional<BytesConstView>(version->bytes())});
    }
  }
  for (size_t i = 0; i < _index_deltas.size(); ++i) {
    if (_index_deltas[i].empty()) continue;
    OrderedIndex::SortedEditor index(*engine._indices[i]);
    const PendingMap& pending = _pending[engine._schema.indices[i].collection];
    _index_deltas[i].Drain([&](std::string_view key, const IndexDelta& delta) {
      if (delta.added) {
        // What it was added for is in this transaction, and survives it, or the two would have
        // cancelled.
        const PendingWrite* write =
            pending.Find(delta.record_hash, key.substr(delta.secondary_size));
        index.Insert(key, delta.secondary_size, seq, write->node);
      } else if (IndexEntry* entry = index.FindLive(key)) {
        entry->deleted.store(seq, std::memory_order_relaxed);
        engine._garbage.push_back({seq, Engine::Garbage::kIndexEntryToUnlink, entry, i});
      }
    });
  }
  if (track_digest) engine._state_digest.store(digest, std::memory_order_relaxed);
  if (logged) {
    if (engine._ring->enabled()) engine._ring->Push(lsn, frame, digest);
    engine._lsn.store(lsn, std::memory_order_release);
  }
  engine._committed_seq.store(seq, std::memory_order_seq_cst);

  if (notify && !changes.empty()) engine.Notify(seq, changes);

  _writes.clear();
  for (auto& pending : _pending) pending.clear();
  engine.ReclaimUnpinned(seq);
}

}  // namespace gendb
