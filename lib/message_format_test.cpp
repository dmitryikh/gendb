#include "message_format.h"

#include <gtest/gtest.h>

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
  ASSERT_EQ(buffer.size(), 2 + 3 * 2 + 4 + 1);
  MessageBase msg(buffer);

  EXPECT_EQ(msg.ReadScalarField<int32_t>(1, 0), 42);
  EXPECT_EQ(msg.ReadScalarField<bool>(2, false), true);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}