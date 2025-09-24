#include "gendb/storage.h"

#include "gendb/layered_storage.h"
#include "gtest/gtest.h"

namespace gendb {
namespace {

// Helper function to create BytesConstView from string
BytesConstView StringToBytesView(const std::string& str) {
  return {reinterpret_cast<const uint8_t*>(str.data()), str.size()};
}

// Helper function to create Bytes from string
Bytes StringToBytes(const std::string& str) {
  return {reinterpret_cast<const uint8_t*>(str.data()),
          reinterpret_cast<const uint8_t*>(str.data()) + str.size()};
}

// Helper function to convert BytesConstView to string for comparison
std::string BytesViewToString(BytesConstView bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

TEST(StorageTest, MemoryStoragePutAndGet) {
  MemoryStorage storage;

  // Put a value
  const std::string key = "test_key";
  const std::string value = "test_value";
  storage.Put(0, StringToBytesView(key), StringToBytes(value));

  // Get the value
  BytesConstView retrieved_value;
  absl::Status status = storage.Get(0, StringToBytesView(key), retrieved_value);
  ASSERT_TRUE(status.ok()) << status;
  EXPECT_EQ(BytesViewToString(retrieved_value), value);

  // Check that the key exists
  EXPECT_TRUE(storage.Exists(0, StringToBytesView(key)));

  // Check collection size
  EXPECT_EQ(storage.GetCollectionSize(0), 1);
}

TEST(StorageTest, MemoryStorageDelete) {
  MemoryStorage storage;

  // Put a value
  const std::string key = "test_key";
  const std::string value = "test_value";
  storage.Put(0, StringToBytesView(key), StringToBytes(value));

  // Verify it exists
  EXPECT_TRUE(storage.Exists(0, StringToBytesView(key)));
  EXPECT_EQ(storage.GetCollectionSize(0), 1);

  // Delete the key
  absl::Status status = storage.Delete(0, StringToBytesView(key));
  ASSERT_TRUE(status.ok()) << status;

  // Verify it no longer exists
  EXPECT_FALSE(storage.Exists(0, StringToBytesView(key)));
  EXPECT_EQ(storage.GetCollectionSize(0), 0);

  // Try to get deleted key - should fail
  BytesConstView retrieved_value;
  status = storage.Get(0, StringToBytesView(key), retrieved_value);
  EXPECT_EQ(status.code(), absl::StatusCode::kNotFound);
}

TEST(StorageTest, MemoryStorageDeleteNonExistentKey) {
  MemoryStorage storage;

  const std::string key = "non_existent_key";

  // Try to delete non-existent key - should fail
  absl::Status status = storage.Delete(0, StringToBytesView(key));
  EXPECT_EQ(status.code(), absl::StatusCode::kNotFound);
}

TEST(LayeredStorageTest, DeleteWithoutTempStorage) {
  MemoryStorage main_storage;
  LayeredStorage layered_storage(main_storage, nullptr);

  // Put a value in main storage
  const std::string key = "test_key";
  const std::string value = "test_value";
  main_storage.Put(0, StringToBytesView(key), StringToBytes(value));

  // Verify we can get it through layered storage
  BytesConstView retrieved_value;
  absl::Status status = layered_storage.Get(0, StringToBytesView(key), retrieved_value);
  ASSERT_TRUE(status.ok()) << status;
  EXPECT_EQ(BytesViewToString(retrieved_value), value);

  // Delete through layered storage
  status = layered_storage.Delete(0, StringToBytesView(key));
  ASSERT_TRUE(status.ok()) << status;

  // Verify it's deleted from main storage
  EXPECT_FALSE(main_storage.Exists(0, StringToBytesView(key)));
}

TEST(LayeredStorageTest, DeleteWithTempStorage) {
  MemoryStorage main_storage;
  MemoryStorage temp_storage;
  LayeredStorage layered_storage(main_storage, &temp_storage);

  // Put a value in main storage
  const std::string key = "test_key";
  const std::string value = "test_value";
  main_storage.Put(0, StringToBytesView(key), StringToBytes(value));

  // Verify we can get it through layered storage
  BytesConstView retrieved_value;
  absl::Status status = layered_storage.Get(0, StringToBytesView(key), retrieved_value);
  ASSERT_TRUE(status.ok()) << status;
  EXPECT_EQ(BytesViewToString(retrieved_value), value);

  // Delete through layered storage (should mark deletion in temp storage)
  status = layered_storage.Delete(0, StringToBytesView(key));
  ASSERT_TRUE(status.ok()) << status;

  // Value should still exist in main storage
  EXPECT_TRUE(main_storage.Exists(0, StringToBytesView(key)));

  // But getting through layered storage should fail (deletion marker in temp)
  status = layered_storage.Get(0, StringToBytesView(key), retrieved_value);
  EXPECT_EQ(status.code(), absl::StatusCode::kNotFound);

  // Merge temp storage - should apply deletion to main storage
  layered_storage.MergeTempStorage();

  // Now value should be deleted from main storage
  EXPECT_FALSE(main_storage.Exists(0, StringToBytesView(key)));
}

TEST(LayeredStorageTest, DeleteAndRecreateWithTempStorage) {
  MemoryStorage main_storage;
  MemoryStorage temp_storage;
  LayeredStorage layered_storage(main_storage, &temp_storage);

  // Put a value in main storage
  const std::string key = "test_key";
  const std::string value = "test_value";
  main_storage.Put(0, StringToBytesView(key), StringToBytes(value));

  // Delete through layered storage
  absl::Status status = layered_storage.Delete(0, StringToBytesView(key));
  ASSERT_TRUE(status.ok()) << status;

  // Verify deletion
  BytesConstView retrieved_value;
  status = layered_storage.Get(0, StringToBytesView(key), retrieved_value);
  EXPECT_EQ(status.code(), absl::StatusCode::kNotFound);

  // Put a new value with same key in temp storage
  const std::string new_value = "new_test_value";
  temp_storage.Put(0, StringToBytesView(key), StringToBytes(new_value));

  // Should get the new value through layered storage
  status = layered_storage.Get(0, StringToBytesView(key), retrieved_value);
  ASSERT_TRUE(status.ok()) << status;
  EXPECT_EQ(BytesViewToString(retrieved_value), new_value);
}

// Tests based on demo scenarios to ensure comprehensive coverage

TEST(StorageTest, MultipleCollections) {
  MemoryStorage storage;

  // Put data in multiple collections (like in demo)
  storage.Put(0, StringToBytesView("key1"), StringToBytes("value1"));
  storage.Put(0, StringToBytesView("key2"), StringToBytes("value2"));
  storage.Put(1, StringToBytesView("key3"), StringToBytes("value3"));

  // Verify data in collection 0
  BytesConstView retrieved_value;
  absl::Status status = storage.Get(0, StringToBytesView("key1"), retrieved_value);
  ASSERT_TRUE(status.ok()) << status;
  EXPECT_EQ(BytesViewToString(retrieved_value), "value1");

  status = storage.Get(0, StringToBytesView("key2"), retrieved_value);
  ASSERT_TRUE(status.ok()) << status;
  EXPECT_EQ(BytesViewToString(retrieved_value), "value2");

  // Verify data in collection 1
  status = storage.Get(1, StringToBytesView("key3"), retrieved_value);
  ASSERT_TRUE(status.ok()) << status;
  EXPECT_EQ(BytesViewToString(retrieved_value), "value3");

  // Check existence across collections
  EXPECT_TRUE(storage.Exists(0, StringToBytesView("key1")));
  EXPECT_TRUE(storage.Exists(0, StringToBytesView("key2")));
  EXPECT_TRUE(storage.Exists(1, StringToBytesView("key3")));

  // Check cross-collection isolation
  EXPECT_FALSE(storage.Exists(0, StringToBytesView("key3")));
  EXPECT_FALSE(storage.Exists(1, StringToBytesView("key1")));

  // Verify collection stats (like in demo)
  EXPECT_EQ(storage.GetCollectionCount(), 2);
  EXPECT_EQ(storage.GetCollectionSize(0), 2);
  EXPECT_EQ(storage.GetCollectionSize(1), 1);
}

TEST(StorageTest, CollectionManagementWithDeletion) {
  MemoryStorage storage;

  // Setup like demo
  storage.Put(0, StringToBytesView("key1"), StringToBytes("value1"));
  storage.Put(0, StringToBytesView("key2"), StringToBytes("value2"));
  storage.Put(1, StringToBytesView("key3"), StringToBytes("value3"));

  // Initial state verification
  EXPECT_EQ(storage.GetCollectionCount(), 2);
  EXPECT_EQ(storage.GetCollectionSize(0), 2);
  EXPECT_EQ(storage.GetCollectionSize(1), 1);

  // Delete from collection 0 (like demo)
  absl::Status status = storage.Delete(0, StringToBytesView("key1"));
  ASSERT_TRUE(status.ok()) << status;

  // Verify collection sizes after deletion
  EXPECT_EQ(storage.GetCollectionCount(), 2);  // Collection count doesn't decrease
  EXPECT_EQ(storage.GetCollectionSize(0), 1);  // Collection 0 has one less item
  EXPECT_EQ(storage.GetCollectionSize(1), 1);  // Collection 1 unchanged

  // Verify deleted key doesn't exist, others still do
  EXPECT_FALSE(storage.Exists(0, StringToBytesView("key1")));
  EXPECT_TRUE(storage.Exists(0, StringToBytesView("key2")));
  EXPECT_TRUE(storage.Exists(1, StringToBytesView("key3")));
}

TEST(StorageTest, StoragePolymorphism) {
  // This test ensures both MemoryStorage and any other Storage implementation
  // behave identically through the Storage interface (like the demo shows)

  auto test_storage_behavior = [](std::unique_ptr<Storage> storage) {
    // Basic operations
    storage->Put(0, StringToBytesView("key1"), StringToBytes("value1"));

    BytesConstView retrieved_value;
    absl::Status status = storage->Get(0, StringToBytesView("key1"), retrieved_value);
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(BytesViewToString(retrieved_value), "value1");

    EXPECT_TRUE(storage->Exists(0, StringToBytesView("key1")));
    EXPECT_EQ(storage->GetCollectionSize(0), 1);

    // Delete operation
    status = storage->Delete(0, StringToBytesView("key1"));
    EXPECT_TRUE(status.ok());
    EXPECT_FALSE(storage->Exists(0, StringToBytesView("key1")));
    EXPECT_EQ(storage->GetCollectionSize(0), 0);
  };

  // Test with MemoryStorage
  test_storage_behavior(std::make_unique<MemoryStorage>());

  // If RocksDBStorage is available, test it too (commented out to avoid file system dependencies)
  // test_storage_behavior(std::make_unique<RocksDBStorage>("/tmp/test_polymorphism"));
}

TEST(LayeredStorageTest, CompleteWorkflow) {
  // Test the complete layered storage workflow demonstrated in the demo
  MemoryStorage main_storage;
  MemoryStorage temp_storage;
  LayeredStorage layered_storage(main_storage, &temp_storage);

  // 1. Start with data in main storage
  main_storage.Put(0, StringToBytesView("main_key"), StringToBytes("main_value"));

  // 2. Verify read through layered storage
  BytesConstView value;
  absl::Status status = layered_storage.Get(0, StringToBytesView("main_key"), value);
  ASSERT_TRUE(status.ok()) << status;
  EXPECT_EQ(BytesViewToString(value), "main_value");

  // 3. Delete through layered storage (marks in temp storage)
  status = layered_storage.Delete(0, StringToBytesView("main_key"));
  ASSERT_TRUE(status.ok()) << status;

  // 4. Verify deletion semantics
  // - Not visible through layered storage
  status = layered_storage.Get(0, StringToBytesView("main_key"), value);
  EXPECT_EQ(status.code(), absl::StatusCode::kNotFound);

  // - Still exists in main storage
  EXPECT_TRUE(main_storage.Exists(0, StringToBytesView("main_key")));

  // 5. Merge changes
  layered_storage.MergeTempStorage();

  // 6. Verify final state - deletion applied to main storage
  EXPECT_FALSE(main_storage.Exists(0, StringToBytesView("main_key")));
}

TEST(StorageTest, EmptyStorageQueries) {
  MemoryStorage storage;

  // Test queries on empty storage (edge cases that demo didn't cover but are important)
  EXPECT_EQ(storage.GetCollectionCount(), 0);
  EXPECT_EQ(storage.GetCollectionSize(0), 0);
  EXPECT_EQ(storage.GetCollectionSize(999), 0);  // Non-existent collection

  EXPECT_FALSE(storage.Exists(0, StringToBytesView("any_key")));

  BytesConstView value;
  absl::Status status = storage.Get(0, StringToBytesView("any_key"), value);
  EXPECT_EQ(status.code(), absl::StatusCode::kNotFound);
}

TEST(StorageTest, ClearOperation) {
  MemoryStorage storage;

  // Add data across multiple collections
  storage.Put(0, StringToBytesView("key1"), StringToBytes("value1"));
  storage.Put(0, StringToBytesView("key2"), StringToBytes("value2"));
  storage.Put(1, StringToBytesView("key3"), StringToBytes("value3"));

  // Verify data exists
  EXPECT_EQ(storage.GetCollectionCount(), 2);
  EXPECT_EQ(storage.GetCollectionSize(0), 2);
  EXPECT_EQ(storage.GetCollectionSize(1), 1);
  EXPECT_TRUE(storage.Exists(0, StringToBytesView("key1")));
  EXPECT_TRUE(storage.Exists(1, StringToBytesView("key3")));

  // Clear all data
  storage.Clear();

  // Verify everything is gone
  EXPECT_EQ(storage.GetCollectionCount(), 0);
  EXPECT_EQ(storage.GetCollectionSize(0), 0);
  EXPECT_EQ(storage.GetCollectionSize(1), 0);
  EXPECT_FALSE(storage.Exists(0, StringToBytesView("key1")));
  EXPECT_FALSE(storage.Exists(1, StringToBytesView("key3")));

  BytesConstView value;
  absl::Status status = storage.Get(0, StringToBytesView("key1"), value);
  EXPECT_EQ(status.code(), absl::StatusCode::kNotFound);
}

}  // namespace
}  // namespace gendb