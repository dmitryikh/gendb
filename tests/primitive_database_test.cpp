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
    writer.Commit();
  }
  {
    auto guard = db.SharedLock();
    MessageA message_a;
    EXPECT_TRUE(guard.GetMessageA(KeyEnum::kFirstValue, message_a).ok());
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
    writer.Commit();
  }
  {
    auto writer = db.CreateWriter();
    auto patch = MessageAPatchBuilder().set_data("updated").Build();
    EXPECT_TRUE(writer.UpdateMessageA(KeyEnum::kSecondValue, patch).ok());
    writer.Commit();
  }
  {
    auto guard = db.SharedLock();
    MessageA message_a;
    EXPECT_TRUE(guard.GetMessageA(KeyEnum::kSecondValue, message_a).ok());
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
    auto guard = db.SharedLock();
    MessageA message_a;
    // Should not find the uncommitted message
    EXPECT_FALSE(guard.GetMessageA(KeyEnum::kUnknown, message_a).ok());
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
      auto guard = db.SharedLock();
      MessageA message_a;
      // Should not find the uncommitted message
      EXPECT_FALSE(guard.GetMessageA(KeyEnum::kFirstValue, message_a).ok());
    }
    writer.Commit();
  }
  {
    auto guard = db.SharedLock();
    MessageA message_a;
    // Now it's visible.
    EXPECT_TRUE(guard.GetMessageA(KeyEnum::kFirstValue, message_a).ok());
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
