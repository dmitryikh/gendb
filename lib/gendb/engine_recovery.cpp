#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <utility>

#include "absl/strings/str_cat.h"
#include "gendb/checkpoint.h"
#include "gendb/commit_ring.h"
#include "gendb/engine.h"
#include "gendb/engine_internal.h"
#include "gendb/status.h"
#include "gendb/wal.h"

namespace gendb {

using internal::AsBytes;
using internal::CheckpointStage;
using internal::CommitDecoder;
using internal::Frame;
using internal::FrameFileReader;
using internal::FrameType;
using internal::HashKey;
using internal::HistoryId;
using internal::kCheckpointPrefix;
using internal::kCheckpointSuffix;
using internal::kTempSuffix;
using internal::NumberedPath;
using internal::ParseStatus;
using internal::RecordNode;
using internal::RecordTable;
using internal::SameBytes;
using internal::Version;
using internal::Wal;

absl::StatusOr<std::unique_ptr<Engine>> Engine::Open(EngineSchema schema, EngineOptions options) {
  if (options.path.empty()) return absl::InvalidArgumentError("EngineOptions::path is empty");
  if (options.replica && options.discard_unknown_collections) {
    return absl::InvalidArgumentError("A replica must keep everything its primary has");
  }
  std::unique_ptr<Engine> engine(new Engine(std::move(schema), std::move(options)));
  RETURN_IF_ERROR(engine->Recover());
  const EngineOptions& opened = engine->_options;
  if (opened.checkpoint_wal_bytes > 0 || opened.background_sync_interval.count() > 0) {
    engine->_background = std::thread([raw = engine.get()] { raw->BackgroundLoop(); });
  }
  return engine;
}

// Loads a checkpoint into the live tables under one sequence number, so that readers keep seeing
// the old state until it is published. A record that did not change keeps its version, and any
// durable record the checkpoint lacks is removed.
class Engine::CheckpointLoader final : public internal::CheckpointHandler {
 public:
  // With `seq` 0, the engine is recovering, and uses the checkpoint's LSN.
  CheckpointLoader(Engine& engine, uint64_t seq, std::vector<Change>* changes)
      : _engine(engine), _seq(seq), _epoch(++engine._install_epoch), _changes(changes) {}

  uint64_t seq() const noexcept { return _seq; }
  const LogPosition& position() const noexcept { return _position; }

  absl::Status OnHeader(const HistoryId& history, uint64_t lsn) override {
    _position = {history, lsn};
    if (_seq == 0) _seq = lsn;
    return absl::OkStatus();
  }

  absl::Status OnCollection(uint32_t id, std::string_view name, uint64_t expected) override {
    _current = _engine.FindDurableCollection(id);
    if (!_current.has_value()) {
      if (_engine._options.discard_unknown_collections) {
        _engine._rewrite_checkpoint.store(true, std::memory_order_relaxed);
        return absl::OkStatus();
      }
      auto foreign = _engine.AddForeignCollection(id, name);
      if (!foreign.ok()) return foreign.status();
      _current = *foreign;
    }
    if (_current.has_value()) {
      const RecordTable::Slots* replaced = _engine._collections[*_current]->table.Reserve(expected);
      if (replaced != nullptr) {
        _engine._garbage.push_back(
            {_seq, Garbage::kSlots, const_cast<RecordTable::Slots*>(replaced), 0});
      }
    }
    return absl::OkStatus();
  }

  void OnRecord(std::string_view key, std::string_view value) override {
    if (!_current.has_value()) return;
    const size_t c = *_current;
    const uint64_t hash = HashKey(key);
    RecordNode* node = _engine._collections[c]->table.Find(hash, key);
    Version* head = node == nullptr ? nullptr : node->head.load(std::memory_order_relaxed);
    const bool existed = head != nullptr && !head->tombstone;
    if (existed && SameBytes(head->bytes(), value)) {
      node->install_epoch = _epoch;
      return;
    }
    if (node == nullptr) node = _engine.NodeForWrite(c, hash, key, _seq);
    Version* version = Version::Copy(AsBytes(value));
    Link(c, node, version, head);
    if (existed) {
      _engine.UpdateIndexNow(c, node->key(), head->bytes(), version->bytes(), _seq);
    } else {
      _engine.UpdateIndexNow(c, node->key(), std::nullopt, version->bytes(), _seq);
    }
  }

  // Removes durable records that were not in the checkpoint.
  void RemoveMissing() {
    for (size_t c = 0; c < _engine.collection_count(); ++c) {
      if (_engine._collections[c]->ephemeral) continue;
      const RecordTable::Slots* slots = _engine._collections[c]->table.LoadSlots();
      for (size_t i = 0; i <= slots->mask; ++i) {
        RecordNode* node = slots->slots[i].load(std::memory_order_relaxed);
        if (node == nullptr || node == RecordTable::Erased()) continue;
        if (node->install_epoch == _epoch) continue;
        Version* head = node->head.load(std::memory_order_relaxed);
        if (head == nullptr || head->tombstone) continue;
        Link(c, node, Version::Create(0, true), head);
        _engine.UpdateIndexNow(c, node->key(), head->bytes(), std::nullopt, _seq);
      }
    }
  }

 private:
  void Link(size_t collection, RecordNode* node, Version* version, Version* head) {
    version->seq = _seq;
    version->older = head;
    node->head.store(version, std::memory_order_release);
    node->install_epoch = _epoch;
    if (head != nullptr) _engine._garbage.push_back({_seq, Garbage::kOlderVersion, version, 0});
    if (version->tombstone) {
      _engine._garbage.push_back({_seq, Garbage::kTombstone, node, collection});
    }
    if (_changes != nullptr && collection < _engine._schema.collections.size()) {
      const bool had = head != nullptr && !head->tombstone;
      _changes->push_back(
          {collection, AsBytes(node->key()),
           had ? std::optional<BytesConstView>(head->bytes()) : std::nullopt,
           version->tombstone ? std::nullopt : std::optional<BytesConstView>(version->bytes())});
    }
  }

  Engine& _engine;
  uint64_t _seq;
  uint32_t _epoch;
  std::vector<Change>* _changes;
  LogPosition _position{};
  std::optional<size_t> _current;
};

absl::Status Engine::Recover() {
  const std::string& path = _options.path;
  std::error_code error;
  std::filesystem::create_directories(path, error);
  if (error) return absl::InternalError(absl::StrCat("create ", path, ": ", error.message()));

  const std::string lock_path = absl::StrCat(path, "/LOCK");
  _lock_fd = open(lock_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
  if (_lock_fd < 0) return internal::ErrnoStatus("open", lock_path);
  if (flock(_lock_fd, LOCK_EX | LOCK_NB) != 0) {
    return absl::FailedPreconditionError(absl::StrCat(path, " is open in another engine"));
  }

  // Unfinished work of a previous run.
  for (const auto& entry : std::filesystem::directory_iterator(path, error)) {
    if (entry.path().string().ends_with(kTempSuffix)) std::filesystem::remove(entry.path(), error);
  }

  internal::CollectionNames names;
  for (size_t c = 0; c < collection_count(); ++c) {
    if (!_collections[c]->ephemeral) names.emplace_back(_collections[c]->id, _collections[c]->name);
  }
  _wal = std::make_unique<Wal>(Wal::Options{path, _options.wal_segment_bytes, std::move(names)});

  auto checkpoints = internal::ListNumbered(path, kCheckpointPrefix, kCheckpointSuffix);
  if (!checkpoints.ok()) return checkpoints.status();
  std::optional<HistoryId> history;
  uint64_t lsn = 0;
  if (!checkpoints->empty()) {
    lsn = checkpoints->back();
    const std::string file = NumberedPath(path, kCheckpointPrefix, lsn, kCheckpointSuffix);
    auto reader = FrameFileReader::Open(file);
    if (!reader.ok()) return reader.status();
    CheckpointLoader loader(*this, /*seq=*/0, /*changes=*/nullptr);
    if (absl::Status status = internal::ReadCheckpoint(**reader, loader); !status.ok()) {
      return absl::Status(status.code(), absl::StrCat(file, ": ", status.message()));
    }
    if (loader.position().lsn != lsn) return absl::DataLossError(absl::StrCat("LSN of ", file));
    history = loader.position().history;
    _lsn.store(lsn, std::memory_order_relaxed);
    _committed_seq.store(lsn, std::memory_order_relaxed);
    _checkpoint_lsn.store(lsn, std::memory_order_relaxed);
    _checkpoint_bytes.store(std::filesystem::file_size(file, error), std::memory_order_relaxed);
    Reclaim(lsn);  // Nothing has read this engine yet.
  }

  {
    Transaction txn(*this);
    auto recovered = _wal->Recover(
        lsn, [this, &txn](const Frame& frame) { return ApplyCommitFrame(txn, frame); });
    if (!recovered.ok()) return recovered.status();
    lsn = recovered->last_lsn;
    if (recovered->history.has_value()) history = recovered->history;
    _wal_bytes_since_checkpoint.store(recovered->bytes_replayed, std::memory_order_relaxed);
  }
  _history = history.value_or(_history);
  RETURN_IF_ERROR(_wal->StartSegment(_history, lsn + 1));
  return PurgeWal();
}

absl::Status Engine::ApplyCommitFrame(Transaction& txn, const Frame& frame) {
  CommitDecoder commit(frame.body);
  for (uint32_t i = 0; i < commit.count; ++i) {
    const internal::CommitEntry entry = commit.Next();
    if (!commit.ok()) return absl::DataLossError("Malformed commit");
    std::optional<size_t> collection = FindDurableCollection(entry.collection_id);
    if (!collection.has_value()) {
      if (_options.discard_unknown_collections) {
        _rewrite_checkpoint.store(true, std::memory_order_relaxed);
        continue;
      }
      auto foreign = AddForeignCollection(entry.collection_id,
                                          _wal != nullptr ? _wal->NameOf(entry.collection_id)
                                                          : absl::StrCat("#", entry.collection_id));
      if (!foreign.ok()) {
        txn.Rollback();
        return foreign.status();
      }
      collection = *foreign;
      txn._pending.resize(collection_count());
    }
    switch (entry.op) {
      case internal::CommitOp::kPut:
        txn.Put(*collection, AsBytes(entry.key), AsBytes(entry.value));
        break;
      case internal::CommitOp::kDelete:
        if (!txn.Delete(*collection, AsBytes(entry.key)).ok()) {
          txn.Rollback();
          return absl::DataLossError("The log deletes a record that does not exist");
        }
        break;
      case internal::CommitOp::kPatch: {
        internal::Decoder value(entry.value);
        const uint32_t result_crc = value.Fixed32();
        const std::optional<MessagePatch> patch =
            value.ok() ? MessagePatch::Decode(value.rest()) : std::nullopt;
        absl::Status status = patch.has_value()
                                  ? txn.ApplyLoggedPatch(*collection, entry.key, *patch, result_crc)
                                  : absl::DataLossError("Malformed patch in the log");
        if (!status.ok()) {
          txn.Rollback();
          return status;
        }
        break;
      }
      default:
        txn.Rollback();
        return absl::DataLossError("Unknown operation in the log");
    }
  }
  txn.CommitReplayed(commit.lsn, frame.raw);
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<CheckpointStage>> Engine::NewCheckpointStage() const {
  if (_wal == nullptr) return CheckpointStage::Create("");
  return CheckpointStage::Create(absl::StrCat(_options.path, "/incoming", kTempSuffix));
}

absl::Status Engine::InstallCheckpoint(CheckpointStage& stage, const LogPosition& position) {
  std::lock_guard serial(_checkpoint_mutex);
  auto writer = AcquireWriterFirst();

  std::string final_path;
  if (_wal != nullptr) {
    // A crash at any point leaves a state from which the replica recovers and asks again.
    final_path = NumberedPath(_options.path, kCheckpointPrefix, position.lsn, kCheckpointSuffix);
    RETURN_IF_ERROR(_wal->RemoveAll());
    RETURN_IF_ERROR(stage.Publish(final_path));
    RemoveCheckpointsBelow(position.lsn);
    RETURN_IF_ERROR(_wal->StartSegment(position.history, position.lsn + 1));
    std::error_code error;
    _checkpoint_bytes.store(std::filesystem::file_size(final_path, error),
                            std::memory_order_relaxed);
    _checkpoint_lsn.store(position.lsn, std::memory_order_relaxed);
    _wal_bytes_since_checkpoint.store(0, std::memory_order_relaxed);
  }
  auto source = stage.OpenSource(final_path);
  if (!source.ok()) return source.status();

  const uint64_t seq = _committed_seq.load(std::memory_order_relaxed) + 1;
  std::vector<Change> changes;
  CheckpointLoader loader(*this, seq, _subscribers.empty() ? nullptr : &changes);
  // Every frame was verified on arrival and the schema checked up front, so this can fail only if
  // the disk fails right now, with the old state already gone: restart and recover from the file.
  if (absl::Status status = internal::ReadCheckpoint(**source, loader); !status.ok()) {
    std::fprintf(stderr, "gendb: cannot install a checkpoint: %s\n", status.ToString().c_str());
    std::abort();
  }
  loader.RemoveMissing();

  _history = position.history;
  _history_generation.fetch_add(1, std::memory_order_acq_rel);
  _ring->Reset(position.lsn);
  _lsn.store(position.lsn, std::memory_order_release);
  _committed_seq.store(seq, std::memory_order_release);
  Notify(seq, changes);
  ReclaimUnpinned(seq);
  return absl::OkStatus();
}

absl::Status Engine::ApplyReplicated(std::string_view frames) {
  Transaction txn(*this);
  // Check everything first: what reaches the log must apply.
  uint64_t next = _lsn.load(std::memory_order_relaxed) + 1;
  std::string_view rest = frames;
  while (!rest.empty()) {
    Frame frame;
    if (internal::ParseFrame(rest, &frame) != ParseStatus::kOk ||
        frame.type != FrameType::kCommit) {
      return absl::DataLossError("Damaged commit from the primary");
    }
    CommitDecoder commit(frame.body);
    if (!commit.ok() || commit.lsn != next) {
      return absl::DataLossError(absl::StrCat("Expected commit ", next, " from the primary"));
    }
    for (uint32_t i = 0; i < commit.count; ++i) {
      const internal::CommitEntry entry = commit.Next();
      if (!commit.ok()) return absl::DataLossError("Malformed commit from the primary");
      // Named at the start of a session, so this primary's list grew since: a new session will
      // bring the name.
      if (!FindDurableCollection(entry.collection_id).has_value()) {
        return absl::AbortedError("The primary has a collection it has not named yet");
      }
    }
    ++next;
    rest.remove_prefix(frame.raw.size());
  }
  if (_wal != nullptr) {
    RETURN_IF_ERROR(_wal->Append(frames, next - 1, _options.sync_commits));
  }
  rest = frames;
  while (!rest.empty()) {
    Frame frame;
    (void)internal::ParseFrame(rest, &frame);
    RETURN_IF_ERROR(ApplyCommitFrame(txn, frame));
    rest.remove_prefix(frame.raw.size());
  }
  AfterCommit(frames.size());
  return absl::OkStatus();
}

}  // namespace gendb
