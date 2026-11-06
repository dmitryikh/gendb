#include <chrono>
#include <utility>

#include "gendb/commit_ring.h"
#include "gendb/engine.h"
#include "gendb/engine_internal.h"
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

void Transaction::SetPending(size_t collection, uint64_t hash, std::string_view key, Version* value,
                             std::string patch) {
  if (PendingWrite* write = FindPending(collection, key)) {
    Version::Destroy(write->version);
    write->version = value;
    write->patch = std::move(patch);
    return;
  }
  PendingWrite& write = _writes.emplace_back(
      PendingWrite{collection, hash, std::string(key), value, nullptr, std::move(patch)});
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

bool Transaction::IsLogged(size_t collection) const noexcept {
  return (_engine->_wal != nullptr || _engine->_ring->enabled()) &&
         !_engine->_collections[collection]->ephemeral;
}

absl::Status Transaction::Update(size_t collection, BytesConstView key, const MessagePatch& patch) {
  const std::string_view key_view = AsStringView(key);
  const std::optional<BytesConstView> current = Get(collection, key);
  if (!current.has_value()) return absl::NotFoundError("Key not found");

  Version* next = nullptr;
  std::string encoded;
  PendingWrite* pending = FindPending(collection, key_view);
  if (!IsLogged(collection) || (pending != nullptr && pending->patch.empty())) {
    // Nothing to log, or the record is new in this transaction and is logged whole.
    next = PatchedVersion(*current, patch);
  } else if (pending == nullptr) {
    next = PatchedVersion(*current, patch);
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
      next = PatchedVersion(*committed, *merged);
      merged->AppendEncoded(encoded);
    }
  }
  if (next == nullptr) return absl::DataLossError("Malformed stored message");

  if (!_engine->_collections[collection]->indices.empty()) {
    UpdateIndices(collection, key_view, current, next->bytes());
  }
  SetPending(collection, HashKey(key_view), key_view, next, std::move(encoded));
  return absl::OkStatus();
}

absl::Status Transaction::ApplyLoggedPatch(size_t collection, std::string_view key,
                                           const MessagePatch& patch, uint32_t result_crc) {
  const std::optional<BytesConstView> current = Get(collection, AsBytes(key));
  if (!current.has_value()) return absl::DataLossError("The log patches a missing record");
  Version* next = PatchedVersion(*current, patch);
  if (next == nullptr) return absl::DataLossError("The log patches a malformed record");
  if (CrcOf(next->bytes()) != result_crc) {
    Version::Destroy(next);
    return absl::DataLossError(
        "A patch from the log gave a different record: states have diverged");
  }
  if (!_engine->_collections[collection]->indices.empty()) {
    UpdateIndices(collection, key, current, next->bytes());
  }
  SetPending(collection, HashKey(key), key, next);
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
