#include <algorithm>
#include <utility>

#include "absl/strings/str_cat.h"
#include "gendb/commit_ring.h"
#include "gendb/engine.h"
#include "gendb/engine_internal.h"
#include "gendb/status.h"
#include "gendb/wal.h"

namespace gendb {

using internal::HistoryId;
using internal::WalCursor;

absl::Status Engine::PromoteToPrimary() {
  auto writer = AcquireWriterFirst();
  _replica.store(false, std::memory_order_release);
  _history = HistoryId::Random();
  _history_generation.fetch_add(1, std::memory_order_acq_rel);
  _ring->Wake();
  if (_wal == nullptr) return absl::OkStatus();
  return _wal->StartSegment(_history, _lsn.load(std::memory_order_relaxed) + 1);
}

absl::Status Engine::RegisterCollections(
    const std::vector<std::pair<uint32_t, std::string>>& collections) {
  auto writer = AcquireWriterFirst();
  for (const auto& [id, name] : collections) {
    const std::optional<size_t> known = FindDurableCollection(id);
    if (!known.has_value()) {
      auto foreign = AddForeignCollection(id, name);
      if (!foreign.ok()) return foreign.status();
    } else if (_collections[*known]->name != name) {
      return absl::FailedPreconditionError(
          absl::StrCat("Collections '", name, "' on the primary and '", _collections[*known]->name,
                       "' here have the same ID: rename one"));
    }
  }
  return absl::OkStatus();
}

void Engine::EnableCommitRing(size_t capacity_bytes) {
  auto writer = AcquireWriterFirst();
  _ring->Enable(capacity_bytes, _lsn.load(std::memory_order_relaxed));
}

Engine::LogPosition Engine::Position() {
  auto writer = AcquireWriterFirst();
  return {_history, _lsn.load(std::memory_order_relaxed)};
}

bool Engine::CanReadLogAfter(uint64_t lsn) const {
  if (lsn == _lsn.load(std::memory_order_acquire) || _ring->Contains(lsn + 1)) return true;
  return _wal != nullptr && _wal->SegmentFor(lsn + 1).has_value();
}

uint64_t Engine::PinWal(uint64_t lsn) {
  std::lock_guard lock(_wal_pins_mutex);
  _wal_pins.emplace_back(_next_wal_pin, lsn);
  return _next_wal_pin++;
}

void Engine::UpdateWalPin(uint64_t pin, uint64_t lsn) {
  std::lock_guard lock(_wal_pins_mutex);
  for (auto& [id, kept] : _wal_pins) {
    if (id == pin) kept = lsn;
  }
}

void Engine::UnpinWal(uint64_t pin) {
  std::lock_guard lock(_wal_pins_mutex);
  std::erase_if(_wal_pins, [pin](const auto& entry) { return entry.first == pin; });
}

absl::StatusOr<bool> Engine::ReadLog(uint64_t* position, size_t max_bytes, std::string* out,
                                     std::unique_ptr<WalCursor>* cursor) {
  const uint64_t latest = _ring->latest();
  if (latest <= *position) return false;
  uint64_t limit = latest;
  if (_wal != nullptr) {
    // A commit leaves the primary only once it is durable here, so that a replica can never be
    // ahead of what this engine recovers after a power loss.
    RETURN_IF_ERROR(_wal->Sync());
    limit = std::min(latest, _wal->synced_lsn());
    if (limit <= *position) return false;
  }
  if (_ring->Read(position, limit, max_bytes, out)) return true;
  if (_wal != nullptr) {
    if (*cursor == nullptr) *cursor = std::make_unique<WalCursor>(*_wal);
    auto read = (*cursor)->Read(position, limit, max_bytes, out);
    if (!read.ok() || *read) return read;
  }
  return absl::OutOfRangeError(absl::StrCat("Commit ", *position + 1, " is no longer available"));
}

void Engine::WaitForLog(uint64_t after, std::chrono::milliseconds timeout) const {
  _ring->Wait(after, timeout);
}

void Engine::WakeLogWaiters() const {
  _ring->Wake();
}

}  // namespace gendb
