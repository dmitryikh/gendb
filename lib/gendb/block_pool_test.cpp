#include "gendb/block_pool.h"

#include <gtest/gtest.h>

#include <cstring>
#include <random>
#include <vector>

namespace gendb::internal {
namespace {

TEST(BlockPoolTest, BlocksAreAlignedAndApart) {
  BlockPool pool;
  std::vector<std::pair<std::byte*, size_t>> blocks;
  for (size_t size = 1; size < 9000; size += 37) {
    auto* block = static_cast<std::byte*>(pool.Allocate(size));
    ASSERT_EQ(reinterpret_cast<uintptr_t>(block) % 16, 0u);
    std::memset(block, static_cast<int>(size & 0xff), size);
    blocks.emplace_back(block, size);
  }
  for (const auto& [block, size] : blocks) {
    for (size_t i = 0; i < size; ++i) {
      ASSERT_EQ(block[i], static_cast<std::byte>(size & 0xff));
    }
    pool.Free(block, size);
  }
}

TEST(BlockPoolTest, AFreedBlockComesBackForTheSameSize) {
  BlockPool pool;
  void* first = pool.Allocate(100);
  pool.Free(first, 100);
  EXPECT_EQ(pool.Allocate(100), first);
  // Sizes round up to the same class.
  pool.Free(first, 100);
  EXPECT_EQ(pool.Allocate(105), first);
}

TEST(BlockPoolTest, EmptyBlocksAreStillBlocks) {
  BlockPool pool;
  void* a = pool.Allocate(0);
  void* b = pool.Allocate(0);
  EXPECT_NE(a, b);
  pool.Free(a, 0);
  pool.Free(b, 0);
}

// Goes through more than a chunk, with blocks of every kind coming and going. AddressSanitizer
// checks that nothing overlaps, as the pool poisons what is free.
TEST(BlockPoolTest, ManyBlocksComeAndGo) {
  BlockPool pool;
  std::mt19937 rng(7);
  struct Live {
    std::byte* block;
    size_t size;
    std::byte fill;
  };
  std::vector<Live> live;
  for (int i = 0; i < 200000; ++i) {
    if (live.empty() || rng() % 3 != 0) {
      const size_t size = rng() % 16 == 0 ? rng() % 10000 : rng() % 300;
      auto* block = static_cast<std::byte*>(pool.Allocate(size));
      const auto fill = static_cast<std::byte>(rng());
      std::memset(block, static_cast<int>(fill), size);
      live.push_back({block, size, fill});
    } else {
      const size_t at = rng() % live.size();
      const Live gone = live[at];
      live[at] = live.back();
      live.pop_back();
      if (gone.size > 0) {
        ASSERT_EQ(gone.block[0], gone.fill);
        ASSERT_EQ(gone.block[gone.size - 1], gone.fill);
      }
      pool.Free(gone.block, gone.size);
    }
  }
  // What is left goes with the pool.
}

}  // namespace
}  // namespace gendb::internal
