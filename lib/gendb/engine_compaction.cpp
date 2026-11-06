#include <algorithm>
#include <chrono>
#include <utility>

#include "absl/strings/str_cat.h"
#include "gendb/checkpoint.h"
#include "gendb/engine.h"
#include "gendb/engine_internal.h"
#include "gendb/status.h"
#include "gendb/wal.h"

namespace gendb {

using internal::AsStringView;
using internal::kCheckpointPrefix;
using internal::kCheckpointSuffix;
using internal::kTempSuffix;
using internal::NumberedPath;

absl::Status Engine::ExportCheckpoint(
    const EngineSnapshot& snapshot, const LogPosition& position,
    const std::function<absl::Status(std::string_view)>& sink) const {
  internal::CheckpointWriter writer(sink);
  RETURN_IF_ERROR(writer.Begin(position.history, position.lsn));
  for (size_t c = 0; c < collection_count(); ++c) {
    const Collection& collection = *_collections[c];
    if (collection.ephemeral) continue;
    // Nothing would bring back a foreign collection that has emptied.
    if (c >= _schema.collections.size() && collection.table.live() == 0) continue;
    RETURN_IF_ERROR(
        writer.BeginCollection(collection.id, collection.name, collection.table.live()));
    for (ScanCursor cursor(snapshot, c); cursor.Valid(); cursor.Next()) {
      RETURN_IF_ERROR(writer.Add(cursor.key(), AsStringView(cursor.value())));
    }
  }
  return writer.Finish();
}

absl::Status Engine::Checkpoint() {
  if (_wal == nullptr) return absl::FailedPreconditionError("The engine is in memory only");
  std::lock_guard serial(_checkpoint_mutex);
  const auto started = std::chrono::steady_clock::now();

  std::optional<EngineSnapshot> snapshot;
  LogPosition position;
  uint64_t log_bytes;
  {
    // The snapshot's sequence number and the LSN must describe the same commit.
    auto writer = AcquireWriterFirst();
    position = {_history, _lsn.load(std::memory_order_relaxed)};
    if (position.lsn == _checkpoint_lsn.load(std::memory_order_relaxed) &&
        !_rewrite_checkpoint.load(std::memory_order_relaxed)) {
      return absl::OkStatus();
    }
    RETURN_IF_ERROR(_wal->Rotate());
    snapshot.emplace(*this);
    log_bytes = _wal_bytes_since_checkpoint.exchange(0, std::memory_order_relaxed);
  }
  const auto report = [&](absl::Status status, uint64_t bytes) {
    if (_options.observer != nullptr) {
      _options.observer->OnCheckpoint(
          {position.lsn, bytes, std::chrono::steady_clock::now() - started, status});
    }
    return status;
  };

  const std::string& path = _options.path;
  const std::string final_path =
      NumberedPath(path, kCheckpointPrefix, position.lsn, kCheckpointSuffix);
  const std::string temp_path = absl::StrCat(final_path, kTempSuffix);
  auto file = _file_system->Create(temp_path);
  absl::Status status = file.status();
  if (status.ok()) {
    status = ExportCheckpoint(*snapshot, position,
                              [&file](std::string_view frames) { return (*file)->Append(frames); });
  }
  snapshot.reset();  // Lets go of the old versions before the slow sync.
  if (status.ok()) status = (*file)->Sync();
  if (status.ok()) status = _file_system->Rename(temp_path, final_path);
  if (status.ok()) status = _file_system->SyncDirectory(path);
  if (!status.ok()) {
    (void)_file_system->Remove(temp_path);
    _wal_bytes_since_checkpoint.fetch_add(log_bytes, std::memory_order_relaxed);
    return report(std::move(status), 0);
  }

  _checkpoint_bytes.store((*file)->size(), std::memory_order_relaxed);
  _checkpoint_lsn.store(position.lsn, std::memory_order_relaxed);
  _rewrite_checkpoint.store(false, std::memory_order_relaxed);
  RemoveCheckpointsBelow(position.lsn);
  return report(PurgeWal(), (*file)->size());
}

void Engine::RemoveCheckpointsBelow(uint64_t lsn) const {
  auto checkpoints =
      internal::ListNumbered(*_file_system, _options.path, kCheckpointPrefix, kCheckpointSuffix);
  if (!checkpoints.ok()) return;  // Leftovers are harmless: the newest one is used.
  bool removed = false;
  for (uint64_t old : *checkpoints) {
    if (old == lsn) continue;
    (void)_file_system->Remove(
        NumberedPath(_options.path, kCheckpointPrefix, old, kCheckpointSuffix));
    removed = true;
  }
  if (removed) (void)_file_system->SyncDirectory(_options.path);
}

absl::Status Engine::PurgeWal() {
  uint64_t keep = _checkpoint_lsn.load(std::memory_order_relaxed);
  if (_wal->total_bytes() <= _options.wal_replica_limit_bytes) {
    std::lock_guard lock(_wal_pins_mutex);
    for (const auto& [pin, lsn] : _wal_pins) keep = std::min(keep, lsn);
  }
  return _wal->Purge(keep, _options.wal_retain_bytes);
}

void Engine::AfterCommit(uint64_t wal_bytes) {
  if (_wal == nullptr || _options.checkpoint_wal_bytes == 0) return;
  const uint64_t total =
      _wal_bytes_since_checkpoint.fetch_add(wal_bytes, std::memory_order_relaxed) + wal_bytes;
  const uint64_t threshold =
      std::max(_options.checkpoint_wal_bytes, _checkpoint_bytes.load(std::memory_order_relaxed));
  if (total < threshold || _checkpoint_requested.exchange(true)) return;
  { std::lock_guard lock(_background_mutex); }
  _background_cv.notify_all();
}

void Engine::RecordBackgroundError(absl::Status status) {
  if (status.ok()) return;
  Log(LogLevel::kError, absl::StrCat("Background work failed: ", status.ToString()));
  std::lock_guard lock(_background_mutex);
  _background_error = std::move(status);
}

void Engine::BackgroundLoop() {
  const auto interval = _options.background_sync_interval;
  std::unique_lock lock(_background_mutex);
  while (!_background_stop) {
    if (interval.count() > 0) {
      _background_cv.wait_for(lock, interval);
    } else {
      _background_cv.wait(lock, [this] {
        return _background_stop || _checkpoint_requested.load(std::memory_order_acquire);
      });
    }
    if (_background_stop) break;
    lock.unlock();
    if (interval.count() > 0) RecordBackgroundError(_wal->Sync());
    if (_checkpoint_requested.exchange(false)) RecordBackgroundError(Checkpoint());
    lock.lock();
  }
}

}  // namespace gendb
