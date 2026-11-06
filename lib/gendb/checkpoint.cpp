#include "gendb/checkpoint.h"

#include <filesystem>

#include "absl/strings/str_cat.h"
#include "gendb/status.h"

namespace gendb::internal {
namespace {

constexpr uint64_t kCheckpointMagic = 0x504e534244470a31;
constexpr size_t kRecordBlockBytes = 1 << 20;

// Frames kept in memory, each chunk holding whole frames. Freed as they are read.
class MemoryFrameSource final : public FrameSource {
 public:
  explicit MemoryFrameSource(std::vector<std::string> chunks) : _chunks(std::move(chunks)) {}

  ParseStatus Peek(Frame* frame) override {
    while (_index < _chunks.size() && _position == _chunks[_index].size()) {
      std::string().swap(_chunks[_index]);
      ++_index;
      _position = 0;
    }
    if (_index == _chunks.size()) return ParseStatus::kIncomplete;
    const ParseStatus status =
        ParseFrame(std::string_view(_chunks[_index]).substr(_position), frame);
    if (status == ParseStatus::kOk) _current_size = frame->raw.size();
    return status;
  }
  void Advance() override {
    _position += _current_size;
    _current_size = 0;
  }

 private:
  std::vector<std::string> _chunks;
  size_t _index = 0;
  size_t _position = 0;
  size_t _current_size = 0;
};

}  // namespace

absl::Status CheckpointWriter::Begin(const HistoryId& history, uint64_t lsn) {
  _frame.clear();
  const size_t begin = BeginFrame(_frame, FrameType::kCheckpointHeader);
  PutFixed64(_frame, kCheckpointMagic);
  PutFixed32(_frame, kFormatVersion);
  PutHistory(_frame, history);
  PutFixed64(_frame, lsn);
  EndFrame(_frame, begin);
  return _sink(_frame);
}

absl::Status CheckpointWriter::BeginCollection(uint32_t id, std::string_view name,
                                               uint64_t expected_records) {
  RETURN_IF_ERROR(FlushRecords());
  _frame.clear();
  const size_t begin = BeginFrame(_frame, FrameType::kCollection);
  PutFixed32(_frame, id);
  PutString(_frame, name);
  PutVarint(_frame, expected_records);
  EndFrame(_frame, begin);
  return _sink(_frame);
}

absl::Status CheckpointWriter::Add(std::string_view key, std::string_view value) {
  PutVarint(_records, key.size());
  PutVarint(_records, value.size());
  _records.append(key);
  _records.append(value);
  ++_block_count;
  ++_total;
  return _records.size() >= kRecordBlockBytes ? FlushRecords() : absl::OkStatus();
}

absl::Status CheckpointWriter::FlushRecords() {
  if (_block_count == 0) return absl::OkStatus();
  _frame.clear();
  const size_t begin = BeginFrame(_frame, FrameType::kRecords);
  PutVarint(_frame, _block_count);
  _frame.append(_records);
  EndFrame(_frame, begin);
  _records.clear();
  _block_count = 0;
  return _sink(_frame);
}

absl::Status CheckpointWriter::Finish() {
  RETURN_IF_ERROR(FlushRecords());
  _frame.clear();
  const size_t begin = BeginFrame(_frame, FrameType::kCheckpointFooter);
  PutFixed64(_frame, _total);
  EndFrame(_frame, begin);
  return _sink(_frame);
}

absl::Status ReadCheckpoint(FrameSource& source, CheckpointHandler& handler) {
  bool has_header = false;
  bool has_collection = false;
  uint64_t total = 0;
  for (;;) {
    Frame frame;
    const ParseStatus status = source.Peek(&frame);
    RETURN_IF_ERROR(source.status());
    if (status != ParseStatus::kOk) return absl::DataLossError("Incomplete or damaged checkpoint");
    Decoder decoder(frame.body);
    switch (frame.type) {
      case FrameType::kCheckpointHeader: {
        const uint64_t magic = decoder.Fixed64();
        const uint32_t version = decoder.Fixed32();
        const HistoryId history = decoder.History();
        const uint64_t lsn = decoder.Fixed64();
        if (!decoder.ok() || magic != kCheckpointMagic) {
          return absl::DataLossError("Not a checkpoint");
        }
        if (version != kFormatVersion) {
          return absl::FailedPreconditionError(
              absl::StrCat("Unsupported checkpoint version ", version));
        }
        RETURN_IF_ERROR(handler.OnHeader(history, lsn));
        has_header = true;
        break;
      }
      case FrameType::kCollection: {
        const uint32_t id = decoder.Fixed32();
        const std::string_view name = decoder.String();
        const uint64_t expected = decoder.Varint();
        if (!decoder.ok() || !has_header) return absl::DataLossError("Malformed checkpoint");
        RETURN_IF_ERROR(handler.OnCollection(id, name, expected));
        has_collection = true;
        break;
      }
      case FrameType::kRecords: {
        const uint64_t count = decoder.Varint();
        if (!has_collection) return absl::DataLossError("Malformed checkpoint");
        for (uint64_t i = 0; i < count; ++i) {
          const uint64_t key_size = decoder.Varint();
          const uint64_t value_size = decoder.Varint();
          const std::string_view key = decoder.Bytes(key_size);
          const std::string_view value = decoder.Bytes(value_size);
          if (!decoder.ok()) return absl::DataLossError("Malformed checkpoint");
          handler.OnRecord(key, value);
        }
        total += count;
        break;
      }
      case FrameType::kCheckpointFooter: {
        const uint64_t expected = decoder.Fixed64();
        if (!decoder.ok() || !has_header || expected != total) {
          return absl::DataLossError("Malformed checkpoint");
        }
        return absl::OkStatus();
      }
      default:
        return absl::DataLossError("Unexpected frame in a checkpoint");
    }
    source.Advance();
  }
}

absl::StatusOr<std::unique_ptr<CheckpointStage>> CheckpointStage::Create(
    std::shared_ptr<FileSystem> file_system, std::string path) {
  auto stage = std::make_unique<CheckpointStage>();
  stage->_file_system = std::move(file_system);
  stage->_path = std::move(path);
  if (!stage->_path.empty()) {
    auto file = stage->_file_system->Create(stage->_path);
    if (!file.ok()) return file.status();
    stage->_file = std::move(*file);
  }
  return stage;
}

absl::Status CheckpointStage::Add(std::string_view frames) {
  if (_file != nullptr) return _file->Append(frames);
  _chunks.emplace_back(frames);
  return absl::OkStatus();
}

absl::Status CheckpointStage::Publish(const std::string& final_path) {
  if (_file == nullptr) return absl::OkStatus();
  RETURN_IF_ERROR(_file->Sync());
  RETURN_IF_ERROR(_file_system->Rename(_path, final_path));
  return _file_system->SyncDirectory(std::filesystem::path(final_path).parent_path().string());
}

absl::StatusOr<std::unique_ptr<FrameSource>> CheckpointStage::OpenSource(const std::string& path) {
  if (_file == nullptr) {
    return std::unique_ptr<FrameSource>(new MemoryFrameSource(std::move(_chunks)));
  }
  auto reader = FrameFileReader::Open(*_file_system, path);
  if (!reader.ok()) return reader.status();
  return std::unique_ptr<FrameSource>(std::move(*reader));
}

}  // namespace gendb::internal
