#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>

#include "gendb/engine.h"

namespace gendb {

// Commits ready for replicas, kept in memory so that following the primary touches no disk.
class Engine::CommitRing {
 public:
  bool enabled() const noexcept { return _enabled.load(std::memory_order_relaxed); }

  void Enable(size_t capacity_bytes, uint64_t lsn, uint64_t digest) {
    std::lock_guard lock(_mutex);
    _capacity = std::max(_capacity, capacity_bytes);
    if (!_enabled.exchange(true)) {
      _latest = lsn;
      _latest_digest = digest;
    }
  }

  void Push(uint64_t lsn, std::string_view frame, uint64_t digest) {
    {
      std::lock_guard lock(_mutex);
      _entries.push_back({lsn, std::string(frame), digest});
      _bytes += frame.size();
      _latest = lsn;
      _latest_digest = digest;
      while (_bytes > _capacity && _entries.size() > 1) {
        _bytes -= _entries.front().frame.size();
        _entries.pop_front();
      }
    }
    _cv.notify_all();
  }

  // Forgets the commits before a new state.
  void Reset(uint64_t lsn, uint64_t digest) {
    {
      std::lock_guard lock(_mutex);
      _entries.clear();
      _bytes = 0;
      _latest = lsn;
      _latest_digest = digest;
    }
    _cv.notify_all();
  }

  uint64_t latest() const {
    std::lock_guard lock(_mutex);
    return _latest;
  }

  Engine::DigestAt latest_digest() const {
    std::lock_guard lock(_mutex);
    return {_latest, _latest_digest};
  }

  bool Contains(uint64_t lsn) const {
    std::lock_guard lock(_mutex);
    return !_entries.empty() && _entries.front().lsn <= lsn && lsn <= _entries.back().lsn;
  }

  // Appends frames with LSN in (*position, limit], at least one if any, until `out` grows by
  // `max_bytes`. False if the next commit is not held here.
  bool Read(uint64_t* position, uint64_t limit, size_t max_bytes, std::string* out,
            Engine::DigestAt* digest) const {
    std::lock_guard lock(_mutex);
    if (_entries.empty() || *position + 1 < _entries.front().lsn) return false;
    const size_t start_size = out->size();
    size_t at = *position + 1 - _entries.front().lsn;
    while (at < _entries.size() && _entries[at].lsn <= limit &&
           out->size() - start_size < max_bytes) {
      out->append(_entries[at].frame);
      *position = _entries[at].lsn;
      *digest = {_entries[at].lsn, _entries[at].digest};
      ++at;
    }
    return true;
  }

  void Wait(uint64_t after, std::chrono::milliseconds timeout) const {
    std::unique_lock lock(_mutex);
    const uint64_t wakeups = _wakeups;
    _cv.wait_for(lock, timeout, [&] { return _latest > after || _wakeups != wakeups; });
  }

  void Wake() const {
    {
      std::lock_guard lock(_mutex);
      ++_wakeups;
    }
    _cv.notify_all();
  }

 private:
  struct Entry {
    uint64_t lsn;
    std::string frame;
    uint64_t digest;  // Of the state after this commit.
  };

  std::atomic<bool> _enabled{false};
  mutable std::mutex _mutex;
  mutable std::condition_variable _cv;
  mutable uint64_t _wakeups = 0;
  std::deque<Entry> _entries;  // Contiguous LSNs.
  size_t _bytes = 0;
  size_t _capacity = 0;
  uint64_t _latest = 0;
  uint64_t _latest_digest = 0;
};

}  // namespace gendb
