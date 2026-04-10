
#include <gtest/gtest.h>

#include "gendb/message_base.h"
#include "gendb/message_builder.h"

using namespace gendb;

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
  ASSERT_EQ(buffer.size(), internal::kMessageHeaderSize + 6 + 5);
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

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
