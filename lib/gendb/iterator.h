#pragma once

#include "absl/status/status.h"
#include "gendb/bytes.h"
#include "gendb/index.h"
#include "gendb/layered_storage.h"

namespace gendb {

// Abstract base class for iterator implementation.
template <typename T>
class IteratorImpl {
 public:
  virtual ~IteratorImpl() = default;
  virtual T Value() = 0;
  virtual void Next() = 0;
  virtual bool IsValid() const = 0;
  virtual absl::Status Status() const = 0;
};

// Iterator class using pimpl idiom.
template <typename T>
class Iterator {
 public:
  Iterator(std::unique_ptr<IteratorImpl<T>> impl) : pimpl_(std::move(impl)) {}
  ~Iterator() = default;

  T Value() { return pimpl_->Value(); }
  void Next() { pimpl_->Next(); }
  bool IsValid() const { return pimpl_->IsValid(); }
  bool IsEnd() const { return absl::IsOutOfRange(pimpl_->Status()); }
  absl::Status Status() const { return pimpl_->Status(); }

 private:
  std::unique_ptr<IteratorImpl<T>> pimpl_;
};

template <typename T, typename SecKey>
class SecondaryIndexIterator : public IteratorImpl<T> {
 public:
  SecondaryIndexIterator(const LayeredStorage& storage, size_t collection_id,
                         MergedSetIterator<IndexRecord<SecKey, Bytes>> merge_it)
      : _storage(storage),
        _collection_id(collection_id),
        _merge_it(std::move(merge_it)),
        _status(absl::OkStatus()) {
    LoadCurrent();
  }

  T Value() override { return _current_value.value(); }

  void Next() override {
    _merge_it.next();
    LoadCurrent();
  }

  bool IsValid() const override { return _merge_it.valid() && _current_value.has_value(); }

  absl::Status Status() const override { return _status; }

 private:
  const LayeredStorage& _storage;
  const size_t _collection_id;
  MergedSetIterator<IndexRecord<SecKey, Bytes>> _merge_it;
  std::optional<T> _current_value = std::nullopt;
  absl::Status _status = absl::OkStatus();

  void LoadCurrent() {
    _current_value.reset();
    _status = absl::OkStatus();
    if (!_merge_it.valid()) {
      _status = absl::OutOfRangeError("End of iterator");
      return;
    }
    const auto& rec = _merge_it.value();
    if (rec.is_deleted) {
      // Skip deleted records
      // TODO (correctness): Do not use recursion here.
      Next();
      return;
    }
    BytesConstView value;
    absl::Status s = _storage.Get(_collection_id, rec.prim_key, value);
    if (!s.ok()) {
      _status = s;
      return;
    }
    _current_value = T{value};
  }
};

}  // namespace gendb