#include <algorithm>
#include <utility>

#include "gendb/engine.h"
#include "gendb/engine_internal.h"

namespace gendb {

using internal::AsBytes;
using internal::HashKey;
using internal::IndexEntry;
using internal::OrderedIndex;
using internal::RecordNode;
using internal::RecordTable;
using internal::Version;

ScanCursor::ScanCursor(const EngineSnapshot& snapshot, size_t collection)
    : _seq(snapshot._seq), _slots(snapshot._engine->_collections[collection]->table.LoadSlots()) {
  Next();
}

ScanCursor::ScanCursor(const Transaction& txn, size_t collection)
    : _seq(txn._engine->_committed_seq.load(std::memory_order_relaxed)),
      _slots(txn._engine->_collections[collection]->table.LoadSlots()),
      _txn(&txn),
      _collection(collection) {
  Next();
}

void ScanCursor::Next() noexcept {
  _value.reset();
  while (_next <= _slots->mask) {
    const RecordNode* node = _slots->slots[_next++].load(std::memory_order_acquire);
    if (node == nullptr || node == RecordTable::Erased()) continue;
    // A record the transaction changed comes with the uncommitted ones, below.
    if (_txn != nullptr && _txn->FindPending(_collection, node->key()) != nullptr) continue;
    const Version* v = node->head.load(std::memory_order_acquire);
    while (v != nullptr && v->seq > _seq) v = v->older;
    if (v != nullptr && !v->tombstone) {
      _key = node->key();
      _value = v->bytes();
      return;
    }
  }
  if (_txn == nullptr) return;
  while (_next_write < _txn->_writes.size()) {
    const auto& write = _txn->_writes[_next_write++];
    if (write.collection != _collection || write.version->tombstone) continue;
    _key = write.key;
    _value = write.version->bytes();
    return;
  }
}

IndexCursor::IndexCursor(const EngineSnapshot& snapshot, size_t index, std::string_view lo,
                         std::string hi, ScanOptions options)
    : IndexCursor(*snapshot._engine, index, snapshot._seq, nullptr, lo, std::move(hi), options) {}

IndexCursor::IndexCursor(const Transaction& txn, size_t index, std::string_view lo, std::string hi,
                         ScanOptions options)
    : IndexCursor(*txn._engine, index, txn._engine->_committed_seq.load(std::memory_order_relaxed),
                  &txn, lo, std::move(hi), options) {}

IndexCursor::IndexCursor(const Engine& engine, size_t index, uint64_t seq, const Transaction* txn,
                         std::string_view lo, std::string hi, ScanOptions options)
    : _engine(&engine),
      _collection(engine._schema.indices[index].collection),
      _seq(seq),
      _txn(txn),
      _index(engine._indices[index].get()),
      _lo(lo),
      _hi(std::move(hi)),
      _options(options) {
  if (txn != nullptr) {
    const auto& deltas = txn->_index_deltas[index];
    for (auto it = deltas.lower_bound(lo); it != deltas.end() && (_hi.empty() || it->first < _hi);
         ++it) {
      _deltas.push_back({it->first, it->second.added, it->second.secondary_size});
    }
    if (_options.reverse) std::reverse(_deltas.begin(), _deltas.end());
  }
  _entry = _options.reverse ? PreviousVisible(_hi) : NextVisible(_index->LowerBound(lo));
  Next();
}

const IndexEntry* IndexCursor::NextVisible(const IndexEntry* e) const noexcept {
  while (e != nullptr && (_hi.empty() || e->key() < _hi)) {
    if (e->VisibleAt(_seq)) return e;
    e = OrderedIndex::Next(e);
  }
  return nullptr;
}

const IndexEntry* IndexCursor::PreviousVisible(std::string_view bound) const noexcept {
  for (;;) {
    const IndexEntry* e = _index->LastBefore(bound);
    if (e == nullptr || e->key() < _lo) return nullptr;
    // A deleted entry and its re-insertion have the same key, and a snapshot can see either.
    // Stepping back from the last of them would skip the others.
    const std::string_view key = e->key();
    for (const IndexEntry* same = _index->LowerBound(key); same != nullptr && same->key() == key;
         same = OrderedIndex::Next(same)) {
      if (same->VisibleAt(_seq)) return same;
    }
    bound = key;
  }
}

std::optional<BytesConstView> IndexCursor::Lookup(std::string_view primary_key) const noexcept {
  if (_txn != nullptr) return _txn->Get(_collection, AsBytes(primary_key));
  return _engine->ReadAt(_collection, HashKey(primary_key), primary_key, _seq);
}

void IndexCursor::Next() {
  _value.reset();
  if (_options.limit != 0 && _returned == _options.limit) return;
  const auto advance = [this] {
    _entry =
        _options.reverse ? PreviousVisible(_entry->key()) : NextVisible(OrderedIndex::Next(_entry));
  };
  while (!_value.has_value()) {
    const Delta* delta = _next_delta < _deltas.size() ? &_deltas[_next_delta] : nullptr;
    if (_entry == nullptr && delta == nullptr) return;
    const bool delta_first =
        delta != nullptr && (_entry == nullptr || (_options.reverse ? delta->key >= _entry->key()
                                                                    : delta->key <= _entry->key()));
    if (delta_first) {
      // A delta for a committed entry replaces it.
      if (_entry != nullptr && delta->key == _entry->key()) advance();
      if (delta->added) {
        _primary_key = std::string_view(delta->key).substr(delta->secondary_size);
        _value = Lookup(_primary_key);
      }
      ++_next_delta;
    } else {
      _primary_key = _entry->primary_key();
      _value = Lookup(_primary_key);
      advance();
    }
  }
  ++_returned;
}

}  // namespace gendb
