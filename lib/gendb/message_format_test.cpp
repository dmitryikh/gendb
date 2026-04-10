
#include <gtest/gtest.h>

#include <array>

#include "gendb/message_base.h"
#include "gendb/message_builder.h"

using namespace gendb;

namespace {

constexpr std::array<FixedFieldDescriptor, 4> kSegmentedFixedDescriptors = {
    FixedFieldDescriptor{1, 1, FixedStorageWidth::Byte1, 0, 0, 0u, 0},
    FixedFieldDescriptor{2, 2, FixedStorageWidth::Byte4, 0, 0, 0u, 0},
    FixedFieldDescriptor{3, 4, FixedStorageWidth::Byte4, 1, 1, 1u, 0},
    FixedFieldDescriptor{4, 8, FixedStorageWidth::Byte8, 0, 0, 0u, 0},
};

constexpr std::array<uint8_t, kFixedStorageClassCount> kSegmentedFixedWordCounts = {1, 1, 1};
constexpr MessageLayout kSegmentedLayout{kSegmentedFixedDescriptors, kSegmentedFixedWordCounts};

}  // namespace

TEST(MessageBaseTest, EmptyMessageHasNoFields) {
  MessageBase msg;
  EXPECT_EQ(msg.FieldCount(), 0);
  EXPECT_FALSE(msg.HasField(0));
}

TEST(MessageBaseTest, ScalarFieldReadWrite) {
  MessageBuilder builder;
  builder.AddField<int32_t>(1, 42);
  builder.AddField<bool>(2, true);
  std::vector<uint8_t> buffer = builder.Build();
  ASSERT_EQ(buffer.size(), internal::kMessageHeaderSize + 11);
  MessageBase msg(buffer);

  EXPECT_EQ(msg.ReadScalarField<int32_t>(1, 0), 42);
  EXPECT_EQ(msg.ReadScalarField<bool>(2, false), true);
}

TEST(MessageBaseTest, LargeSparseFieldIdsStayCompact) {
  MessageBuilder builder;
  builder.AddField<uint8_t>(1000, 42);
  std::vector<uint8_t> buffer = builder.Build();

  EXPECT_LT(buffer.size(), 32);
  MessageBase msg(buffer);
  EXPECT_EQ(msg.ReadScalarField<uint8_t>(1000, 0), 42);
}

TEST(MessageBaseTest, RepeatedSparseReadsReturnStableValues) {
  MessageBuilder builder;
  builder.AddStringField(3, "alpha");
  builder.AddStringField(1000, "omega");
  std::vector<uint8_t> buffer = builder.Build();
  MessageBase msg(buffer);

  EXPECT_EQ(msg.ReadStringField(3, ""), "alpha");
  EXPECT_EQ(msg.ReadStringField(3, ""), "alpha");
  EXPECT_EQ(msg.ReadStringField(1000, ""), "omega");
  EXPECT_EQ(msg.ReadStringField(1000, ""), "omega");
}

TEST(MessageBaseTest, SegmentedFixedLayoutReadsMixedWidths) {
  MessageBuilder builder(&kSegmentedLayout);
  builder.AddField<bool>(1, true);
  builder.AddField<int16_t>(2, 7);
  builder.AddField<int64_t>(4, 9);
  std::vector<uint8_t> buffer = builder.Build();
  MessageBase msg(buffer, &kSegmentedLayout);

  EXPECT_EQ(msg.ReadScalarField<bool>(1, false), true);
  EXPECT_EQ(msg.ReadScalarField<int16_t>(2, 0), 7);
  EXPECT_EQ(msg.ReadScalarField<int32_t>(3, 0), 0);
  EXPECT_EQ(msg.ReadScalarField<int64_t>(4, 0), 9);
}

TEST(MessageBaseTest, TwoByteFixedFieldUsesFourByteStorageForPatching) {
  MessageBuilder builder(&kSegmentedLayout);
  builder.AddField<int16_t>(2, 17);
  std::vector<uint8_t> buffer = builder.Build();
  MessageBase msg(buffer, &kSegmentedLayout);

  EXPECT_EQ(msg.FieldRaw(2).size(), 4);
  EXPECT_EQ(msg.ReadScalarField<int16_t>(2, 0), 17);

  ASSERT_TRUE(msg.SetScalarField<int16_t>(2, 33));
  EXPECT_EQ(msg.ReadScalarField<int16_t>(2, 0), 33);
  EXPECT_EQ(msg.FieldRaw(2).size(), 4);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
