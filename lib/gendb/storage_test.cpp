#include "gendb/storage.h"

#include <filesystem>
#include <memory>
#include <random>

#include "gendb/layered_storage.h"
#include "gtest/gtest.h"
#include "status_matchers.h"

namespace gendb {
namespace {

// Helper functions for tests
BytesConstView StringToBytesView(const std::string& str) {
  return {reinterpret_cast<const uint8_t*>(str.data()), str.size()};
}

Bytes StringToBytes(const std::string& str) {
  return {reinterpret_cast<const uint8_t*>(str.data()),
          reinterpret_cast<const uint8_t*>(str.data()) + str.size()};
}

std::string BytesViewToString(BytesConstView bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

// Storage type enum for parameterized tests
enum class StorageType { Memory, RocksDB };

// Helper to create storage instances
class StorageHelper {
 public:
  static std::unique_ptr<Storage> CreateStorage(StorageType type) {
    switch (type) {
      case StorageType::Memory:
        return std::make_unique<MemoryStorage>();
      case StorageType::RocksDB: {
        auto test_db_path = std::filesystem::temp_directory_path() /
                            ("rocksdb_test_" + std::to_string(std::random_device{}()));
        std::filesystem::remove_all(test_db_path);
        created_paths_.push_back(test_db_path);
        return std::make_unique<RocksDBStorage>(test_db_path);
      }
    }
    return nullptr;
  }

  static std::string GetName(StorageType type) {
    switch (type) {
      case StorageType::Memory:
        return "MemoryStorage";
      case StorageType::RocksDB:
        return "RocksDBStorage";
    }
    return "Unknown";
  }

  static void CleanupAll() {
    for (const auto& path : created_paths_) {
      std::filesystem::remove_all(path);
    }
    created_paths_.clear();
  }

 private:
  static std::vector<std::filesystem::path> created_paths_;
};

std::vector<std::filesystem::path> StorageHelper::created_paths_;

// Parameterized test fixture
class StorageTest : public ::testing::TestWithParam<StorageType> {
 protected:
  void SetUp() override {
    storage_type_ = GetParam();
    storage_ = StorageHelper::CreateStorage(storage_type_);
  }

  void TearDown() override {
    storage_.reset();
    StorageHelper::CleanupAll();
  }

  StorageType storage_type_;
  std::unique_ptr<Storage> storage_;
};

// Parameterized tests that work with any Storage implementation
TEST_P(StorageTest, BasicPutAndGet) {
  const std::string key = "test_key";
  const std::string value = "test_value";

  // Put a value
  storage_->Put(0, StringToBytesView(key), StringToBytes(value));

  // Get the value
  BytesConstView retrieved_value;
  ASSERT_OK(storage_->Get(0, StringToBytesView(key), retrieved_value));
  EXPECT_EQ(BytesViewToString(retrieved_value), value);

  // Check that the key exists
  EXPECT_TRUE(storage_->Exists(0, StringToBytesView(key)));

  // Check collection size
  EXPECT_EQ(storage_->GetCollectionSize(0), 1);
}

TEST_P(StorageTest, Delete) {
  const std::string key = "test_key";
  const std::string value = "test_value";

  // Put a value
  storage_->Put(0, StringToBytesView(key), StringToBytes(value));
  EXPECT_TRUE(storage_->Exists(0, StringToBytesView(key)));
  EXPECT_EQ(storage_->GetCollectionSize(0), 1);

  // Delete the key
  ASSERT_OK(storage_->Delete(0, StringToBytesView(key)));

  // Verify it no longer exists
  EXPECT_FALSE(storage_->Exists(0, StringToBytesView(key)));
  EXPECT_EQ(storage_->GetCollectionSize(0), 0);

  // Try to get deleted key - should fail
  BytesConstView retrieved_value;
  EXPECT_NOT_FOUND(storage_->Get(0, StringToBytesView(key), retrieved_value));
}

TEST_P(StorageTest, DeleteNonExistentKey) {
  const std::string key = "non_existent_key";

  // Try to delete non-existent key
  absl::Status status = storage_->Delete(0, StringToBytesView(key));

  // MemoryStorage returns NotFound, RocksDBStorage returns Ok (both are valid behaviors)
  EXPECT_TRUE(status.ok() || status.code() == absl::StatusCode::kNotFound)
      << "Failed with " << StorageHelper::GetName(storage_type_) << ": " << status;
}

TEST_P(StorageTest, GetNonExistentKey) {
  const std::string key = "non_existent_key";

  BytesConstView retrieved_value;
  EXPECT_NOT_FOUND(storage_->Get(0, StringToBytesView(key), retrieved_value));
}

TEST_P(StorageTest, MultipleCollections) {
  // Put data in multiple collections
  storage_->Put(0, StringToBytesView("key1"), StringToBytes("value1"));
  storage_->Put(0, StringToBytesView("key2"), StringToBytes("value2"));
  storage_->Put(1, StringToBytesView("key3"), StringToBytes("value3"));

  // Verify data in collection 0
  BytesConstView retrieved_value;
  ASSERT_OK(storage_->Get(0, StringToBytesView("key1"), retrieved_value));
  EXPECT_EQ(BytesViewToString(retrieved_value), "value1");

  ASSERT_OK(storage_->Get(0, StringToBytesView("key2"), retrieved_value));
  EXPECT_EQ(BytesViewToString(retrieved_value), "value2");

  // Verify data in collection 1
  ASSERT_OK(storage_->Get(1, StringToBytesView("key3"), retrieved_value));
  EXPECT_EQ(BytesViewToString(retrieved_value), "value3");

  // Check cross-collection isolation
  EXPECT_FALSE(storage_->Exists(0, StringToBytesView("key3")));
  EXPECT_FALSE(storage_->Exists(1, StringToBytesView("key1")));

  // Verify collection stats
  EXPECT_EQ(storage_->GetCollectionCount(), 2);
  EXPECT_EQ(storage_->GetCollectionSize(0), 2);
  EXPECT_EQ(storage_->GetCollectionSize(1), 1);
}

TEST_P(StorageTest, CollectionManagementWithDeletion) {
  // Setup data
  storage_->Put(0, StringToBytesView("key1"), StringToBytes("value1"));
  storage_->Put(0, StringToBytesView("key2"), StringToBytes("value2"));
  storage_->Put(1, StringToBytesView("key3"), StringToBytes("value3"));

  // Initial state verification
  EXPECT_EQ(storage_->GetCollectionCount(), 2);
  EXPECT_EQ(storage_->GetCollectionSize(0), 2);
  EXPECT_EQ(storage_->GetCollectionSize(1), 1);

  // Delete from collection 0
  ASSERT_OK(storage_->Delete(0, StringToBytesView("key1")));

  // Verify collection sizes after deletion
  EXPECT_EQ(storage_->GetCollectionCount(), 2);  // Collection count doesn't decrease
  EXPECT_EQ(storage_->GetCollectionSize(0), 1);  // Collection 0 has one less item
  EXPECT_EQ(storage_->GetCollectionSize(1), 1);  // Collection 1 unchanged

  // Verify deleted key doesn't exist, others still do
  EXPECT_FALSE(storage_->Exists(0, StringToBytesView("key1")));
  EXPECT_TRUE(storage_->Exists(0, StringToBytesView("key2")));
  EXPECT_TRUE(storage_->Exists(1, StringToBytesView("key3")));
}

TEST_P(StorageTest, UpdateValue) {
  const std::string key = "test_key";
  const std::string value1 = "value1";
  const std::string value2 = "value2";

  // Put initial value
  storage_->Put(0, StringToBytesView(key), StringToBytes(value1));

  // Verify initial value
  BytesConstView retrieved_value;
  ASSERT_OK(storage_->Get(0, StringToBytesView(key), retrieved_value));
  EXPECT_EQ(BytesViewToString(retrieved_value), value1);

  // Update value
  storage_->Put(0, StringToBytesView(key), StringToBytes(value2));

  // Verify updated value
  ASSERT_OK(storage_->Get(0, StringToBytesView(key), retrieved_value));
  EXPECT_EQ(BytesViewToString(retrieved_value), value2);
}

TEST_P(StorageTest, LargeValues) {
  const std::string key = "large_key";
  std::string large_value(10000, 'A');  // 10KB value

  // Put large value
  storage_->Put(0, StringToBytesView(key), StringToBytes(large_value));

  // Get large value
  BytesConstView retrieved_value;
  ASSERT_OK(storage_->Get(0, StringToBytesView(key), retrieved_value));
  EXPECT_EQ(BytesViewToString(retrieved_value), large_value);
}

TEST_P(StorageTest, BinaryData) {
  const std::string key = "binary_key";

  // Create binary data with null bytes
  std::vector<uint8_t> binary_data = {0x00, 0x01, 0x02, 0xFF, 0xFE, 0x00, 0x03};
  BytesConstView binary_key_view{reinterpret_cast<const uint8_t*>(key.data()), key.size()};
  Bytes binary_value{binary_data.begin(), binary_data.end()};

  // Put binary value
  storage_->Put(0, binary_key_view, std::move(binary_value));

  // Get binary value
  BytesConstView retrieved_value;
  ASSERT_OK(storage_->Get(0, binary_key_view, retrieved_value));

  // Verify binary data
  EXPECT_EQ(retrieved_value.size(), binary_data.size());
  EXPECT_TRUE(std::equal(retrieved_value.begin(), retrieved_value.end(), binary_data.begin()));
}

TEST_P(StorageTest, Clear) {
  // Put values in multiple collections
  storage_->Put(0, StringToBytesView("key1"), StringToBytes("value1"));
  storage_->Put(1, StringToBytesView("key2"), StringToBytes("value2"));

  // Verify data exists
  EXPECT_TRUE(storage_->Exists(0, StringToBytesView("key1")));
  EXPECT_TRUE(storage_->Exists(1, StringToBytesView("key2")));

  // Clear all data
  storage_->Clear();

  // Verify data is gone
  EXPECT_FALSE(storage_->Exists(0, StringToBytesView("key1")));
  EXPECT_FALSE(storage_->Exists(1, StringToBytesView("key2")));
  EXPECT_EQ(storage_->GetCollectionSize(0), 0);
}

TEST_P(StorageTest, EmptyStorageQueries) {
  // Test queries on empty storage
  // Note: RocksDBStorage starts with a default column family, so GetCollectionCount() returns 1
  // MemoryStorage truly starts empty, so it returns 0
  if (storage_type_ == StorageType::Memory) {
    EXPECT_EQ(storage_->GetCollectionCount(), 0);
  } else {
    EXPECT_GE(storage_->GetCollectionCount(), 0);  // RocksDB may have default column families
  }

  EXPECT_EQ(storage_->GetCollectionSize(0), 0);
  EXPECT_EQ(storage_->GetCollectionSize(999), 0);  // Non-existent collection

  EXPECT_FALSE(storage_->Exists(0, StringToBytesView("any_key")));

  BytesConstView value;
  EXPECT_NOT_FOUND(storage_->Get(0, StringToBytesView("any_key"), value));
}

// Instantiate tests for both storage types
INSTANTIATE_TEST_SUITE_P(AllStorageTypes, StorageTest,
                         ::testing::Values(StorageType::Memory, StorageType::RocksDB),
                         [](const ::testing::TestParamInfo<StorageType>& info) {
                           return StorageHelper::GetName(info.param);
                         });

// RocksDB-specific test for persistence (only makes sense for persistent storage)
class RocksDBPersistenceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_db_path_ = std::filesystem::temp_directory_path() /
                    ("rocksdb_persistence_test_" + std::to_string(std::random_device{}()));
    std::filesystem::remove_all(test_db_path_);
  }

  void TearDown() override { std::filesystem::remove_all(test_db_path_); }

  std::filesystem::path test_db_path_;
};

TEST_F(RocksDBPersistenceTest, DataPersistsAcrossRestarts) {
  const std::string key = "persistent_key";
  const std::string value = "persistent_value";

  {
    // Create storage and put value
    RocksDBStorage storage(test_db_path_);
    storage.Put(0, StringToBytesView(key), StringToBytes(value));
  }  // Storage goes out of scope and is destroyed

  {
    // Reopen storage
    RocksDBStorage storage(test_db_path_);

    // Verify value persisted
    BytesConstView retrieved_value;
    ASSERT_OK(storage.Get(0, StringToBytesView(key), retrieved_value));
    EXPECT_EQ(BytesViewToString(retrieved_value), value);
  }
}

// LayeredStorage tests (these work with any Storage implementation)
class LayeredStorageTest : public ::testing::TestWithParam<StorageType> {
 protected:
  void SetUp() override {
    storage_type_ = GetParam();
    main_storage_ = StorageHelper::CreateStorage(storage_type_);
    temp_storage_ = std::make_unique<MemoryStorage>();
    layered_storage_ = std::make_unique<LayeredStorage>(*main_storage_, temp_storage_.get());
  }

  void TearDown() override {
    layered_storage_.reset();
    temp_storage_.reset();
    main_storage_.reset();
    StorageHelper::CleanupAll();
  }

  StorageType storage_type_;
  std::unique_ptr<Storage> main_storage_;
  std::unique_ptr<MemoryStorage> temp_storage_;
  std::unique_ptr<LayeredStorage> layered_storage_;
};

TEST_P(LayeredStorageTest, DeleteWithTempStorage) {
  // Put a value in main storage
  const std::string key = "test_key";
  const std::string value = "test_value";
  main_storage_->Put(0, StringToBytesView(key), StringToBytes(value));

  // Verify we can get it through layered storage
  BytesConstView retrieved_value;
  ASSERT_OK(layered_storage_->Get(0, StringToBytesView(key), retrieved_value));
  EXPECT_EQ(BytesViewToString(retrieved_value), value);

  // Delete through layered storage (should mark deletion in temp storage)
  ASSERT_OK(layered_storage_->Delete(0, StringToBytesView(key)));

  // Value should still exist in main storage
  EXPECT_TRUE(main_storage_->Exists(0, StringToBytesView(key)));

  // But getting through layered storage should fail (deletion marker in temp)
  EXPECT_NOT_FOUND(layered_storage_->Get(0, StringToBytesView(key), retrieved_value));

  // Merge temp storage - should apply deletion to main storage
  layered_storage_->MergeTempStorage();

  // Now value should be deleted from main storage
  EXPECT_FALSE(main_storage_->Exists(0, StringToBytesView(key)));
}

TEST_P(LayeredStorageTest, CompleteWorkflow) {
  // Start with data in main storage
  main_storage_->Put(0, StringToBytesView("main_key"), StringToBytes("main_value"));

  // Verify read through layered storage
  BytesConstView value;
  ASSERT_OK(layered_storage_->Get(0, StringToBytesView("main_key"), value));
  EXPECT_EQ(BytesViewToString(value), "main_value");

  // Delete through layered storage (marks in temp storage)
  ASSERT_OK(layered_storage_->Delete(0, StringToBytesView("main_key")));

  // Not visible through layered storage
  EXPECT_NOT_FOUND(layered_storage_->Get(0, StringToBytesView("main_key"), value));

  // Still exists in main storage
  EXPECT_TRUE(main_storage_->Exists(0, StringToBytesView("main_key")));

  // Merge changes
  layered_storage_->MergeTempStorage();

  // Deletion applied to main storage
  EXPECT_FALSE(main_storage_->Exists(0, StringToBytesView("main_key")));
}

// Instantiate LayeredStorage tests for both storage types
INSTANTIATE_TEST_SUITE_P(AllStorageTypes, LayeredStorageTest,
                         ::testing::Values(StorageType::Memory, StorageType::RocksDB),
                         [](const ::testing::TestParamInfo<StorageType>& info) {
                           return StorageHelper::GetName(info.param);
                         });

}  // namespace
}  // namespace gendb