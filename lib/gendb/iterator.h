#pragma once

#include <concepts>

#include "absl/status/status.h"
#include "gendb/bytes.h"
#include "gendb/index.h"
#include "gendb/layered_storage.h"

namespace gendb {

// Concept for types with Value() whose result has prim_key convertible to BytesConstView and
// is_deleted returns bool
template <typename T, typename MessageT>
concept IteratorConcept = requires(T t) {
  //   { t.Value() } -> std::same_as<MessageT>;
  { t.Value() };
  { BytesConstView{t.Value().prim_key} };
  { t.Value().is_deleted } -> std::convertible_to<bool>;
  { t.Next() };
  { t.Valid() } -> std::same_as<bool>;
  //   { t.Status() } -> std::same_as<absl::Status>;
};

// Abstract base class for iterator implementation.
template <typename MessageT>
class IteratorImpl {
 public:
  virtual ~IteratorImpl() = default;
  virtual MessageT Value() = 0;
  virtual void Next() = 0;
  virtual bool Valid() const = 0;
  virtual absl::Status Status() const = 0;
};

// Iterator class using pimpl idiom.
template <typename MessageT>
class Iterator {
 public:
  Iterator(std::unique_ptr<IteratorImpl<MessageT>> impl) : pimpl_(std::move(impl)) {}
  ~Iterator() = default;

  MessageT Value() { return pimpl_->Value(); }
  void Next() { pimpl_->Next(); }
  bool Valid() const { return pimpl_->Valid(); }
  bool IsEnd() const { return absl::IsOutOfRange(pimpl_->Status()); }
  absl::Status Status() const { return pimpl_->Status(); }

 private:
  std::unique_ptr<IteratorImpl<MessageT>> pimpl_;
};

template <typename T, typename IteratorT>
  requires IteratorConcept<IteratorT, T>
class SecondaryIndexIterator : public IteratorImpl<T> {
 public:
  SecondaryIndexIterator(const LayeredStorage& storage, size_t collection_id, IteratorT merge_it)
      : _storage(storage),
        _collection_id(collection_id),
        _merge_it(std::move(merge_it)),
        _status(absl::OkStatus()) {
    LoadCurrent();
  }

  T Value() override { return _current_value.value(); }

  void Next() override {
    _merge_it.Next();
    LoadCurrent();
  }

  bool Valid() const override { return _merge_it.Valid() && _current_value.has_value(); }

  absl::Status Status() const override { return _status; }

 private:
  const LayeredStorage& _storage;
  const size_t _collection_id;
  IteratorT _merge_it;
  std::optional<T> _current_value = std::nullopt;
  absl::Status _status = absl::OkStatus();

  void LoadCurrent() {
    _current_value.reset();
    _status = absl::OkStatus();
    if (!_merge_it.Valid()) {
      _status = absl::OutOfRangeError("End of iterator");
      return;
    }
    const auto& rec = _merge_it.Value();
    if (rec.is_deleted) {
      // Skip deleted records
      // TODO (correctness): Do not use recursion here.
      Next();
      return;
    }
    BytesConstView value;
    absl::Status s = _storage.Get(_collection_id, BytesConstView{rec.prim_key}, value);
    if (!s.ok()) {
      _status = s;
      return;
    }
    _current_value = T{value};
  }
};

template <typename MessageT, typename IndexT>
gendb::Iterator<MessageT> MakeSecondaryIndexIterator(
    const LayeredStorage& storage, size_t collection_id,
    typename IndexT::Container::const_iterator begin,
    typename IndexT::Container::const_iterator end) {
  using IteratorT = SingleSetIterator<IndexT>;
  return gendb::Iterator<MessageT>(std::make_unique<SecondaryIndexIterator<MessageT, IteratorT>>(
      storage, collection_id, IteratorT{begin, end}));
}

template <typename MessageT, typename IndexT>
gendb::Iterator<MessageT> MakeSecondaryIndexIterator(
    const LayeredStorage& storage, size_t collection_id,
    typename IndexT::Container::const_iterator begin,
    typename IndexT::Container::const_iterator end,
    typename IndexT::Container::const_iterator m2_begin,
    typename IndexT::Container::const_iterator m2_end) {
  using IteratorT = MergedSetIterator<IndexT>;
  return gendb::Iterator<MessageT>(std::make_unique<SecondaryIndexIterator<MessageT, IteratorT>>(
      storage, collection_id, IteratorT{begin, end, m2_begin, m2_end}));
}

}  // namespace gendb