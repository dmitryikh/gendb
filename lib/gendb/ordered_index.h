#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <span>
#include <string_view>

#include "gendb/block_pool.h"

namespace gendb::internal {

struct RecordNode;

inline constexpr uint64_t kNeverDeleted = std::numeric_limits<uint64_t>::max();

// An index key, encoded so that byte order is value order, followed by the primary key. Visible
// to snapshots with created <= seq < deleted. Next pointers and key bytes follow it in memory.
struct IndexEntry {
  uint64_t created;
  std::atomic<uint64_t> deleted;
  // The record, which lives at least as long as this is visible to anybody, so that a scan has no
  // need to look it up by primary key. Null only at the head of a list.
  const RecordNode* node;
  uint32_t key_size;
  uint32_t secondary_size;
  uint8_t height;

  std::atomic<IndexEntry*>* next() noexcept {
    return reinterpret_cast<std::atomic<IndexEntry*>*>(this + 1);
  }
  const std::atomic<IndexEntry*>* next() const noexcept {
    return reinterpret_cast<const std::atomic<IndexEntry*>*>(this + 1);
  }
  std::string_view key() const noexcept {
    return {reinterpret_cast<const char*>(next() + height), key_size};
  }
  std::string_view primary_key() const noexcept { return key().substr(secondary_size); }
  bool VisibleAt(uint64_t seq) const noexcept {
    return created <= seq && seq < deleted.load(std::memory_order_relaxed);
  }
};

// Skip list with one writer and any number of lock-free readers. Removal is two-step, as readers
// may be standing on an entry: Unlink hides it from new traversals and keeps its next pointers
// intact, and the engine frees it once no snapshot can still reach it.
class OrderedIndex {
  static constexpr int kMaxHeight = 12;

 public:
  // Entries live in `pool`, which frees them when it goes.
  explicit OrderedIndex(BlockPool& pool)
      : _pool(pool), _head(NewEntry({}, 0, 0, nullptr, kMaxHeight)) {}
  OrderedIndex(const OrderedIndex&) = delete;
  OrderedIndex& operator=(const OrderedIndex&) = delete;

  void Destroy(IndexEntry* e) noexcept { _pool.Free(e, BlockSize(e->height, e->key_size)); }

  // Any thread, with a pinned snapshot: the first entry with key >= `key`.
  const IndexEntry* LowerBound(std::string_view key) const noexcept {
    const IndexEntry* e = _head;
    for (int level = _height.load(std::memory_order_relaxed) - 1; level >= 0; --level) {
      for (const IndexEntry* n = e->next()[level].load(std::memory_order_acquire);
           n != nullptr && n->key() < key; n = e->next()[level].load(std::memory_order_acquire)) {
        e = n;
      }
    }
    return e->next()[0].load(std::memory_order_acquire);
  }

  // Any thread, with a pinned snapshot: the last entry with key < `key`, or the last of all if
  // `key` is empty.
  const IndexEntry* LastBefore(std::string_view key) const noexcept {
    const IndexEntry* e = _head;
    for (int level = _height.load(std::memory_order_relaxed) - 1; level >= 0; --level) {
      for (const IndexEntry* n = e->next()[level].load(std::memory_order_acquire);
           n != nullptr && (key.empty() || n->key() < key);
           n = e->next()[level].load(std::memory_order_acquire)) {
        e = n;
      }
    }
    return e == _head ? nullptr : e;
  }

  static const IndexEntry* Next(const IndexEntry* e) noexcept {
    return e->next()[0].load(std::memory_order_acquire);
  }

  // Writer only. Equal keys, from a deleted entry and its re-insertion, stay in insertion order.
  void Insert(std::string_view key, uint32_t secondary_size, uint64_t created,
              const RecordNode* node) {
    std::array<IndexEntry*, kMaxHeight> preds;
    IndexEntry* e = _head;
    for (int level = kMaxHeight - 1; level >= 0; --level) {
      for (IndexEntry* n = e->next()[level].load(std::memory_order_relaxed);
           n != nullptr && n->key() <= key; n = e->next()[level].load(std::memory_order_relaxed)) {
        e = n;
      }
      preds[level] = e;
    }
    const int height = RandomHeight();
    IndexEntry* entry = NewEntry(key, secondary_size, created, node, height);
    if (height > _height.load(std::memory_order_relaxed)) {
      _height.store(height, std::memory_order_relaxed);
    }
    for (int level = 0; level < height; ++level) {
      entry->next()[level].store(preds[level]->next()[level].load(std::memory_order_relaxed),
                                 std::memory_order_relaxed);
      preds[level]->next()[level].store(entry, std::memory_order_release);
    }
  }

  // Writer only: changes in rising key order, each going on from where the last one was instead of
  // from the top, which in a large transaction saves most of the search.
  class SortedEditor {
   public:
    explicit SortedEditor(OrderedIndex& index) noexcept : _index(index) {
      _preds.fill(index._head);
    }

    IndexEntry* Insert(std::string_view key, uint32_t secondary_size, uint64_t created,
                       const RecordNode* node) {
      MoveTo<true>(key);
      const int height = _index.RandomHeight();
      IndexEntry* entry = _index.NewEntry(key, secondary_size, created, node, height);
      if (height > _index._height.load(std::memory_order_relaxed)) {
        _index._height.store(height, std::memory_order_relaxed);
      }
      for (int level = 0; level < height; ++level) {
        entry->next()[level].store(_preds[level]->next()[level].load(std::memory_order_relaxed),
                                   std::memory_order_relaxed);
        _preds[level]->next()[level].store(entry, std::memory_order_release);
        _preds[level] = entry;
      }
      return entry;
    }

    void Unlink(IndexEntry* target) noexcept {
      MoveTo<false>(target->key());
      for (int level = 0; level < target->height; ++level) {
        // Past any entries of the same key that were deleted before it.
        IndexEntry* pred = _preds[level];
        while (pred->next()[level].load(std::memory_order_relaxed) != target) {
          pred = pred->next()[level].load(std::memory_order_relaxed);
        }
        pred->next()[level].store(target->next()[level].load(std::memory_order_relaxed),
                                  std::memory_order_release);
      }
    }

    // The entry with this key that is not deleted yet.
    IndexEntry* FindLive(std::string_view key) noexcept {
      MoveTo<false>(key);
      IndexEntry* e = _preds[0]->next()[0].load(std::memory_order_relaxed);
      while (e != nullptr && e->key() == key) {
        if (e->deleted.load(std::memory_order_relaxed) == kNeverDeleted) return e;
        e = e->next()[0].load(std::memory_order_relaxed);
      }
      return nullptr;
    }

   private:
    // `_preds[level]` holds the last entry of each level before the previous key, which is before
    // this one. While its next entry is at or past this key, every level above needs no look
    // either, as a later entry there would be on this level too.
    template <bool kPastEqual>
    void MoveTo(std::string_view key) noexcept {
      const auto before = [key](const IndexEntry* e) {
        return e != nullptr && (kPastEqual ? e->key() <= key : e->key() < key);
      };
      int level = 0;
      while (level < kMaxHeight - 1 &&
             before(_preds[level]->next()[level].load(std::memory_order_relaxed))) {
        ++level;
      }
      IndexEntry* e = _preds[level];
      for (;; --level) {
        for (IndexEntry* n = e->next()[level].load(std::memory_order_relaxed); before(n);
             n = e->next()[level].load(std::memory_order_relaxed)) {
          e = n;
        }
        _preds[level] = e;
        if (level == 0) return;
      }
    }

    OrderedIndex& _index;
    std::array<IndexEntry*, kMaxHeight> _preds;
  };

  // Fills an empty index from sorted keys, in linear time, before any reader exists.
  struct SortedKey {
    const char* data;
    uint32_t size;
    uint32_t secondary_size;
    const RecordNode* node;
  };
  void BuildSorted(std::span<const SortedKey> keys, uint64_t created) {
    std::array<IndexEntry*, kMaxHeight> tails;
    tails.fill(_head);
    int top = 1;
    for (const SortedKey& key : keys) {
      const int height = RandomHeight();
      IndexEntry* entry =
          NewEntry({key.data, key.size}, key.secondary_size, created, key.node, height);
      for (int level = 0; level < height; ++level) {
        tails[level]->next()[level].store(entry, std::memory_order_relaxed);
        tails[level] = entry;
      }
      if (height > top) top = height;
    }
    _height.store(top, std::memory_order_release);
  }

  // Writer only: the entry with this key that is not deleted yet.
  IndexEntry* FindLive(std::string_view key) noexcept {
    auto* e = const_cast<IndexEntry*>(LowerBound(key));
    while (e != nullptr && e->key() == key) {
      if (e->deleted.load(std::memory_order_relaxed) == kNeverDeleted) return e;
      e = e->next()[0].load(std::memory_order_relaxed);
    }
    return nullptr;
  }

 private:
  static size_t BlockSize(int height, size_t key_size) noexcept {
    return sizeof(IndexEntry) + sizeof(std::atomic<IndexEntry*>) * height + key_size;
  }

  IndexEntry* NewEntry(std::string_view key, uint32_t secondary_size, uint64_t created,
                       const RecordNode* node, int height) {
    auto* e = static_cast<IndexEntry*>(_pool.Allocate(BlockSize(height, key.size())));
    e->created = created;
    new (&e->deleted) std::atomic<uint64_t>(kNeverDeleted);
    e->node = node;
    e->key_size = static_cast<uint32_t>(key.size());
    e->secondary_size = secondary_size;
    e->height = static_cast<uint8_t>(height);
    for (int i = 0; i < height; ++i) new (&e->next()[i]) std::atomic<IndexEntry*>(nullptr);
    if (!key.empty()) std::memcpy(e->next() + height, key.data(), key.size());
    return e;
  }

  // Each level holds a quarter of the entries of the level below.
  int RandomHeight() noexcept {
    _rng ^= _rng << 13;
    _rng ^= _rng >> 7;
    _rng ^= _rng << 17;
    const int height = std::countr_zero(_rng | (uint64_t{1} << 62)) / 2 + 1;
    return height < kMaxHeight ? height : kMaxHeight;
  }

  BlockPool& _pool;
  IndexEntry* _head;
  std::atomic<int> _height{1};
  uint64_t _rng = 0x9e3779b97f4a7c15;
};

}  // namespace gendb::internal
