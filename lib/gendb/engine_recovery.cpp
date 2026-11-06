#include <algorithm>
#include <chrono>
#include <string>
#include <thread>
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
using internal::OrderedIndex;
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
      : _engine(engine),
        _seq(seq),
        _epoch(++engine._install_epoch),
        _changes(changes),
        _opening(seq == 0) {}

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
    if (_opening) {  // Nothing else is in the tables, and indices come later.
      Version* version = Version::Copy(AsBytes(value));
      version->seq = _seq;
      _engine.NodeForWrite(c, hash, key, _seq)->head.store(version, std::memory_order_relaxed);
      return;
    }
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
  const bool _opening;
  LogPosition _position{};
  std::optional<size_t> _current;
};

namespace {

using SortedKey = OrderedIndex::SortedKey;

bool KeyLess(const SortedKey& a, const SortedKey& b) noexcept {
  return std::string_view(a.data, a.size) < std::string_view(b.data, b.size);
}

// Runs fn(0), ..., fn(count - 1), each on a thread of its own but the last.
template <typename F>
void RunOnThreads(size_t count, const F& fn) {
  std::vector<std::thread> threads;
  threads.reserve(count - 1);
  for (size_t i = 0; i + 1 < count; ++i) threads.emplace_back(fn, i);
  fn(count - 1);
  for (std::thread& thread : threads) thread.join();
}

}  // namespace

void Engine::BuildIndices(uint64_t seq) {
  constexpr size_t kRecordsPerThread = 16384;
  unsigned threads = _options.recovery_threads;
  if (threads == 0) threads = std::min(std::max(1u, std::thread::hardware_concurrency()), 8u);

  for (size_t i = 0; i < _indices.size(); ++i) {
    const IndexDef& def = _schema.indices[i];
    const RecordTable& table = _collections[def.collection]->table;
    const RecordTable::Slots* slots = table.LoadSlots();
    const size_t capacity = slots->mask + 1;
    const size_t workers = std::clamp<size_t>(table.live() / kRecordsPerThread, 1, threads);

    // Each worker encodes the keys of its part of the table and sorts them.
    struct Part {
      std::string arena;
      std::vector<std::pair<size_t, uint32_t>> ends;  // Offset of the end, and secondary size.
      std::vector<SortedKey> keys;
    };
    std::vector<Part> parts(workers);
    RunOnThreads(workers, [&](size_t w) {
      Part& part = parts[w];
      for (size_t slot = capacity * w / workers; slot < capacity * (w + 1) / workers; ++slot) {
        const RecordNode* node = slots->slots[slot].load(std::memory_order_relaxed);
        if (node == nullptr || node == RecordTable::Erased()) continue;
        const Version* head = node->head.load(std::memory_order_relaxed);
        if (head == nullptr || head->tombstone) continue;
        const size_t start = part.arena.size();
        if (!def.extract(head->bytes(), part.arena)) {
          part.arena.resize(start);
          continue;
        }
        const auto secondary = static_cast<uint32_t>(part.arena.size() - start);
        part.arena.append(node->key());
        part.ends.emplace_back(part.arena.size(), secondary);
      }
      part.keys.reserve(part.ends.size());
      size_t start = 0;
      for (const auto& [end, secondary] : part.ends) {
        part.keys.push_back(
            {part.arena.data() + start, static_cast<uint32_t>(end - start), secondary});
        start = end;
      }
      std::sort(part.keys.begin(), part.keys.end(), KeyLess);
    });

    // Then the sorted parts merge, in pairs, on as many threads as there are pairs.
    std::vector<SortedKey> keys;
    std::vector<size_t> bounds{0};
    size_t total = 0;
    for (const Part& part : parts) total += part.keys.size();
    keys.reserve(total);
    for (const Part& part : parts) {
      keys.insert(keys.end(), part.keys.begin(), part.keys.end());
      bounds.push_back(keys.size());
    }
    while (bounds.size() > 2) {
      const size_t pairs = (bounds.size() - 1) / 2;
      RunOnThreads(pairs, [&](size_t p) {
        std::inplace_merge(keys.begin() + static_cast<ptrdiff_t>(bounds[p * 2]),
                           keys.begin() + static_cast<ptrdiff_t>(bounds[p * 2 + 1]),
                           keys.begin() + static_cast<ptrdiff_t>(bounds[p * 2 + 2]), KeyLess);
      });
      std::vector<size_t> merged;
      for (size_t b = 0; b < bounds.size(); b += 2) merged.push_back(bounds[b]);
      if (merged.back() != bounds.back()) merged.push_back(bounds.back());
      bounds = std::move(merged);
    }
    _indices[i]->BuildSorted(keys, seq);
  }
}

absl::Status Engine::Recover() {
  const auto started = std::chrono::steady_clock::now();
  const std::string& path = _options.path;
  RETURN_IF_ERROR(_file_system->CreateDirectories(path));

  auto lock = _file_system->Lock(absl::StrCat(path, "/LOCK"));
  if (!lock.ok()) return lock.status();
  _file_lock = std::move(*lock);

  // Unfinished work of a previous run.
  auto names_on_disk = _file_system->List(path);
  if (!names_on_disk.ok()) return names_on_disk.status();
  for (const std::string& name : *names_on_disk) {
    if (name.ends_with(kTempSuffix)) {
      RETURN_IF_ERROR(_file_system->Remove(absl::StrCat(path, "/", name)));
    }
  }

  internal::CollectionNames names;
  for (size_t c = 0; c < collection_count(); ++c) {
    if (!_collections[c]->ephemeral) names.emplace_back(_collections[c]->id, _collections[c]->name);
  }
  _wal = std::make_unique<Wal>(
      Wal::Options{_file_system, path, _options.wal_segment_bytes, std::move(names)});

  _indices_deferred = true;
  auto checkpoints =
      internal::ListNumbered(*_file_system, path, kCheckpointPrefix, kCheckpointSuffix);
  if (!checkpoints.ok()) return checkpoints.status();
  std::optional<HistoryId> history;
  uint64_t lsn = 0;
  if (!checkpoints->empty()) {
    lsn = checkpoints->back();
    const std::string file = NumberedPath(path, kCheckpointPrefix, lsn, kCheckpointSuffix);
    auto reader = FrameFileReader::Open(*_file_system, file);
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
    auto file_size = _file_system->FileSize(file);
    if (!file_size.ok()) return file_size.status();
    _checkpoint_bytes.store(*file_size, std::memory_order_relaxed);
    Reclaim(lsn);  // Nothing has read this engine yet.
  }

  RecoveryInfo info{};
  {
    Transaction txn(*this);
    auto recovered = _wal->Recover(
        lsn, [this, &txn](const Frame& frame) { return ApplyCommitFrame(txn, frame); });
    if (!recovered.ok()) return recovered.status();
    lsn = recovered->last_lsn;
    if (recovered->history.has_value()) history = recovered->history;
    _wal_bytes_since_checkpoint.store(recovered->bytes_replayed, std::memory_order_relaxed);
    info.commits_replayed = recovered->commits_replayed;
    info.log_bytes_replayed = recovered->bytes_replayed;
    info.torn_bytes_cut = recovered->torn_bytes_cut;
  }
  _indices_deferred = false;
  BuildIndices(_committed_seq.load(std::memory_order_relaxed));
  _history = history.value_or(_history);
  RETURN_IF_ERROR(_wal->StartSegment(_history, lsn + 1));
  RETURN_IF_ERROR(PurgeWal());

  if (info.torn_bytes_cut > 0) {
    Log(LogLevel::kWarning, absl::StrCat("Cut ", info.torn_bytes_cut,
                                         " bytes of an unfinished write from the end of the log"));
  }
  if (_options.observer != nullptr) {
    info.lsn = lsn;
    info.duration = std::chrono::steady_clock::now() - started;
    _options.observer->OnRecovery(info);
  }
  return absl::OkStatus();
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
        txn.PutUnchecked(*collection, AsBytes(entry.key), AsBytes(entry.value));
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
  if (_wal == nullptr) return CheckpointStage::Create(nullptr, "");
  return CheckpointStage::Create(_file_system,
                                 absl::StrCat(_options.path, "/incoming", kTempSuffix));
}

absl::Status Engine::InstallCheckpoint(CheckpointStage& stage, const LogPosition& position) {
  std::lock_guard serial(_checkpoint_mutex);
  auto writer = AcquireWriterFirst();

  std::string final_path;
  if (_wal != nullptr) {
    // A crash at any point leaves a state from which the replica recovers and asks again.
    final_path = NumberedPath(_options.path, kCheckpointPrefix, position.lsn, kCheckpointSuffix);
    absl::Status files = _wal->RemoveAll();
    if (files.ok()) files = stage.Publish(final_path);
    if (files.ok()) {
      RemoveCheckpointsBelow(position.lsn);
      files = _wal->StartSegment(position.history, position.lsn + 1);
    }
    if (!files.ok()) return _wal->Fail(std::move(files));
    auto file_size = _file_system->FileSize(final_path);
    _checkpoint_bytes.store(file_size.value_or(0), std::memory_order_relaxed);
    _checkpoint_lsn.store(position.lsn, std::memory_order_relaxed);
    _wal_bytes_since_checkpoint.store(0, std::memory_order_relaxed);
  }
  auto source = stage.OpenSource(final_path);
  if (!source.ok()) return _wal != nullptr ? _wal->Fail(source.status()) : source.status();

  const uint64_t seq = _committed_seq.load(std::memory_order_relaxed) + 1;
  std::vector<Change> changes;
  CheckpointLoader loader(*this, seq, _subscribers.empty() ? nullptr : &changes);
  // Every frame was verified on arrival, so this can fail only if the disk fails right now, with
  // the old state already gone. Reads keep the old state, and a restart recovers from the file.
  if (absl::Status status = internal::ReadCheckpoint(**source, loader); !status.ok()) {
    _broken.store(true, std::memory_order_release);
    Log(LogLevel::kError, absl::StrCat("Installing a checkpoint: ", status.ToString()));
    return _wal != nullptr ? _wal->Fail(std::move(status)) : status;
  }
  loader.RemoveMissing();

  _history = position.history;
  _history_generation.fetch_add(1, std::memory_order_acq_rel);
  if (_digest_tracked.load(std::memory_order_relaxed)) {
    // Not the head versions' old values: the new versions are linked, though not yet visible.
    _state_digest.store(ComputeStateDigest(), std::memory_order_relaxed);
  }
  _ring->Reset(position.lsn, _state_digest.load(std::memory_order_relaxed));
  _lsn.store(position.lsn, std::memory_order_release);
  _committed_seq.store(seq, std::memory_order_seq_cst);
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
