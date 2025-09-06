#pragma once

#include <map>
#include <set>
#include <type_traits>

namespace gendb {

template <typename SecKey, typename PrimKey>
struct IndexRecord {
  SecKey sec_key;
  PrimKey prim_key;
  bool is_deleted;

  // Lexicographical comparison: SecKey, then PrimKey
  auto operator<=>(const IndexRecord& other) const {
    if (auto cmp = sec_key <=> other.sec_key; cmp != 0) return cmp;
    return prim_key <=> other.prim_key;
  }

  bool operator==(const IndexRecord& other) const {
    return sec_key == other.sec_key && prim_key == other.prim_key;
  }

  bool operator!=(const IndexRecord& other) const { return !(*this == other); }
};

// TODO(design): Can I make it type-erased?
//     This way I will be able to use different key types without templating the index
//     implementation.
template <typename SecKey, typename PrimKey>
class Index {
 public:
  auto lower_bound(const SecKey& key) const {
    auto it = _index.lower_bound({key, PrimKey{}});
    return it;
  }
  auto upper_bound(const SecKey& key) const {
    auto it = _index.upper_bound({key + 1, PrimKey{}});
    return it;
  }
  auto end() const { return _index.end(); }
  auto begin() const { return _index.begin(); }

  void Insert(const SecKey& sec_key, const PrimKey& prim_key, bool is_deleted = false) {
    _index.insert({sec_key, prim_key, is_deleted});
  }

  void Erase(const SecKey& sec_key, const PrimKey& prim_key) { _index.erase({sec_key, prim_key}); }

  void MergeTempIndex(Index&& temp_index) {
    for (auto& rec : temp_index._index) {
      if (rec.is_deleted) {
        _index.erase(rec);
      } else {
        // TODO(perf): Optimize insertion (now it's find + insert).
        auto it = _index.find(rec);
        if (it != _index.end()) {
          // TODO(correctness): the field is_deleted doesn't participate as a key. So it's safe to
          // modify it without re-inserting the record.
          const_cast<IndexRecord<SecKey, PrimKey>&>(*it).is_deleted = false;
        } else {
          _index.emplace(std::move(rec));
        }
      }
    }
    temp_index._index.clear();
  }

  std::set<IndexRecord<SecKey, PrimKey>> _index;
};

template <typename SecKey, typename PrimKey, typename Functor>
void ForEachInRange(const Index<SecKey, PrimKey>& index, const SecKey& start, const SecKey& end,
                    Functor&& func) {
  auto it = index.lower_bound(start);
  while (it != index.lower_bound(end)) {
    if (!func(it->first, it->second)) {
      // If the function returns false, we can stop the iteration
      break;
    }
    ++it;
  }
}

template <typename SecKey, typename PrimKey, typename Functor>
void ForEachEqual(const Index<SecKey, PrimKey>& index, const SecKey& key, Functor&& func) {
  auto it = index.lower_bound(key);
  while (it != index.upper_bound(key)) {
    if (!func(*it)) {
      // If the function returns false, we can stop the iteration
      break;
    }
    ++it;
  }
}

template <typename V>
class MergedSetIterator {
 public:
  using Set = std::set<V>;
  using Iter = typename Set::const_iterator;

  MergedSetIterator(Iter m1_begin, Iter m1_end, Iter m2_begin, Iter m2_end)
      : _m1_it{m1_begin}, _m1_end{m1_end}, _m2_it{m2_begin}, _m2_end{m2_end} {}

  bool valid() const { return _m1_it != _m1_end || _m2_it != _m2_end; }

  const V& value() const {
    if (_m2_it != _m2_end && (_m1_it == _m1_end || (*_m2_it) <= (*_m1_it)))
      return *_m2_it;
    else
      return *_m1_it;
  }

  void next() {
    if (!valid()) return;
    if (_m1_it != _m1_end && _m2_it != _m2_end && (*_m1_it) == (*_m2_it)) {
      ++_m1_it;
      ++_m2_it;
    } else if (_m2_it != _m2_end && (_m1_it == _m1_end || (*_m2_it) < (*_m1_it))) {
      ++_m2_it;
    } else {
      ++_m1_it;
    }
  }

 private:
  Iter _m1_it;
  const Iter _m1_end;
  Iter _m2_it;
  const Iter _m2_end;
};

template <typename SecKey, typename PrimKey, typename Functor>
void ForEachInMergedRange(const Index<SecKey, PrimKey>& index1,
                          const Index<SecKey, PrimKey>& index2, const SecKey& start,
                          const SecKey& end, Functor&& func) {
  MergedSetIterator<std::pair<SecKey, PrimKey>> it(
      index1.lower_bound(start), index1.lower_bound(end), index2.lower_bound(start),
      index2.lower_bound(end));
  while (it.valid()) {
    if (!func(it.value())) {
      break;
    }
    it.next();
  }
}

template <typename SecKey, typename PrimKey, typename Functor>
void ForEachInMergedEqual(const Index<SecKey, PrimKey>& index1,
                          const Index<SecKey, PrimKey>& index2, const SecKey& key, Functor&& func) {
  MergedSetIterator<std::pair<SecKey, PrimKey>> it(index1.lower_bound(key), index1.upper_bound(key),
                                                   index2.lower_bound(key),
                                                   index2.upper_bound(key));
  while (it.valid()) {
    if (!func(it.value())) {
      break;
    }
    it.next();
  }
}

}  // namespace gendb
