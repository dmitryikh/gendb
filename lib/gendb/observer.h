#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "absl/status/status.h"

namespace gendb {

enum class LogLevel { kInfo, kWarning, kError };

struct CommitInfo {
  uint64_t lsn;
  size_t records;
  size_t log_bytes;
  bool synced;
  std::chrono::nanoseconds log_time;  // Writing to the log, including any fsync.
};

struct CheckpointInfo {
  uint64_t lsn;
  uint64_t bytes;
  std::chrono::nanoseconds duration;
  absl::Status status;
};

struct RecoveryInfo {
  uint64_t lsn;
  uint64_t commits_replayed;
  uint64_t log_bytes_replayed;
  uint64_t torn_bytes_cut;  // Unacknowledged tail of the log, removed by a crash recovery.
  std::chrono::nanoseconds duration;
};

// How an application sees what the engine does: the engine itself never writes to stderr or to any
// log of its own. Set it in EngineOptions::observer and override what is needed. Calls can come
// from any of the engine's threads at once.
class EngineObserver {
 public:
  virtual ~EngineObserver() = default;
  virtual void OnLog(LogLevel level, std::string_view message) {
    (void)level;
    (void)message;
  }
  // During Commit, on the writer's thread, for commits that reach the log. Keep it quick.
  virtual void OnCommit(const CommitInfo& info) { (void)info; }
  virtual void OnCheckpoint(const CheckpointInfo& info) { (void)info; }
  // During Engine::Open, before it returns.
  virtual void OnRecovery(const RecoveryInfo& info) { (void)info; }
};

}  // namespace gendb
