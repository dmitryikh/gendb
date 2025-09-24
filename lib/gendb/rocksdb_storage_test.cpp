#include <filesystem>
#include <memory>
#include <random>

#include "gendb/layered_storage.h"
#include "gendb/storage.h"
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

// Fixture for RocksDB tests
class RocksDBStorageTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create a unique temp directory for each test
    test_db_path_ = std::filesystem::temp_directory_path() /
                    ("rocksdb_test_" + std::to_string(std::random_device{}()));

    // Ensure the directory doesn't exist
    std::filesystem::remove_all(test_db_path_);

    storage_ = std::make_unique<RocksDBStorage>(test_db_path_);
  }

  void TearDown() override {
    // Close the database and clean up
    storage_.reset();
    std::filesystem::remove_all(test_db_path_);
  }

  std::filesystem::path test_db_path_;
  std::unique_ptr<RocksDBStorage> storage_;
};

TEST_F(RocksDBStorageTest, BasicPutAndGet) {
  const std::string key = "test_key";
  const std::string value = "test_value";

  // Put a value
  storage_->Put(0, StringToBytesView(key), StringToBytes(value));

  // Get the value
  BytesConstView retrieved_value;
  absl::Status status = storage_->Get(0, StringToBytesView(key), retrieved_value);
  ASSERT_TRUE(status.ok()) << status;
  EXPECT_EQ(BytesViewToString(retrieved_value), value);
}

TEST_F(RocksDBStorageTest, MultipleCollections) {
  const std::string key1 = "key1";
  const std::string key2 = "key2";
  const std::string value1 = "value1";
  const std::string value2 = "value2";

  // Put values in different collections
  storage_->Put(0, StringToBytesView(key1), StringToBytes(value1));
  storage_->Put(1, StringToBytesView(key2), StringToBytes(value2));

  // Verify both values can be retrieved
  BytesConstView retrieved_value1, retrieved_value2;

  absl::Status status1 = storage_->Get(0, StringToBytesView(key1), retrieved_value1);
  ASSERT_TRUE(status1.ok()) << status1;
  EXPECT_EQ(BytesViewToString(retrieved_value1), value1);

  absl::Status status2 = storage_->Get(1, StringToBytesView(key2), retrieved_value2);
  ASSERT_TRUE(status2.ok()) << status2;
  EXPECT_EQ(BytesViewToString(retrieved_value2), value2);

  // Verify collection count
  EXPECT_EQ(storage_->GetCollectionCount(), 2);
}

TEST_F(RocksDBStorageTest, KeyExists) {
  const std::string key = "test_key";
  const std::string value = "test_value";

  // Key should not exist initially
  EXPECT_FALSE(storage_->Exists(0, StringToBytesView(key)));

  // Put a value
  storage_->Put(0, StringToBytesView(key), StringToBytes(value));

  // Now key should exist
  EXPECT_TRUE(storage_->Exists(0, StringToBytesView(key)));
}

TEST_F(RocksDBStorageTest, Delete) {
  const std::string key = "test_key";
  const std::string value = "test_value";

  // Put a value
  storage_->Put(0, StringToBytesView(key), StringToBytes(value));
  EXPECT_TRUE(storage_->Exists(0, StringToBytesView(key)));

  // Delete the key
  absl::Status status = storage_->Delete(0, StringToBytesView(key));
  ASSERT_TRUE(status.ok()) << status;

  // Verify it no longer exists
  EXPECT_FALSE(storage_->Exists(0, StringToBytesView(key)));

  // Try to get deleted key - should fail
  BytesConstView retrieved_value;
  status = storage_->Get(0, StringToBytesView(key), retrieved_value);
  EXPECT_EQ(status.code(), absl::StatusCode::kNotFound);
}

TEST_F(RocksDBStorageTest, DeleteNonExistentKey) {
  const std::string key = "non_existent_key";

  // Try to delete non-existent key - should succeed (RocksDB behavior)
  absl::Status status = storage_->Delete(0, StringToBytesView(key));
  EXPECT_TRUE(status.ok());
}

TEST_F(RocksDBStorageTest, GetNonExistentKey) {
  const std::string key = "non_existent_key";

  BytesConstView retrieved_value;
  absl::Status status = storage_->Get(0, StringToBytesView(key), retrieved_value);
  EXPECT_EQ(status.code(), absl::StatusCode::kNotFound);
}

TEST_F(RocksDBStorageTest, CollectionSize) {
  const std::string key1 = "key1";
  const std::string key2 = "key2";
  const std::string value1 = "value1";
  const std::string value2 = "value2";

  // Initially collection should be empty
  EXPECT_EQ(storage_->GetCollectionSize(0), 0);

  // Add one key
  storage_->Put(0, StringToBytesView(key1), StringToBytes(value1));
  EXPECT_EQ(storage_->GetCollectionSize(0), 1);

  // Add another key
  storage_->Put(0, StringToBytesView(key2), StringToBytes(value2));
  EXPECT_EQ(storage_->GetCollectionSize(0), 2);

  // Delete one key
  absl::Status delete_status = storage_->Delete(0, StringToBytesView(key1));
  EXPECT_TRUE(delete_status.ok());
  EXPECT_EQ(storage_->GetCollectionSize(0), 1);
}

TEST_F(RocksDBStorageTest, UpdateValue) {
  const std::string key = "test_key";
  const std::string value1 = "value1";
  const std::string value2 = "value2";

  // Put initial value
  storage_->Put(0, StringToBytesView(key), StringToBytes(value1));

  // Verify initial value
  BytesConstView retrieved_value;
  absl::Status status = storage_->Get(0, StringToBytesView(key), retrieved_value);
  ASSERT_TRUE(status.ok()) << status;
  EXPECT_EQ(BytesViewToString(retrieved_value), value1);

  // Update value
  storage_->Put(0, StringToBytesView(key), StringToBytes(value2));

  // Verify updated value
  status = storage_->Get(0, StringToBytesView(key), retrieved_value);
  ASSERT_TRUE(status.ok()) << status;
  EXPECT_EQ(BytesViewToString(retrieved_value), value2);
}

TEST_F(RocksDBStorageTest, LargeValues) {
  const std::string key = "large_key";
  std::string large_value(10000, 'A');  // 10KB value

  // Put large value
  storage_->Put(0, StringToBytesView(key), StringToBytes(large_value));

  // Get large value
  BytesConstView retrieved_value;
  absl::Status status = storage_->Get(0, StringToBytesView(key), retrieved_value);
  ASSERT_TRUE(status.ok()) << status;
  EXPECT_EQ(BytesViewToString(retrieved_value), large_value);
}

TEST_F(RocksDBStorageTest, BinaryData) {
  const std::string key = "binary_key";

  // Create binary data with null bytes
  std::vector<uint8_t> binary_data = {0x00, 0x01, 0x02, 0xFF, 0xFE, 0x00, 0x03};
  BytesConstView binary_key_view{reinterpret_cast<const uint8_t*>(key.data()), key.size()};
  Bytes binary_value{binary_data.begin(), binary_data.end()};

  // Put binary value
  storage_->Put(0, binary_key_view, std::move(binary_value));

  // Get binary value
  BytesConstView retrieved_value;
  absl::Status status = storage_->Get(0, binary_key_view, retrieved_value);
  ASSERT_TRUE(status.ok()) << status;

  // Verify binary data
  EXPECT_EQ(retrieved_value.size(), binary_data.size());
  EXPECT_TRUE(std::equal(retrieved_value.begin(), retrieved_value.end(), binary_data.begin()));
}

TEST_F(RocksDBStorageTest, Persistence) {
  const std::string key = "persistent_key";
  const std::string value = "persistent_value";

  // Put value and close storage
  storage_->Put(0, StringToBytesView(key), StringToBytes(value));
  storage_.reset();

  // Reopen storage
  storage_ = std::make_unique<RocksDBStorage>(test_db_path_);

  // Verify value persisted
  BytesConstView retrieved_value;
  absl::Status status = storage_->Get(0, StringToBytesView(key), retrieved_value);
  ASSERT_TRUE(status.ok()) << status;
  EXPECT_EQ(BytesViewToString(retrieved_value), value);
}

TEST_F(RocksDBStorageTest, Clear) {
  const std::string key1 = "key1";
  const std::string key2 = "key2";
  const std::string value1 = "value1";
  const std::string value2 = "value2";

  // Put values in multiple collections
  storage_->Put(0, StringToBytesView(key1), StringToBytes(value1));
  storage_->Put(1, StringToBytesView(key2), StringToBytes(value2));

  // Verify data exists
  EXPECT_TRUE(storage_->Exists(0, StringToBytesView(key1)));
  EXPECT_TRUE(storage_->Exists(1, StringToBytesView(key2)));

  // Clear all data
  storage_->Clear();

  // Verify data is gone
  EXPECT_FALSE(storage_->Exists(0, StringToBytesView(key1)));
  EXPECT_FALSE(storage_->Exists(1, StringToBytesView(key2)));
  EXPECT_EQ(storage_->GetCollectionSize(0), 0);
}

// Test RocksDBStorage with LayeredStorage
TEST_F(RocksDBStorageTest, LayeredStorageIntegration) {
  MemoryStorage temp_storage;
  LayeredStorage layered_storage(*storage_, &temp_storage);

  const std::string key = "layered_key";
  const std::string value = "layered_value";

  // Put value in main storage
  storage_->Put(0, StringToBytesView(key), StringToBytes(value));

  // Get through layered storage
  BytesConstView retrieved_value;
  absl::Status status = layered_storage.Get(0, StringToBytesView(key), retrieved_value);
  ASSERT_TRUE(status.ok()) << status;
  EXPECT_EQ(BytesViewToString(retrieved_value), value);

  // Delete through layered storage (should mark in temp storage)
  status = layered_storage.Delete(0, StringToBytesView(key));
  ASSERT_TRUE(status.ok()) << status;

  // Should not be visible through layered storage
  status = layered_storage.Get(0, StringToBytesView(key), retrieved_value);
  EXPECT_EQ(status.code(), absl::StatusCode::kNotFound);

  // But should still exist in main storage
  EXPECT_TRUE(storage_->Exists(0, StringToBytesView(key)));

  // Merge should apply deletion to main storage
  layered_storage.MergeTempStorage();
  EXPECT_FALSE(storage_->Exists(0, StringToBytesView(key)));
}

}  // namespace
}  // namespace gendb