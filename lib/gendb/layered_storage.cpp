#include "gendb/layered_storage.h"

namespace gendb {

absl::Status LayeredStorage::Get(const size_t collection_id, BytesConstView key,
                                 BytesConstView& value) const {
  if (_temp_storage_ptr != nullptr && collection_id < _temp_storage_ptr->collections.size()) {
    auto& coll = _temp_storage_ptr->collections[collection_id];
    auto it = coll.find(std::vector<uint8_t>(key.begin(), key.end()));
    if (it != coll.end()) {
      if (it->second.empty()) {
        return absl::NotFoundError("Key not found");
      }
      value = BytesConstView(it->second);
      return absl::OkStatus();
    }
  }
  if (collection_id >= _storage.collections.size()) {
    return absl::NotFoundError("Collection not found");
  }
  auto& coll = _storage.collections[collection_id];
  auto it = coll.find(std::vector<uint8_t>(key.begin(), key.end()));
  if (it == coll.end()) {
    return absl::NotFoundError("Key not found");
  }
  value = BytesConstView(it->second);
  return absl::OkStatus();
}

absl::Status LayeredStorage::EnsureInTempStorage(const size_t collection_id, BytesConstView key,
                                                 Bytes** value) {
  if (_temp_storage_ptr == nullptr) {
    return absl::NotFoundError("No temp storage");
  }
  _temp_storage_ptr->collections.resize(
      std::max(_temp_storage_ptr->collections.size(), collection_id + 1));
  auto& temp_coll = _temp_storage_ptr->collections[collection_id];

  auto [temp_id, added] = temp_coll.try_emplace(Bytes{key.begin(), key.end()});
  if (!added) {
    *value = &temp_id->second;
    return absl::OkStatus();
  }

  if (collection_id >= _storage.collections.size()) {
    return absl::NotFoundError("Collection not found");
  }
  auto& coll = _storage.collections[collection_id];
  auto it = coll.find(key);
  if (it == coll.end()) {
    return absl::NotFoundError("Key not found");
  }

  temp_id->second = it->second;
  *value = &temp_id->second;

  return absl::OkStatus();
}

void LayeredStorage::MergeTempStorage() {
  if (_temp_storage_ptr == nullptr) {
    return;
  }
  for (size_t i = 0; i < _temp_storage_ptr->collections.size(); ++i) {
    auto& temp_coll = _temp_storage_ptr->collections[i];
    for (auto& [key, value] : temp_coll) {
      _storage.Put(i, key, std::move(value));
    }
    temp_coll.clear();
  }
}
}  // namespace gendb