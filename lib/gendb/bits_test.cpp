#include "gendb/bits.h"

#include <gtest/gtest.h>

#include <vector>
using gendb::IsBitmaskSubset;

TEST(BitmaskSubsetTest, BothZeroSized) {
  std::vector<uint32_t> a, b;
  EXPECT_TRUE(IsBitmaskSubset(a, b));
}

TEST(BitmaskSubsetTest, BZeroSizedANonEmpty) {
  std::vector<uint32_t> a = {0xFFFFFFFF};
  std::vector<uint32_t> b;
  EXPECT_TRUE(IsBitmaskSubset(a, b));
}

TEST(BitmaskSubsetTest, AZeroSizedBNonEmpty) {
  std::vector<uint32_t> a;
  std::vector<uint32_t> b = {0};
  EXPECT_TRUE(IsBitmaskSubset(a, b));
  std::vector<uint32_t> b2 = {1};
  EXPECT_FALSE(IsBitmaskSubset(a, b2));
}

TEST(BitmaskSubsetTest, SameSizeBSubsetOfA) {
  std::vector<uint32_t> a = {0b1111};
  std::vector<uint32_t> b = {0b0110};
  EXPECT_TRUE(IsBitmaskSubset(a, b));
}

TEST(BitmaskSubsetTest, SameSizeBNotSubsetOfA) {
  std::vector<uint32_t> a = {0b0101};
  std::vector<uint32_t> b = {0b0110};
  EXPECT_FALSE(IsBitmaskSubset(a, b));
}

TEST(BitmaskSubsetTest, BLargerThanATrailingZeros) {
  std::vector<uint32_t> a = {0b1111};
  std::vector<uint32_t> b = {0b0111, 0};
  EXPECT_TRUE(IsBitmaskSubset(a, b));
}

TEST(BitmaskSubsetTest, BLargerThanATrailingNonZeros) {
  std::vector<uint32_t> a = {0b1111};
  std::vector<uint32_t> b = {0b0111, 1};
  EXPECT_FALSE(IsBitmaskSubset(a, b));
}

TEST(BitmaskSubsetTest, ALargerThanB) {
  std::vector<uint32_t> a = {0b1111, 0b1111};
  std::vector<uint32_t> b = {0b0111};
  EXPECT_TRUE(IsBitmaskSubset(a, b));
}

TEST(BitmaskSubsetTest, BothAllZeros) {
  std::vector<uint32_t> a = {0, 0};
  std::vector<uint32_t> b = {0, 0};
  EXPECT_TRUE(IsBitmaskSubset(a, b));
}

TEST(BitmaskSubsetTest, BAllZerosANonZero) {
  std::vector<uint32_t> a = {0xFFFFFFFF, 0xFFFFFFFF};
  std::vector<uint32_t> b = {0, 0};
  EXPECT_TRUE(IsBitmaskSubset(a, b));
}

TEST(BitmaskSubsetTest, BAllZerosASmaller) {
  std::vector<uint32_t> a = {0xFFFFFFFF};
  std::vector<uint32_t> b = {0, 0};
  EXPECT_TRUE(IsBitmaskSubset(a, b));
}

TEST(BitmaskSubsetTest, BAllZerosAEmpty) {
  std::vector<uint32_t> a;
  std::vector<uint32_t> b = {0, 0};
  EXPECT_TRUE(IsBitmaskSubset(a, b));
}

TEST(ForEachSetFieldTest, EmptyMask) {
  std::vector<uint32_t> mask;
  std::vector<int> fields;
  gendb::ForEachSetField(mask, [&](int field_id) { fields.push_back(field_id); });
  EXPECT_TRUE(fields.empty());
}

TEST(ForEachSetFieldTest, SingleWord) {
  std::vector<uint32_t> mask = {0b10101};
  std::vector<int> fields;
  gendb::ForEachSetField(mask, [&](int field_id) { fields.push_back(field_id); });
  std::vector<int> expected = {1, 3, 5};
  EXPECT_EQ(fields, expected);
}

TEST(ForEachSetFieldTest, MultiWord) {
  std::vector<uint32_t> mask = {0b10101, 0b100};
  std::vector<int> fields;
  gendb::ForEachSetField(mask, [&](int field_id) { fields.push_back(field_id); });
  std::vector<int> expected = {1, 3, 5, 35};
  EXPECT_EQ(fields, expected);
}

TEST(ForEachSetFieldTest, AllBitsSet) {
  std::vector<uint32_t> mask = {0xFFFFFFFF};
  std::vector<int> fields;
  gendb::ForEachSetField(mask, [&](int field_id) { fields.push_back(field_id); });
  std::vector<int> expected(32);
  for (int i = 0; i < 32; ++i) expected[i] = i + 1;
  EXPECT_EQ(fields, expected);
}