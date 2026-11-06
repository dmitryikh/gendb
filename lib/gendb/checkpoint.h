#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gendb/file_io.h"
#include "gendb/log_format.h"

// A checkpoint is the image of a database at one LSN, as frames:
//
//   header:     [magic: u64] [version: u32] [history: 16 bytes] [lsn: u64]
//   collection: [id: u32] [name: string] [expected records: varint]
//   records:    [count: varint] ([key size: varint] [value size: varint] key value)*
//   footer:     [total records: u64]
//
// The same bytes are a file on disk and the stream that seeds a replica. Without a footer a
// checkpoint is incomplete and is not used.
namespace gendb::internal {

using FrameSink = std::function<absl::Status(std::string_view frames)>;

class CheckpointWriter {
 public:
  explicit CheckpointWriter(FrameSink sink) : _sink(std::move(sink)) {}

  absl::Status Begin(const HistoryId& history, uint64_t lsn);
  absl::Status BeginCollection(uint32_t id, std::string_view name, uint64_t expected_records);
  absl::Status Add(std::string_view key, std::string_view value);
  absl::Status Finish();

 private:
  absl::Status FlushRecords();

  FrameSink _sink;
  std::string _frame;    // Scratch for one frame.
  std::string _records;  // The current block's encoded records.
  uint64_t _block_count = 0;
  uint64_t _total = 0;
};

class CheckpointHandler {
 public:
  virtual ~CheckpointHandler() = default;
  virtual absl::Status OnHeader(const HistoryId& history, uint64_t lsn) = 0;
  virtual absl::Status OnCollection(uint32_t id, std::string_view name, uint64_t expected) = 0;
  // Records belong to the latest collection.
  virtual void OnRecord(std::string_view key, std::string_view value) = 0;
};

// DataLoss unless `source` holds a whole, intact checkpoint.
absl::Status ReadCheckpoint(FrameSource& source, CheckpointHandler& handler);

// A checkpoint arriving from a primary, held aside until complete: on disk when the engine has a
// directory, where it becomes the checkpoint file, otherwise in memory. Only verified whole frames
// are added.
class CheckpointStage {
 public:
  // An empty `path` stages in memory.
  static absl::StatusOr<std::unique_ptr<CheckpointStage>> Create(std::string path);

  absl::Status Add(std::string_view frames);
  // Syncs a staged file and moves it to `final_path`, in the same directory.
  absl::Status Publish(const std::string& final_path);
  // In memory, consumes the stage as it reads.
  absl::StatusOr<std::unique_ptr<FrameSource>> OpenSource(const std::string& path);

 private:
  std::unique_ptr<AppendFile> _file;
  std::vector<std::string> _chunks;
};

}  // namespace gendb::internal
