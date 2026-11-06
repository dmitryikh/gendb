#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gendb/file_io.h"
#include "gendb/file_system.h"
#include "gendb/log_format.h"

namespace gendb::internal {

// A collection's ID and name, recorded in segment headers for error messages.
using CollectionNames = std::vector<std::pair<uint32_t, std::string>>;

// The write-ahead log: commit frames in numbered segments, wal-<first LSN>.log, with contiguous
// LSNs. A crash can only tear the unsynced tail of the last segment, because a segment is synced
// before the next one exists; recovery cuts a torn tail, and treats damage anywhere else as data
// loss.
//
// To tell the two apart in the last segment, sync marks record every few KB how far the log was
// known to be durable, and one closes the log on a clean shutdown. Damage that a later mark says
// was durable is data loss, and anything else may be the tail of a crash. The OS can write pages of
// unsynced data out of order, so intact commits after the damage do not prove anything by
// themselves.
//
// A segment can end in zeros, which durable commits reserve ahead of them, and which recovery and
// WalCursor read as its end: see WritableFile::Reserve. They are nothing that was ever written to
// the log, so a crash that leaves them has torn nothing.
//
// One writer calls Append and Rotate, under the engine's writer lock. Sync can run on any thread
// and never holds up Append: a slow fsync delays only commits that wait for it.
class Wal {
  friend class WalCursor;

 public:
  struct Options {
    std::shared_ptr<FileSystem> file_system;
    std::string dir;
    uint64_t segment_bytes;
    uint64_t reserve_bytes;
    CollectionNames collections;
  };
  struct Recovered {
    uint64_t last_lsn;
    uint64_t bytes_replayed;
    uint64_t commits_replayed;
    uint64_t torn_bytes_cut;
    std::optional<HistoryId> history;  // Of the newest segment, if any.
  };

  explicit Wal(Options options)
      : _options(std::move(options)), _collections(_options.collections) {}

  // Calls `apply` with each intact commit after `after_lsn`, oldest first, and cuts a torn tail.
  absl::StatusOr<Recovered> Recover(uint64_t after_lsn,
                                    const std::function<absl::Status(const Frame&)>& apply);
  // Opens a segment for the commits from `next_lsn` on. Replaces a current one, if any.
  absl::Status StartSegment(const HistoryId& history, uint64_t next_lsn);

  // `frames` are whole commit frames whose last LSN is `last_lsn`. After a failure the log refuses
  // further appends until reopened: what reached the disk is unknown.
  absl::Status Append(std::string_view frames, uint64_t last_lsn, bool sync);
  // Starts a new segment for the commits after the last appended one.
  absl::Status Rotate();
  // Names a collection in the headers from now on, starting with a new segment, so that any commit
  // that follows is in a segment that names it.
  absl::Status AddCollection(uint32_t id, std::string name);
  // Makes every appended commit durable.
  absl::Status Sync();
  // Syncs and leaves a last sync mark, for a clean shutdown.
  absl::Status Close();
  // Stops the log, as after a failed write. Later appends return `status`.
  absl::Status Fail(absl::Status status);
  // Removes segments that hold only commits at or below `lsn`, keeping the newest `retain_bytes`
  // of them, and never the current segment.
  absl::Status Purge(uint64_t lsn, uint64_t retain_bytes);
  // Removes everything, newest first, so a crash midway leaves a valid prefix.
  absl::Status RemoveAll();

  uint64_t synced_lsn() const noexcept { return _synced_lsn.load(std::memory_order_acquire); }
  uint64_t written_lsn() const noexcept { return _written_lsn.load(std::memory_order_acquire); }
  uint64_t total_bytes() const;
  // The name of a collection seen in a segment header during Recover, for error messages.
  std::string NameOf(uint32_t collection_id) const;
  // The newest segment that can hold `lsn`, if `lsn` has not been purged.
  std::optional<std::string> SegmentFor(uint64_t lsn) const;

 private:
  struct Segment {
    uint64_t first_lsn;
    std::string path;
    uint64_t bytes;
  };

  absl::Status OpenSegmentLocked(uint64_t first_lsn);
  absl::Status RollLocked();
  absl::Status WriteSyncMark(WritableFile& file, uint64_t synced);
  // Whether all there is in a file from `offset` on is zeros. How many other bytes there are, if
  // not.
  absl::StatusOr<uint64_t> BytesAfterZeros(const std::string& path, uint64_t offset);
  // Whether the rest of a damaged last segment says that commit `lsn` had been durable.
  absl::StatusOr<bool> MarkedDurableAfter(const std::string& path, uint64_t offset, uint64_t lsn);

  const Options _options;
  HistoryId _history;
  CollectionNames _seen_collections;

  mutable std::mutex _mutex;
  CollectionNames _collections;
  std::shared_ptr<WritableFile> _current;
  std::vector<Segment> _segments;  // Oldest first; the last one is _current.
  absl::Status _failure;

  std::atomic<bool> _failed{false};
  std::atomic<uint64_t> _written_lsn{0};
  std::atomic<uint64_t> _synced_lsn{0};
  uint64_t _bytes_since_mark = 0;  // Writer only.
  uint64_t _marked_lsn = 0;        // Writer only.
};

// Reads committed frames back from the segments, for a replica that the in-memory ring no longer
// covers. It keeps its position between calls.
class WalCursor {
 public:
  explicit WalCursor(const Wal& wal) : _wal(wal) {}

  // Appends whole frames with LSN in (*position, limit] until `out` grows by `max_bytes`, and
  // advances *position. False if the commit after *position has been purged.
  absl::StatusOr<bool> Read(uint64_t* position, uint64_t limit, size_t max_bytes, std::string* out);

 private:
  absl::Status Open(const std::string& path);

  const Wal& _wal;
  std::unique_ptr<FrameFileReader> _reader;
  std::string _reader_path;
  uint64_t _next_lsn = 0;
};

// A segment or checkpoint path for a given LSN, ordered lexically.
std::string NumberedPath(const std::string& dir, std::string_view prefix, uint64_t lsn,
                         std::string_view suffix);
// LSNs of the files `<prefix><lsn><suffix>` in `dir`, ascending.
absl::StatusOr<std::vector<uint64_t>> ListNumbered(FileSystem& file_system, const std::string& dir,
                                                   std::string_view prefix,
                                                   std::string_view suffix);

}  // namespace gendb::internal
