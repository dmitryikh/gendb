// Opening a database from whatever is on disk: it may fail, but must not crash or misbehave.
#include <cstdint>
#include <memory>
#include <string>

#include "common.h"
#include "gendb/engine.h"
#include "gendb/wal.h"
#include "lib/memory_file_system.h"

using namespace gendb;
using internal::FrameType;
using internal::HistoryId;

namespace {

constexpr uint64_t kSegmentMagic = 0x4c41574244470a31;
constexpr uint64_t kCheckpointMagic = 0x504e534244470a31;

std::string SegmentHeader(uint64_t first_lsn) {
  std::string body;
  internal::PutFixed64(body, kSegmentMagic);
  internal::PutFixed32(body, internal::kFormatVersion);
  internal::PutHistory(body, HistoryId{1, 2});
  internal::PutFixed64(body, first_lsn);
  internal::PutVarint(body, 0);
  return fuzz::MakeFrame(FrameType::kWalHeader, body);
}

std::string CheckpointHeader(uint64_t lsn) {
  std::string body;
  internal::PutFixed64(body, kCheckpointMagic);
  internal::PutFixed32(body, internal::kFormatVersion);
  internal::PutHistory(body, HistoryId{1, 2});
  internal::PutFixed64(body, lsn);
  return fuzz::MakeFrame(FrameType::kCheckpointHeader, body);
}

void Write(testing::MemoryFileSystem& file_system, const std::string& path,
           const std::string& contents) {
  auto file = file_system.Create(path);
  if (file.ok()) (void)(*file)->Append(contents);
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  fuzz::Pieces input(data, size);
  auto file_system = std::make_shared<testing::MemoryFileSystem>();

  const uint8_t shape = input.Byte();
  uint64_t lsn = 0;
  if ((shape & 1) != 0) {
    lsn = 2;
    std::string checkpoint = CheckpointHeader(lsn);
    while (!input.empty() && input.Byte() != 0) {
      const uint8_t choice = input.Byte();
      if (choice % 3 == 0) {
        std::string body;
        internal::PutFixed32(body, internal::CollectionIdOf(input.Byte() % 2 == 0 ? "users" : "x"));
        internal::PutString(body, input.Next());
        internal::PutVarint(body, input.Byte());
        checkpoint += fuzz::MakeFrame(FrameType::kCollection, body);
      } else if (choice % 3 == 1) {
        checkpoint += fuzz::MakeFrame(FrameType::kRecords, input.Next());
      } else {
        std::string body;
        internal::PutFixed64(body, input.Byte());
        checkpoint += fuzz::MakeFrame(FrameType::kCheckpointFooter, body);
      }
    }
    Write(*file_system, "/db/checkpoint-00000000000000000002.gdb", checkpoint);
  }

  std::string segment = SegmentHeader(lsn + 1);
  for (uint64_t next = lsn + 1; !input.empty(); ++next) {
    const uint8_t choice = input.Byte();
    if (choice % 8 == 0) {
      segment += fuzz::MakeFrame(static_cast<FrameType>(input.Byte()), input.Next());
    } else if (choice % 8 == 1) {
      segment.append(input.Next());  // Not a frame: a torn tail or damage.
    } else {
      segment += fuzz::MakeFrame(FrameType::kCommit, fuzz::CommitBody(next, input));
    }
  }
  Write(*file_system, "/db/wal-" + std::string(19, '0') + std::to_string(lsn + 1) + ".log",
        segment);

  EngineOptions options;
  options.path = "/db";
  options.file_system = file_system;
  options.checkpoint_wal_bytes = 0;
  options.recovery_threads = 1;
  auto engine = Engine::Open(fuzz::MakeSchema(), options);
  if (!engine.ok()) return 0;

  // What did open must work, and survive a checkpoint and another open.
  (void)(*engine)->Checkpoint();
  const uint64_t recovered = (*engine)->Stats().lsn;
  engine->reset();
  auto again = Engine::Open(fuzz::MakeSchema(), options);
  if (again.ok() && (*again)->Stats().lsn != recovered) __builtin_trap();
  return 0;
}
