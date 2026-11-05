#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <string_view>

namespace gendb::internal {

inline constexpr uint64_t kNeverDeleted = std::numeric_limits<uint64_t>::max();

// An index key, encoded so that byte order is value order, followed by the primary key. Visible
// to snapshots with created <= seq < deleted. Next pointers and key bytes follow it in memory.
struct IndexEntry {
  uint64_t created;
  std::atomic<uint64_t> deleted;
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
 public:
  OrderedIndex() : _head(NewEntry({}, 0, 0, kMaxHeight)) {}
  ~OrderedIndex() {
    IndexEntry* e = _head;
    while (e != nullptr) {
      IndexEntry* next = e->next()[0].load(std::memory_order_relaxed);
      Destroy(e);
      e = next;
    }
  }
  OrderedIndex(const OrderedIndex&) = delete;
  OrderedIndex& operator=(const OrderedIndex&) = delete;

  static void Destroy(IndexEntry* e) noexcept { ::operator delete(e); }

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

  static const IndexEntry* Next(const IndexEntry* e) noexcept {
    return e->next()[0].load(std::memory_order_acquire);
  }

  // Writer only. Equal keys, from a deleted entry and its re-insertion, stay in insertion order.
  void Insert(std::string_view key, uint32_t secondary_size, uint64_t created) {
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
    IndexEntry* entry = NewEntry(key, secondary_size, created, height);
    if (height > _height.load(std::memory_order_relaxed)) {
      _height.store(height, std::memory_order_relaxed);
    }
    for (int level = 0; level < height; ++level) {
      entry->next()[level].store(preds[level]->next()[level].load(std::memory_order_relaxed),
                                 std::memory_order_relaxed);
      preds[level]->next()[level].store(entry, std::memory_order_release);
    }
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

  // Writer only.
  void Unlink(IndexEntry* target) noexcept {
    IndexEntry* e = _head;
    const std::string_view key = target->key();
    for (int level = _height.load(std::memory_order_relaxed) - 1; level >= 0; --level) {
      for (IndexEntry* n = e->next()[level].load(std::memory_order_relaxed);
           n != nullptr && n->key() < key; n = e->next()[level].load(std::memory_order_relaxed)) {
        e = n;
      }
      if (level >= target->height) continue;
      IndexEntry* pred = e;
      while (pred->next()[level].load(std::memory_order_relaxed) != target) {
        pred = pred->next()[level].load(std::memory_order_relaxed);
      }
      pred->next()[level].store(target->next()[level].load(std::memory_order_relaxed),
                                std::memory_order_release);
    }
  }

 private:
  static constexpr int kMaxHeight = 12;

  static IndexEntry* NewEntry(std::string_view key, uint32_t secondary_size, uint64_t created,
                              int height) {
    const size_t links = sizeof(std::atomic<IndexEntry*>) * height;
    auto* e = static_cast<IndexEntry*>(::operator new(sizeof(IndexEntry) + links + key.size()));
    e->created = created;
    new (&e->deleted) std::atomic<uint64_t>(kNeverDeleted);
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

  IndexEntry* _head;
  std::atomic<int> _height{1};
  uint64_t _rng = 0x9e3779b97f4a7c15;
};

}  // namespace gendb::internal
