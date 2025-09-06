#pragma once

#include <unordered_map>
#include <vector>

#include "absl/hash/hash.h"
#include "gendb/bytes.h"

namespace gendb {

struct Storage {
  using Collection = std::unordered_map<Bytes, Bytes, BytesHash, BytesEqual>;
  std::vector<Collection> collections;

  void Put(const size_t collection_id, BytesConstView key, Bytes&& value) {
    if (collection_id >= collections.size()) {
      collections.resize(collection_id + 1);
    }
    auto& coll = collections[collection_id];
    // TODO(perf): can we build the pair in-place?
    coll[Bytes{key.begin(), key.end()}] = std::move(value);
  }
};
}  // namespace gendb