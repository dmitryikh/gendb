#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <utility>
#include <vector>

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define GENDB_POOL_POISON 1
#endif
#elif defined(__SANITIZE_ADDRESS__)
#define GENDB_POOL_POISON 1
#endif
#ifdef GENDB_POOL_POISON
#include <sanitizer/asan_interface.h>
#endif

namespace gendb::internal {

// Memory for what a database keeps of each record: its versions, its node and its index entries.
// They come and go by the million, and a general allocator spends more time on them than the rest
// of a write does: glibc's took more than half of a benchmark's run time.
//
// Blocks of one size go back to a list for that size, and new ones come from large chunks, in the
// order they were asked for, which also keeps a record's parts together in memory. Memory goes back
// to the system only when the pool goes, all at once, so that closing a database does not walk its
// records. What a large delete frees waits for writes that want the same sizes.
//
// Not thread-safe: the engine uses it from under the writer lock, or while opening.
class BlockPool {
 public:
  BlockPool() = default;
  BlockPool(const BlockPool&) = delete;
  BlockPool& operator=(const BlockPool&) = delete;
  ~BlockPool() {
    for (Chunk& chunk : _chunks) {
#ifdef GENDB_POOL_POISON
      ASAN_UNPOISON_MEMORY_REGION(chunk.memory.get(), chunk.size);
#endif
      (void)chunk;
    }
    while (_large != nullptr) {
      Large* next = _large->next;
      ::operator delete(_large);
      _large = next;
    }
  }

  void* Allocate(size_t size) {
    if (size > kMaxSmall) [[unlikely]]
      return AllocateLarge(size);
    const size_t index = ClassOf(size);
    void* block = _free[index];
    if (block != nullptr) {
#ifdef GENDB_POOL_POISON
      ASAN_UNPOISON_MEMORY_REGION(block, index * kGranule);
#endif
      _free[index] = *static_cast<void**>(block);
      return block;
    }
    const size_t rounded = index * kGranule;
    if (static_cast<size_t>(_end - _next) < rounded) [[unlikely]]
      NewChunk();
    block = _next;
    _next += rounded;
#ifdef GENDB_POOL_POISON
    ASAN_UNPOISON_MEMORY_REGION(block, rounded);
#endif
    return block;
  }

  // `size` as given to Allocate.
  void Free(void* block, size_t size) noexcept {
    if (size > kMaxSmall) [[unlikely]] {
      FreeLarge(block);
      return;
    }
    const size_t index = ClassOf(size);
    *static_cast<void**>(block) = _free[index];
    _free[index] = block;
#ifdef GENDB_POOL_POISON
    ASAN_POISON_MEMORY_REGION(block, index * kGranule);
#endif
  }

 private:
  static constexpr size_t kGranule = 16;
  static constexpr size_t kMaxSmall = 4096;
  static constexpr size_t kMinChunk = size_t{64} << 10;
  static constexpr size_t kMaxChunk = size_t{4} << 20;

  struct Chunk {
    std::unique_ptr<std::byte[]> memory;
    size_t size;
  };
  struct alignas(kGranule) Large {
    Large* previous;
    Large* next;
  };

  static size_t ClassOf(size_t size) noexcept {
    return size == 0 ? 1 : (size + kGranule - 1) / kGranule;
  }

  void NewChunk() {
    // A small database stays small, and a large one soon asks for little of the system.
    const size_t size = _chunks.empty() ? kMinChunk : std::min(_chunks.back().size * 2, kMaxChunk);
    auto memory = std::make_unique_for_overwrite<std::byte[]>(size);
    _next = memory.get();
    _end = _next + size;
#ifdef GENDB_POOL_POISON
    ASAN_POISON_MEMORY_REGION(_next, size);
#endif
    _chunks.push_back({std::move(memory), size});
  }

  void* AllocateLarge(size_t size) {
    auto* large = static_cast<Large*>(::operator new(sizeof(Large) + size));
    large->previous = nullptr;
    large->next = _large;
    if (_large != nullptr) _large->previous = large;
    _large = large;
    return large + 1;
  }

  void FreeLarge(void* block) noexcept {
    Large* large = static_cast<Large*>(block) - 1;
    if (large->previous != nullptr) {
      large->previous->next = large->next;
    } else {
      _large = large->next;
    }
    if (large->next != nullptr) large->next->previous = large->previous;
    ::operator delete(large);
  }

  std::array<void*, kMaxSmall / kGranule + 1> _free{};
  std::byte* _next = nullptr;
  std::byte* _end = nullptr;
  std::vector<Chunk> _chunks;
  Large* _large = nullptr;
};

}  // namespace gendb::internal
