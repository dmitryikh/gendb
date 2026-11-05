#include "generated/primitive_database.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace gendb::tests::primitive;

TEST(PrimitiveDbTest, AddAndGetMessageA) {
  Db db;
  {
    auto writer = db.CreateWriter();
    MessageABuilder builder;
    builder.set_key(KeyEnum::kFirstValue);
    builder.set_data("value42");
    std::vector<uint8_t> buffer = builder.Build();
    EXPECT_TRUE(writer.PutMessageA(KeyEnum::kFirstValue, buffer).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  {
    auto snapshot = db.CreateSnapshot();
    MessageA message_a;
    EXPECT_TRUE(snapshot.GetMessageA(KeyEnum::kFirstValue, message_a).ok());
    EXPECT_EQ(message_a.key(), KeyEnum::kFirstValue);
    EXPECT_EQ(message_a.data(), "value42");
  }
}

TEST(PrimitiveDbTest, UpdateMessageAWithPatch) {
  Db db;
  {
    auto writer = db.CreateWriter();
    MessageABuilder builder;
    builder.set_key(KeyEnum::kSecondValue);
    builder.set_data("initial");
    std::vector<uint8_t> buffer = builder.Build();
    EXPECT_TRUE(writer.PutMessageA(KeyEnum::kSecondValue, buffer).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  {
    auto writer = db.CreateWriter();
    auto patch = MessageAPatchBuilder().set_data("updated").Build();
    EXPECT_TRUE(writer.UpdateMessageA(KeyEnum::kSecondValue, patch).ok());
    ASSERT_TRUE(writer.Commit().ok());
  }
  {
    auto snapshot = db.CreateSnapshot();
    MessageA message_a;
    EXPECT_TRUE(snapshot.GetMessageA(KeyEnum::kSecondValue, message_a).ok());
    EXPECT_EQ(message_a.data(), "updated");
  }
}

TEST(PrimitiveDbTest, NoChangesWithoutCommit) {
  Db db;
  {
    auto writer = db.CreateWriter();
    MessageABuilder builder;
    builder.set_key(KeyEnum::kUnknown);
    builder.set_data("uncommitted");
    std::vector<uint8_t> buffer = builder.Build();
    EXPECT_TRUE(writer.PutMessageA(KeyEnum::kUnknown, buffer).ok());
    // Do not commit
  }
  {
    auto snapshot = db.CreateSnapshot();
    MessageA message_a;
    // Should not find the uncommitted message
    EXPECT_FALSE(snapshot.GetMessageA(KeyEnum::kUnknown, message_a).ok());
  }
}

TEST(PrimitiveDbTest, NoYetCommittedChangesNotVisibleFromOtherReaders) {
  Db db;
  {
    auto writer = db.CreateWriter();
    MessageABuilder builder;
    builder.set_key(KeyEnum::kFirstValue);
    builder.set_data("notvisible");
    std::vector<uint8_t> buffer = builder.Build();
    EXPECT_TRUE(writer.PutMessageA(KeyEnum::kFirstValue, buffer).ok());
    // Do not commit yet.
    {
      auto snapshot = db.CreateSnapshot();
      MessageA message_a;
      // Should not find the uncommitted message
      EXPECT_FALSE(snapshot.GetMessageA(KeyEnum::kFirstValue, message_a).ok());
    }
    ASSERT_TRUE(writer.Commit().ok());
  }
  {
    auto snapshot = db.CreateSnapshot();
    MessageA message_a;
    // Now it's visible.
    EXPECT_TRUE(snapshot.GetMessageA(KeyEnum::kFirstValue, message_a).ok());
    EXPECT_EQ(message_a.data(), "notvisible");
  }
}

TEST(PrimitiveDbTest, NoYetCommittedChangesVisibleInWriter) {
  Db db;
  {
    auto writer = db.CreateWriter();
    MessageABuilder builder;
    builder.set_key(KeyEnum::kSecondValue);
    builder.set_data("visibleinwriter");
    std::vector<uint8_t> buffer = builder.Build();
    EXPECT_TRUE(writer.PutMessageA(KeyEnum::kSecondValue, buffer).ok());

    MessageA message_a;
    EXPECT_TRUE(writer.GetMessageA(KeyEnum::kSecondValue, message_a).ok());
    EXPECT_EQ(message_a.data(), "visibleinwriter");
  }
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
