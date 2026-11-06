#include "gendb/wal.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <system_error>

#include "absl/strings/str_cat.h"
#include "gendb/status.h"

namespace gendb::internal {
namespace {

constexpr std::string_view kSegmentPrefix = "wal-";
constexpr std::string_view kSegmentSuffix = ".log";
constexpr uint64_t kSegmentMagic = 0x4c41574244470a31;

// How often a sync mark goes into the log. Damage in about this much of the end of a segment, after
// a crash, cannot be told from a torn tail.
constexpr uint64_t kSyncMarkIntervalBytes = 4096;

}  // namespace

std::string NumberedPath(const std::string& dir, std::string_view prefix, uint64_t lsn,
                         std::string_view suffix) {
  char digits[21];
  std::snprintf(digits, sizeof(digits), "%020llu", static_cast<unsigned long long>(lsn));
  return absl::StrCat(dir, "/", prefix, digits, suffix);
}

absl::StatusOr<std::vector<uint64_t>> ListNumbered(FileSystem& file_system, const std::string& dir,
                                                   std::string_view prefix,
                                                   std::string_view suffix) {
  auto names = file_system.List(dir);
  if (!names.ok()) return names.status();
  std::vector<uint64_t> lsns;
  for (const std::string& name : *names) {
    if (name.size() <= prefix.size() + suffix.size() || !name.starts_with(prefix) ||
        !name.ends_with(suffix)) {
      continue;
    }
    uint64_t lsn = 0;
    const char* first = name.data() + prefix.size();
    const char* last = name.data() + name.size() - suffix.size();
    const auto [end, parsed] = std::from_chars(first, last, lsn);
    if (parsed == std::errc() && end == last) lsns.push_back(lsn);
  }
  std::sort(lsns.begin(), lsns.end());
  return lsns;
}

absl::StatusOr<bool> Wal::MarkedDurableAfter(const std::string& path, uint64_t offset,
                                             uint64_t lsn) {
  FileSystem& file_system = *_options.file_system;
  auto size = file_system.FileSize(path);
  if (!size.ok()) return size.status();
  if (*size <= offset) return false;
  auto file = file_system.OpenForRead(path);
  if (!file.ok()) return file.status();
  std::string rest(*size - offset, '\0');
  auto got = (*file)->Read(offset, rest.data(), rest.size());
  if (!got.ok()) return got.status();
  std::string_view view(rest.data(), *got);
  while (view.size() >= kFrameHeaderSize) {
    Frame frame;
    if (ParseFrame(view, &frame) != ParseStatus::kOk) {
      view.remove_prefix(1);
      continue;
    }
    if (frame.type == FrameType::kSyncMark) {
      Decoder mark(frame.body);
      const uint64_t synced = mark.Fixed64();
      if (mark.ok() && synced >= lsn) return true;
    }
    view.remove_prefix(frame.raw.size());
  }
  return false;
}

absl::StatusOr<uint64_t> Wal::BytesAfterZeros(const std::string& path, uint64_t offset) {
  FileSystem& file_system = *_options.file_system;
  auto size = file_system.FileSize(path);
  if (!size.ok()) return size.status();
  if (*size <= offset) return uint64_t{0};
  auto file = file_system.OpenForRead(path);
  if (!file.ok()) return file.status();
  std::string rest(*size - offset, '\0');
  auto got = (*file)->Read(offset, rest.data(), rest.size());
  if (!got.ok()) return got.status();
  const size_t last = std::string_view(rest.data(), *got).find_last_not_of('\0');
  return last == std::string_view::npos ? uint64_t{0} : uint64_t{last + 1};
}

absl::StatusOr<Wal::Recovered> Wal::Recover(
    uint64_t after_lsn, const std::function<absl::Status(const Frame&)>& apply) {
  FileSystem& file_system = *_options.file_system;
  auto first_lsns = ListNumbered(file_system, _options.dir, kSegmentPrefix, kSegmentSuffix);
  if (!first_lsns.ok()) return first_lsns.status();

  Recovered result{after_lsn, 0, 0, 0, std::nullopt};
  const auto add_name = [this](uint32_t id, std::string_view name) {
    for (const auto& [known, _] : _seen_collections) {
      if (known == id) return;
    }
    _seen_collections.emplace_back(id, std::string(name));
  };

  for (size_t i = 0; i < first_lsns->size(); ++i) {
    const uint64_t first_lsn = (*first_lsns)[i];
    const std::string path = NumberedPath(_options.dir, kSegmentPrefix, first_lsn, kSegmentSuffix);
    const bool last_segment = i + 1 == first_lsns->size();
    auto reader = FrameFileReader::Open(file_system, path);
    if (!reader.ok()) return reader.status();

    // The header. A crash while creating the last segment can leave it incomplete.
    Frame frame;
    const ParseStatus header = (*reader)->Peek(&frame);
    RETURN_IF_ERROR((*reader)->status());
    if (header != ParseStatus::kOk) {
      auto marked = MarkedDurableAfter(path, 0, result.last_lsn + 1);
      if (!marked.ok()) return marked.status();
      if (!last_segment || *marked) {
        return absl::DataLossError(absl::StrCat("Damaged header: ", path));
      }
      RETURN_IF_ERROR(file_system.Remove(path));
      continue;
    }
    Decoder decoder(frame.body);
    const uint64_t magic = decoder.Fixed64();
    const uint32_t version = decoder.Fixed32();
    const HistoryId history = decoder.History();
    const uint64_t header_lsn = decoder.Fixed64();
    const uint64_t name_count = decoder.Varint();
    for (uint64_t n = 0; n < name_count && decoder.ok(); ++n) {
      const uint32_t id = decoder.Fixed32();
      add_name(id, decoder.String());
    }
    if (frame.type != FrameType::kWalHeader || magic != kSegmentMagic || !decoder.ok() ||
        header_lsn != first_lsn) {
      return absl::DataLossError(absl::StrCat("Not a log segment: ", path));
    }
    if (version != kFormatVersion) {
      return absl::FailedPreconditionError(
          absl::StrCat("Unsupported log version ", version, ": ", path));
    }
    result.history = history;
    const uint64_t header_end = frame.raw.size();
    (*reader)->Advance();

    // A segment the next one fully supersedes needs no reading.
    const bool superseded = !last_segment && (*first_lsns)[i + 1] <= after_lsn + 1;
    uint64_t size = header_end;
    if (!superseded) {
      for (;;) {
        const ParseStatus status = (*reader)->Peek(&frame);
        // A failure to read says nothing about the data, and must not look like a torn tail.
        RETURN_IF_ERROR((*reader)->status());
        if (status != ParseStatus::kOk) {
          if (status == ParseStatus::kIncomplete && (*reader)->at_end()) break;
          const uint64_t damaged_at = (*reader)->offset();
          // Zeros to the end are what is left of a reservation, if a crash took away the cut that
          // follows the end of a segment. Were any commits gone, the next segment would show a gap.
          auto bytes = BytesAfterZeros(path, damaged_at);
          if (!bytes.ok()) return bytes.status();
          if (*bytes == 0) break;
          auto marked = MarkedDurableAfter(path, damaged_at, result.last_lsn + 1);
          if (!marked.ok()) return marked.status();
          if (!last_segment || *marked) {
            return absl::DataLossError(absl::StrCat("Damaged segment at offset ", damaged_at,
                                                    ", in data known to be durable: ", path));
          }
          break;  // The tail of a crash: nothing in it was acknowledged as durable.
        }
        if (frame.type == FrameType::kSyncMark) {
          size = (*reader)->offset() + frame.raw.size();
          (*reader)->Advance();
          continue;
        }
        if (frame.type != FrameType::kCommit) {
          return absl::DataLossError(absl::StrCat("Unexpected frame in ", path));
        }
        CommitDecoder commit(frame.body);
        if (!commit.ok()) return absl::DataLossError(absl::StrCat("Malformed commit in ", path));
        if (commit.lsn > after_lsn) {
          if (commit.lsn != result.last_lsn + 1) {
            return absl::DataLossError(absl::StrCat("The log jumps from LSN ", result.last_lsn,
                                                    " to ", commit.lsn, " in ", path));
          }
          RETURN_IF_ERROR(apply(frame));
          result.last_lsn = commit.lsn;
          result.bytes_replayed += frame.raw.size();
          ++result.commits_replayed;
        }
        size = (*reader)->offset() + frame.raw.size();
        (*reader)->Advance();
      }
      if (last_segment) {
        auto on_disk = file_system.FileSize(path);
        if (!on_disk.ok()) return on_disk.status();
        if (*on_disk > size) {
          auto torn = BytesAfterZeros(path, size);
          if (!torn.ok()) return torn.status();
          result.torn_bytes_cut = *torn;
          RETURN_IF_ERROR(file_system.TruncateAndSync(path, size));
        }
      }
    } else {
      auto on_disk = file_system.FileSize(path);
      if (!on_disk.ok()) return on_disk.status();
      size = *on_disk;
    }
    _segments.push_back({first_lsn, path, size});
  }
  if (!_segments.empty() && _segments.front().first_lsn > after_lsn + 1) {
    return absl::DataLossError(absl::StrCat("The log starts at LSN ", _segments.front().first_lsn,
                                            " but state is only available up to ", after_lsn));
  }
  return result;
}

absl::Status Wal::OpenSegmentLocked(uint64_t first_lsn) {
  std::string path = NumberedPath(_options.dir, kSegmentPrefix, first_lsn, kSegmentSuffix);
  // Without commits, the latest segment can be replaced under the same number.
  if (!_segments.empty() && _segments.back().first_lsn == first_lsn) _segments.pop_back();

  auto file = _options.file_system->Create(path);
  if (!file.ok()) return file.status();

  std::string header;
  const size_t begin = BeginFrame(header, FrameType::kWalHeader);
  PutFixed64(header, kSegmentMagic);
  PutFixed32(header, kFormatVersion);
  PutHistory(header, _history);
  PutFixed64(header, first_lsn);
  PutVarint(header, _collections.size());
  for (const auto& [id, name] : _collections) {
    PutFixed32(header, id);
    PutString(header, name);
  }
  EndFrame(header, begin);
  RETURN_IF_ERROR((*file)->Append(header));
  RETURN_IF_ERROR((*file)->Sync());
  RETURN_IF_ERROR(_options.file_system->SyncDirectory(_options.dir));

  _current = std::shared_ptr<WritableFile>(std::move(*file));
  _segments.push_back({first_lsn, std::move(path), _current->size()});
  return absl::OkStatus();
}

absl::Status Wal::StartSegment(const HistoryId& history, uint64_t next_lsn) {
  std::lock_guard lock(_mutex);
  if (_current != nullptr) {
    RETURN_IF_ERROR(_current->Sync());
    RETURN_IF_ERROR(_current->ReleaseReserved());
    _segments.back().bytes = _current->size();
  }
  _history = history;
  _written_lsn.store(next_lsn - 1, std::memory_order_release);
  _synced_lsn.store(next_lsn - 1, std::memory_order_release);
  return OpenSegmentLocked(next_lsn);
}

absl::Status Wal::Fail(absl::Status status) {
  std::lock_guard lock(_mutex);
  if (_failure.ok()) _failure = status;
  _failed.store(true, std::memory_order_release);
  return status;
}

absl::Status Wal::Append(std::string_view frames, uint64_t last_lsn, bool sync) {
  if (_failed.load(std::memory_order_acquire)) {
    std::lock_guard lock(_mutex);
    return _failure;
  }
  // Only this thread replaces _current, so reading it here needs no lock.
  WritableFile& file = *_current;
  if (sync && _options.reserve_bytes > 0) {
    // Room for a sync mark as well. Commits that are not durable do not come here: it would slow
    // them down, for a sync they do not wait for.
    const uint64_t needed = frames.size() + kFrameHeaderSize + 16;
    if (file.reserved() < needed) {
      const uint64_t to_end =
          _options.segment_bytes > file.size() ? _options.segment_bytes - file.size() : 0;
      const uint64_t bytes = std::max(needed, std::min(_options.reserve_bytes, to_end));
      if (absl::Status status = file.Reserve(bytes); !status.ok()) return Fail(std::move(status));
    }
  }
  const uint64_t synced = _synced_lsn.load(std::memory_order_acquire);
  if (_bytes_since_mark >= kSyncMarkIntervalBytes && synced > _marked_lsn) {
    if (absl::Status status = WriteSyncMark(file, synced); !status.ok()) {
      return Fail(std::move(status));
    }
  }
  if (absl::Status status = file.Append(frames); !status.ok()) return Fail(std::move(status));
  _bytes_since_mark += frames.size();
  _written_lsn.store(last_lsn, std::memory_order_release);
  if (sync) {
    if (absl::Status status = file.Sync(); !status.ok()) return Fail(std::move(status));
    _synced_lsn.store(last_lsn, std::memory_order_release);
  }
  if (file.size() >= _options.segment_bytes) {
    if (absl::Status status = Rotate(); !status.ok()) return Fail(std::move(status));
  }
  return absl::OkStatus();
}

absl::Status Wal::WriteSyncMark(WritableFile& file, uint64_t synced) {
  std::string mark;
  const size_t begin = BeginFrame(mark, FrameType::kSyncMark);
  PutFixed64(mark, synced);
  EndFrame(mark, begin);
  RETURN_IF_ERROR(file.Append(mark));
  _marked_lsn = synced;
  _bytes_since_mark = 0;
  return absl::OkStatus();
}

absl::Status Wal::Close() {
  RETURN_IF_ERROR(Sync());
  if (_current == nullptr) return absl::OkStatus();
  RETURN_IF_ERROR(WriteSyncMark(*_current, _synced_lsn.load(std::memory_order_acquire)));
  RETURN_IF_ERROR(_current->Sync());
  return _current->ReleaseReserved();
}

absl::Status Wal::Rotate() {
  std::lock_guard lock(_mutex);
  if (!_failure.ok()) return _failure;
  const uint64_t written = _written_lsn.load(std::memory_order_acquire);
  // Nothing after the header yet: this is already the newest segment.
  if (_segments.back().first_lsn == written + 1) return absl::OkStatus();
  return RollLocked();
}

absl::Status Wal::RollLocked() {
  const uint64_t written = _written_lsn.load(std::memory_order_acquire);
  RETURN_IF_ERROR(_current->Sync());
  uint64_t synced = _synced_lsn.load(std::memory_order_relaxed);
  while (synced < written && !_synced_lsn.compare_exchange_weak(synced, written)) {
  }
  // Before the next segment exists, as only the last one is expected to end in anything but
  // commits.
  RETURN_IF_ERROR(_current->ReleaseReserved());
  _segments.back().bytes = _current->size();
  return OpenSegmentLocked(written + 1);
}

absl::Status Wal::AddCollection(uint32_t id, std::string name) {
  absl::Status status;
  {
    std::lock_guard lock(_mutex);
    _collections.emplace_back(id, std::move(name));
    if (_current == nullptr) return absl::OkStatus();  // The first segment will name it.
    if (!_failure.ok()) return _failure;
    status = RollLocked();
  }
  return status.ok() ? status : Fail(std::move(status));
}

absl::Status Wal::Sync() {
  std::shared_ptr<WritableFile> file;
  uint64_t written;
  {
    // Together: every commit up to `written` is in this file, or in one Rotate already synced.
    // Later appends are not covered by this sync.
    std::lock_guard lock(_mutex);
    if (!_failure.ok()) return _failure;
    file = _current;
    written = _written_lsn.load(std::memory_order_acquire);
  }
  uint64_t synced = _synced_lsn.load(std::memory_order_acquire);
  if (written <= synced) return absl::OkStatus();
  if (absl::Status status = file->Sync(); !status.ok()) return Fail(std::move(status));
  while (synced < written && !_synced_lsn.compare_exchange_weak(synced, written)) {
  }
  return absl::OkStatus();
}

absl::Status Wal::Purge(uint64_t lsn, uint64_t retain_bytes) {
  std::vector<std::string> removed;
  {
    std::lock_guard lock(_mutex);
    uint64_t covered = 0;
    size_t removable = 0;
    for (size_t i = 0; i + 1 < _segments.size(); ++i) {
      if (_segments[i + 1].first_lsn > lsn + 1) break;
      covered += _segments[i].bytes;
      ++removable;
    }
    size_t count = 0;
    while (count < removable && covered - _segments[count].bytes >= retain_bytes) {
      covered -= _segments[count].bytes;
      removed.push_back(_segments[count].path);
      ++count;
    }
    _segments.erase(_segments.begin(), _segments.begin() + static_cast<ptrdiff_t>(count));
  }
  for (const std::string& path : removed) RETURN_IF_ERROR(_options.file_system->Remove(path));
  if (!removed.empty()) RETURN_IF_ERROR(_options.file_system->SyncDirectory(_options.dir));
  return absl::OkStatus();
}

absl::Status Wal::RemoveAll() {
  std::lock_guard lock(_mutex);
  if (!_failure.ok()) return _failure;
  _current.reset();
  while (!_segments.empty()) {
    RETURN_IF_ERROR(_options.file_system->Remove(_segments.back().path));
    _segments.pop_back();
    // One at a time: without it, the disk can keep any of these removals and not the others.
    RETURN_IF_ERROR(_options.file_system->SyncDirectory(_options.dir));
  }
  return absl::OkStatus();
}

uint64_t Wal::total_bytes() const {
  std::lock_guard lock(_mutex);
  uint64_t total = 0;
  for (size_t i = 0; i + 1 < _segments.size(); ++i) total += _segments[i].bytes;
  return total + (_current != nullptr ? _current->size() : 0);
}

std::string Wal::NameOf(uint32_t collection_id) const {
  for (const auto& [id, name] : _seen_collections) {
    if (id == collection_id) return name;
  }
  return absl::StrCat("#", collection_id);
}

std::optional<std::string> Wal::SegmentFor(uint64_t lsn) const {
  std::lock_guard lock(_mutex);
  for (auto it = _segments.rbegin(); it != _segments.rend(); ++it) {
    if (it->first_lsn <= lsn) return it->path;
  }
  return std::nullopt;
}

absl::Status WalCursor::Open(const std::string& path) {
  FileSystem& file_system = *_wal._options.file_system;
  auto reader = FrameFileReader::Open(file_system, path);
  if (!reader.ok()) return reader.status();
  _reader = std::move(*reader);
  _reader_path = path;
  return absl::OkStatus();
}

absl::StatusOr<bool> WalCursor::Read(uint64_t* position, uint64_t limit, size_t max_bytes,
                                     std::string* out) {
  const auto locate = [this](uint64_t lsn) -> absl::StatusOr<bool> {
    const std::optional<std::string> path = _wal.SegmentFor(lsn);
    if (!path.has_value()) return false;
    if (_reader == nullptr || *path != _reader_path) RETURN_IF_ERROR(Open(*path));
    return true;
  };

  if (_reader == nullptr || _next_lsn != *position + 1) {
    _reader.reset();
    auto found = locate(*position + 1);
    if (!found.ok() || !*found) return found;
    _next_lsn = *position + 1;
  }
  const size_t start_size = out->size();
  while (_next_lsn <= limit && out->size() - start_size < max_bytes) {
    Frame frame;
    ParseStatus status = _reader->Peek(&frame);
    RETURN_IF_ERROR(_reader->status());
    bool at_zeros = false;
    if (status == ParseStatus::kCorrupt) {
      // What it read ahead may have been zeros of a reservation, or part of a commit still being
      // written, which by now are what the commit it is after: only reading again can tell.
      _reader->Reread();
      status = _reader->Peek(&frame);
      RETURN_IF_ERROR(_reader->status());
      at_zeros = status == ParseStatus::kCorrupt && _reader->AtZeros();
      if (status == ParseStatus::kCorrupt && !at_zeros) {
        return absl::DataLossError(absl::StrCat("Damaged segment: ", _reader_path));
      }
    }
    if (status == ParseStatus::kIncomplete || at_zeros) {
      // The end of this segment: the commit it was to hold is in the next one.
      const std::string previous = _reader_path;
      auto found = locate(_next_lsn);
      if (!found.ok() || !*found) return found;
      if (_reader_path == previous) break;
      continue;
    }
    if (frame.type == FrameType::kCommit) {
      CommitDecoder commit(frame.body);
      if (commit.lsn > _next_lsn) {
        return absl::DataLossError(absl::StrCat("Missing LSN ", _next_lsn, " in ", _reader_path));
      }
      if (commit.lsn == _next_lsn) {
        out->append(frame.raw);
        *position = _next_lsn++;
      }
    }
    _reader->Advance();
  }
  return true;
}

}  // namespace gendb::internal
