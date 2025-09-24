#pragma once

#include "absl/status/status.h"
#include "gendb/storage.h"

namespace gendb {
// Non owning view of the storages and operations for moving data between the layers.
class LayeredStorage {
 public:
  LayeredStorage(Storage& storage, Storage* temp_storage_ptr)
      : _storage(storage), _temp_storage_ptr(temp_storage_ptr) {}

  // Set `value` to the value associated with the given `key` in the specified `collection_id`.
  // The key is looked up in both the temporary and main storage.
  absl::Status Get(size_t collection_id, BytesConstView key, BytesConstView& value) const;

  // Delete a key from the specified collection
  // The deletion is marked in temporary storage if available, otherwise deleted from main storage
  absl::Status Delete(size_t collection_id, BytesConstView key);

  // Merge the temporary storage into the main storage.
  // The temporary storage is cleared after the merge.
  void MergeTempStorage();

  // Ensure that the specified key is present in the temporary storage.
  // If the value is not already present, it is copied from the main storage.
  // If the value is not found, a not-found-error is returned.
  absl::Status EnsureInTempStorage(size_t collection_id, BytesConstView key, Bytes** value);

 private:
  Storage& _storage;
  Storage* _temp_storage_ptr = nullptr;
};
}  // namespace gendb