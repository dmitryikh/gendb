#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string_view>

#include "gendb/bytes.h"

namespace gendb::internal {

// One immutable value of a record, followed in memory by its bytes. Versions of a record form a
// newest-first chain. Everything except `older` is fixed once the version is published.
struct Version {
  uint64_t seq;
  Version* older;
  uint32_t size;
  bool tombstone;

  static Version* Create(size_t size, bool tombstone) {
    auto* v = static_cast<Version*>(::operator new(sizeof(Version) + size));
    v->seq = 0;
    v->older = nullptr;
    v->size = static_cast<uint32_t>(size);
    v->tombstone = tombstone;
    return v;
  }
  static Version* Copy(BytesConstView bytes) {
    Version* v = Create(bytes.size(), false);
    if (!bytes.empty()) std::memcpy(v->data(), bytes.data(), bytes.size());
    return v;
  }
  static void Destroy(Version* v) noexcept { ::operator delete(v); }

  uint8_t* data() noexcept { return reinterpret_cast<uint8_t*>(this + 1); }
  const uint8_t* data() const noexcept { return reinterpret_cast<const uint8_t*>(this + 1); }
  BytesConstView bytes() const noexcept { return {data(), size}; }
};

// A key and its version chain, followed in memory by the key bytes.
struct RecordNode {
  std::atomic<Version*> head;
  uint64_t hash;
  uint32_t key_size;

  std::string_view key() const noexcept {
    return {reinterpret_cast<const char*>(this + 1), key_size};
  }
};

inline std::string_view AsStringView(BytesConstView bytes) noexcept {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

// Open-addressing hash table with one writer and any number of lock-free readers. Slots hold
// pointers, so a lookup is a few atomic loads and never blocks or is blocked by a commit.
//
// Reclamation is the caller's: a replaced slot array and an erased node stay valid for readers
// until the engine knows no snapshot can reach them.
class RecordTable {
 public:
  struct Slots {
    size_t mask;
    std::unique_ptr<std::atomic<RecordNode*>[]> slots;
  };

  RecordTable() : _slots(NewSlots(16)) {}
  ~RecordTable() {
    const Slots* slots = _slots.load(std::memory_order_relaxed);
    for (size_t i = 0; i <= slots->mask; ++i) {
      RecordNode* node = slots->slots[i].load(std::memory_order_relaxed);
      if (node == nullptr || node == Erased()) continue;
      Version* v = node->head.load(std::memory_order_relaxed);
      while (v != nullptr) {
        Version* older = v->older;
        Version::Destroy(v);
        v = older;
      }
      DestroyNode(node);
    }
    DestroySlots(slots);
  }
  RecordTable(const RecordTable&) = delete;
  RecordTable& operator=(const RecordTable&) = delete;

  static RecordNode* Erased() noexcept { return reinterpret_cast<RecordNode*>(uintptr_t{1}); }
  static void DestroyNode(RecordNode* node) noexcept { ::operator delete(node); }
  static void DestroySlots(const Slots* slots) noexcept { delete slots; }

  const Slots* LoadSlots() const noexcept { return _slots.load(std::memory_order_acquire); }

  // Any thread, with a pinned snapshot.
  RecordNode* Find(uint64_t hash, std::string_view key) const noexcept {
    const Slots* slots = LoadSlots();
    for (size_t i = hash & slots->mask;; i = (i + 1) & slots->mask) {
      RecordNode* node = slots->slots[i].load(std::memory_order_acquire);
      if (node == nullptr) return nullptr;
      if (node != Erased() && node->hash == hash && node->key() == key) return node;
    }
  }

  // Writer only. Returns the slot array this call replaced, if any, for later reclamation.
  RecordNode* Insert(uint64_t hash, std::string_view key, const Slots** replaced) {
    if ((_used + 1) * 10 > (_slots.load(std::memory_order_relaxed)->mask + 1) * 7) {
      *replaced = Rehash();
    }
    auto* node = static_cast<RecordNode*>(::operator new(sizeof(RecordNode) + key.size()));
    new (&node->head) std::atomic<Version*>(nullptr);
    node->hash = hash;
    node->key_size = static_cast<uint32_t>(key.size());
    if (!key.empty()) std::memcpy(node + 1, key.data(), key.size());

    const Slots* slots = _slots.load(std::memory_order_relaxed);
    for (size_t i = hash & slots->mask;; i = (i + 1) & slots->mask) {
      RecordNode* current = slots->slots[i].load(std::memory_order_relaxed);
      if (current == nullptr || current == Erased()) {
        if (current == nullptr) ++_used;
        ++_live;
        slots->slots[i].store(node, std::memory_order_release);
        return node;
      }
    }
  }

  // Writer only. The node stays valid for readers already holding it.
  void Erase(RecordNode* node) noexcept {
    const Slots* slots = _slots.load(std::memory_order_relaxed);
    for (size_t i = node->hash & slots->mask;; i = (i + 1) & slots->mask) {
      if (slots->slots[i].load(std::memory_order_relaxed) == node) {
        slots->slots[i].store(Erased(), std::memory_order_release);
        --_live;
        return;
      }
    }
  }

  size_t live() const noexcept { return _live; }

 private:
  static Slots* NewSlots(size_t capacity) {
    return new Slots{capacity - 1, std::make_unique<std::atomic<RecordNode*>[]>(capacity)};
  }

  const Slots* Rehash() {
    const Slots* old = _slots.load(std::memory_order_relaxed);
    const size_t old_capacity = old->mask + 1;
    Slots* next = NewSlots(_live * 2 >= old_capacity ? old_capacity * 2 : old_capacity);
    for (size_t i = 0; i < old_capacity; ++i) {
      RecordNode* node = old->slots[i].load(std::memory_order_relaxed);
      if (node == nullptr || node == Erased()) continue;
      size_t at = node->hash & next->mask;
      while (next->slots[at].load(std::memory_order_relaxed) != nullptr) at = (at + 1) & next->mask;
      next->slots[at].store(node, std::memory_order_relaxed);
    }
    _used = _live;
    _slots.store(next, std::memory_order_release);
    return old;
  }

  std::atomic<const Slots*> _slots;  // Replaced only by the writer.
  size_t _used = 0;                  // Occupied and erased slots.
  size_t _live = 0;
};

}  // namespace gendb::internal
