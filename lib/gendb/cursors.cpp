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

}  // namespace gendb
