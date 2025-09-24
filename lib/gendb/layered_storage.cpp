#include "gendb/layered_storage.h"

namespace gendb {

absl::Status LayeredStorage::Get(const size_t collection_id, BytesConstView key,
                                 BytesConstView& value) const {
  // First check temp storage if available
  if (_temp_storage_ptr != nullptr) {
    if (_temp_storage_ptr->Exists(collection_id, key)) {
      absl::Status status = _temp_storage_ptr->Get(collection_id, key, value);
      if (status.ok()) {
        // Check if it's a deletion marker (empty value)
        if (value.empty()) {
          return absl::NotFoundError("Key not found");
        }
        return absl::OkStatus();
      }
    }
  }

  // Check main storage
  return _storage.Get(collection_id, key, value);
}

absl::Status LayeredStorage::Delete(const size_t collection_id, BytesConstView key) {
  if (_temp_storage_ptr != nullptr) {
    // Mark deletion in temp storage with empty value
    _temp_storage_ptr->Put(collection_id, key, Bytes{});
    return absl::OkStatus();
  } else {
    // Delete directly from main storage
    return _storage.Delete(collection_id, key);
  }
}

absl::Status LayeredStorage::EnsureInTempStorage(const size_t collection_id, BytesConstView key,
                                                 Bytes** value) {
  if (_temp_storage_ptr == nullptr) {
    return absl::NotFoundError("No temp storage");
  }

  // Check if key already exists in temp storage
  if (_temp_storage_ptr->Exists(collection_id, key)) {
    // We need to get the actual storage implementation to access collections directly
    // This is a limitation of the current design - we need direct access to modify values
    auto* memory_storage = dynamic_cast<MemoryStorage*>(_temp_storage_ptr);
    if (memory_storage == nullptr) {
      return absl::InternalError("Temp storage must be MemoryStorage for EnsureInTempStorage");
    }

    if (collection_id >= memory_storage->collections.size()) {
      memory_storage->collections.resize(collection_id + 1);
    }
    auto& temp_coll = memory_storage->collections[collection_id];
    auto it = temp_coll.find(Bytes{key.begin(), key.end()});
    if (it != temp_coll.end()) {
      *value = &it->second;
      return absl::OkStatus();
    }
  }

  // Key doesn't exist in temp storage, try to copy from main storage
  BytesConstView main_value;
  absl::Status status = _storage.Get(collection_id, key, main_value);
  if (!status.ok()) {
    return status;
  }

  // Copy to temp storage
  Bytes copied_value{main_value.begin(), main_value.end()};
  _temp_storage_ptr->Put(collection_id, key, std::move(copied_value));

  // Get reference to the stored value
  auto* memory_storage = dynamic_cast<MemoryStorage*>(_temp_storage_ptr);
  if (memory_storage == nullptr) {
    return absl::InternalError("Temp storage must be MemoryStorage for EnsureInTempStorage");
  }

  auto& temp_coll = memory_storage->collections[collection_id];
  auto it = temp_coll.find(Bytes{key.begin(), key.end()});
  if (it == temp_coll.end()) {
    return absl::InternalError("Failed to store value in temp storage");
  }

  *value = &it->second;
  return absl::OkStatus();
}

void LayeredStorage::MergeTempStorage() {
  if (_temp_storage_ptr == nullptr) {
    return;
  }

  // We need direct access to iterate over collections
  auto* memory_storage = dynamic_cast<MemoryStorage*>(_temp_storage_ptr);
  if (memory_storage == nullptr) {
    return;  // Can't merge if not MemoryStorage
  }

  for (size_t i = 0; i < memory_storage->collections.size(); ++i) {
    auto& temp_coll = memory_storage->collections[i];
    for (auto& [key, value] : temp_coll) {
      if (value.empty()) {
        // Empty value means deletion - ignore result as this is best effort
        (void)_storage.Delete(i, key);
      } else {
        _storage.Put(i, key, std::move(value));
      }
    }
    temp_coll.clear();
  }
}

}  // namespace gendb