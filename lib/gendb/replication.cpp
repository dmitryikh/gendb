#include "gendb/replication.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "absl/strings/str_cat.h"
#include "gendb/checkpoint.h"
#include "gendb/log_format.h"
#include "gendb/status.h"
#include "gendb/wal.h"

// The session, on top of Connection:
//
//   replica                         primary
//   Hello (history, LSN)  -------->
//                         <--------  Welcome (position, mode, collections) or Error
//                         <--------  CheckpointData*   if the replica cannot continue its log
//   Ack                   -------->
//                         <--------  LogData* or Heartbeat, forever
//   Ack                   -------->
//
// CheckpointData and LogData carry a byte stream of frames, cut anywhere: a frame can span messages
// and be larger than a message. Replicas keep the primary's frames byte for byte.
namespace gendb {
namespace {

using internal::Decoder;
using internal::Frame;
using internal::FrameType;
using internal::HistoryId;
using internal::ParseStatus;
using internal::PutFixed32;
using internal::PutFixed64;
using internal::PutHistory;
using internal::PutString;
using internal::PutVarint;

// Commit frames travel as they are, so this changes with what a frame can hold.
constexpr uint32_t kProtocolVersion = 2;
constexpr size_t kDataBytesPerMessage = size_t{1} << 20;
constexpr auto kPollInterval = std::chrono::milliseconds(100);

enum class MessageType : uint8_t {
  kHello = 1,
  kWelcome = 2,
  kError = 3,
  kCheckpointData = 4,
  kLogData = 5,
  kHeartbeat = 6,
  kAck = 7,
};

std::string NewMessage(MessageType type) {
  std::string message;
  message.push_back(static_cast<char>(type));
  return message;
}

// [type] [primary LSN] [piece of the stream]
absl::Status SendData(Connection& connection, MessageType type, uint64_t primary_lsn,
                      std::string_view bytes, std::string& scratch) {
  while (!bytes.empty()) {
    const std::string_view piece = bytes.substr(0, kDataBytesPerMessage);
    scratch.clear();
    scratch.push_back(static_cast<char>(type));
    PutFixed64(scratch, primary_lsn);
    scratch.append(piece);
    RETURN_IF_ERROR(connection.Send(scratch));
    bytes.remove_prefix(piece.size());
  }
  return absl::OkStatus();
}

absl::Status SendAck(Connection& connection, uint64_t lsn) {
  std::string message = NewMessage(MessageType::kAck);
  PutFixed64(message, lsn);
  return connection.Send(message);
}

// The bytes of the whole frames at the start of `buffer`.
size_t CompleteFramesSize(std::string_view buffer) {
  size_t total = 0;
  while (const size_t size = internal::CompleteFrameSize(buffer.substr(total))) total += size;
  return total;
}

template <typename F>
struct ScopeGuard {
  F fn;
  ~ScopeGuard() { fn(); }
};
template <typename F>
ScopeGuard(F) -> ScopeGuard<F>;

}  // namespace

struct ReplicationServer::Session {
  Connection* connection;
  uint64_t id = 0;
  std::atomic<uint64_t> sent_lsn{0};
  std::atomic<uint64_t> acked_lsn{0};
  std::atomic<bool> receiving_checkpoint{false};
  std::atomic<bool> stop{false};
  std::atomic<std::chrono::steady_clock::rep> last_heard{0};

  void Heard() {
    last_heard.store(std::chrono::steady_clock::now().time_since_epoch().count(),
                     std::memory_order_relaxed);
  }
  bool SilentFor(std::chrono::milliseconds timeout) const {
    const std::chrono::steady_clock::duration last(last_heard.load(std::memory_order_relaxed));
    return std::chrono::steady_clock::now().time_since_epoch() - last > timeout;
  }
};

ReplicationServer::ReplicationServer(Engine& engine, ReplicationServerOptions options)
    : _engine(engine), _options(options) {
  _engine.EnableCommitRing(_options.commit_ring_bytes);
}

ReplicationServer::~ReplicationServer() {
  Shutdown();
}

void ReplicationServer::Shutdown() {
  std::unique_lock lock(_mutex);
  _shutdown = true;
  for (Session* session : _sessions) {
    session->stop.store(true, std::memory_order_release);
    session->connection->Close();
  }
  _engine.WakeLogWaiters();
  _sessions_cv.wait(lock, [this] { return _sessions.empty(); });
}

std::vector<ConnectedReplica> ReplicationServer::Replicas() const {
  std::lock_guard lock(_mutex);
  std::vector<ConnectedReplica> replicas;
  for (const Session* session : _sessions) {
    replicas.push_back({session->id, session->sent_lsn.load(std::memory_order_relaxed),
                        session->acked_lsn.load(std::memory_order_relaxed),
                        session->receiving_checkpoint.load(std::memory_order_relaxed)});
  }
  return replicas;
}

absl::Status ReplicationServer::Serve(Connection& connection) {
  Session session;
  session.connection = &connection;
  session.Heard();
  {
    std::lock_guard lock(_mutex);
    if (_shutdown) {
      connection.Close();
      return absl::CancelledError("The replication server is shut down");
    }
    session.id = _next_session_id++;
    _sessions.push_back(&session);
  }
  absl::Status status = RunSession(connection, session);
  connection.Close();
  {
    std::lock_guard lock(_mutex);
    std::erase(_sessions, &session);
  }
  _sessions_cv.notify_all();
  return status;
}

absl::Status ReplicationServer::RunSession(Connection& connection, Session& session) {
  std::string message;
  RETURN_IF_ERROR(connection.Receive(&message));
  session.Heard();

  Decoder hello(message);
  const auto type = static_cast<MessageType>(hello.Byte());
  const uint32_t version = hello.Fixed32();
  const HistoryId replica_history = hello.History();
  const uint64_t replica_lsn = hello.Fixed64();
  const bool want_checkpoint = hello.Byte() != 0;
  if (!hello.ok() || type != MessageType::kHello) {
    return absl::InvalidArgumentError("Expected a Hello");
  }
  if (version != kProtocolVersion) {
    std::string error = NewMessage(MessageType::kError);
    PutString(error, absl::StrCat("The primary speaks protocol ", kProtocolVersion));
    (void)connection.Send(error);
    return absl::FailedPreconditionError("Unsupported replication protocol");
  }

  // Under the writer lock, the snapshot, the LSN and the log position are one point in history.
  Engine::LogPosition position;
  std::optional<EngineSnapshot> snapshot;
  uint64_t after;
  uint64_t pin;
  uint64_t generation;
  {
    auto writer = _engine.AcquireWriterFirst();
    position = {_engine._history, _engine._lsn.load(std::memory_order_relaxed)};
    generation = _engine.history_generation();
    // A replica ahead of this primary holds commits that no longer exist here.
    const bool can_continue = !want_checkpoint && replica_history == position.history &&
                              replica_lsn <= position.lsn && _engine.CanReadLogAfter(replica_lsn);
    after = can_continue ? replica_lsn : position.lsn;
    pin = _engine.PinWal(after);
    if (!can_continue) snapshot.emplace(_engine);
  }
  ScopeGuard unpin{[&] { _engine.UnpinWal(pin); }};
  session.sent_lsn.store(after, std::memory_order_relaxed);
  session.acked_lsn.store(after, std::memory_order_relaxed);
  session.receiving_checkpoint.store(snapshot.has_value(), std::memory_order_relaxed);

  std::string welcome = NewMessage(MessageType::kWelcome);
  PutHistory(welcome, position.history);
  PutFixed64(welcome, after);
  welcome.push_back(snapshot.has_value() ? 1 : 0);
  // All it needs to keep what it does not know: the records then travel like any others.
  const size_t collections = _engine.collection_count();
  size_t durable_collections = 0;
  for (size_t c = 0; c < collections; ++c) {
    if (!_engine._collections[c]->ephemeral) ++durable_collections;
  }
  PutVarint(welcome, durable_collections);
  for (size_t c = 0; c < collections; ++c) {
    if (_engine._collections[c]->ephemeral) continue;
    PutFixed32(welcome, _engine._collections[c]->id);
    PutString(welcome, _engine._collections[c]->name);
  }
  RETURN_IF_ERROR(connection.Send(welcome));

  // Acks arrive on their own thread: a blocked Send must not stop us hearing them.
  std::thread ack_reader([this, &connection, &session, pin] {
    std::string ack;
    while (connection.Receive(&ack).ok()) {
      session.Heard();
      Decoder decoder(ack);
      const auto ack_type = static_cast<MessageType>(decoder.Byte());
      const uint64_t lsn = decoder.Fixed64();
      if (!decoder.ok() || ack_type != MessageType::kAck) break;
      // During a checkpoint the replica reports its old position, which means nothing here.
      if (lsn <= session.acked_lsn.load(std::memory_order_relaxed)) continue;
      session.acked_lsn.store(lsn, std::memory_order_relaxed);
      _engine.UpdateWalPin(pin, lsn);
    }
    session.stop.store(true, std::memory_order_release);
    _engine.WakeLogWaiters();
  });
  ScopeGuard join_reader{[&] {
    connection.Close();
    ack_reader.join();
  }};

  std::string scratch;
  if (snapshot.has_value()) {
    RETURN_IF_ERROR(
        _engine.ExportCheckpoint(*snapshot, position, [&](std::string_view frames) -> absl::Status {
          if (session.stop.load(std::memory_order_acquire)) {
            return absl::CancelledError("The session ended");
          }
          return SendData(connection, MessageType::kCheckpointData, position.lsn, frames, scratch);
        }));
    snapshot.reset();
    session.receiving_checkpoint.store(false, std::memory_order_relaxed);
  }

  std::unique_ptr<internal::WalCursor> cursor;
  std::string batch;
  auto last_sent = std::chrono::steady_clock::now();
  while (!session.stop.load(std::memory_order_acquire)) {
    if (_engine.history_generation() != generation) {
      return absl::AbortedError("The primary's history or collections changed");
    }
    if (session.SilentFor(_options.replica_timeout)) {
      return absl::DeadlineExceededError("The replica went silent");
    }
    batch.clear();
    auto read = _engine.ReadLog(&after, kDataBytesPerMessage, &batch, &cursor);
    if (!read.ok()) return read.status();
    const uint64_t primary_lsn = _engine._lsn.load(std::memory_order_acquire);
    if (!batch.empty()) {
      RETURN_IF_ERROR(SendData(connection, MessageType::kLogData, primary_lsn, batch, scratch));
      session.sent_lsn.store(after, std::memory_order_relaxed);
      last_sent = std::chrono::steady_clock::now();
      continue;
    }
    if (std::chrono::steady_clock::now() - last_sent >= _options.heartbeat_interval) {
      std::string heartbeat = NewMessage(MessageType::kHeartbeat);
      PutFixed64(heartbeat, primary_lsn);
      RETURN_IF_ERROR(connection.Send(heartbeat));
      last_sent = std::chrono::steady_clock::now();
    }
    _engine.WaitForLog(
        after, std::min<std::chrono::milliseconds>(_options.heartbeat_interval, kPollInterval));
  }
  return absl::CancelledError("The session ended");
}

Replica::Replica(Engine& engine, Connector connector, ReplicaOptions options)
    : _engine(engine), _connector(std::move(connector)), _options(options) {
  _engine.BecomeReplica();
  _thread = std::thread([this] { Run(); });
  _watchdog = std::thread([this] { Watchdog(); });
}

Replica::~Replica() {
  Stop();
}

void Replica::Stop() {
  {
    std::lock_guard lock(_mutex);
    _stopped = true;
    if (_connection != nullptr) _connection->Close();
  }
  _cv.notify_all();
  if (_thread.joinable()) _thread.join();
  if (_watchdog.joinable()) _watchdog.join();
}

ReplicaStatus Replica::Status() const {
  std::lock_guard lock(_mutex);
  return {_state,
          _engine._lsn.load(std::memory_order_acquire),
          _primary_lsn,
          _connections,
          _checkpoints_installed,
          _last_error};
}

bool Replica::WaitForLsn(uint64_t lsn, std::chrono::milliseconds timeout) const {
  std::unique_lock lock(_mutex);
  return _cv.wait_for(lock, timeout,
                      [&] { return _engine._lsn.load(std::memory_order_acquire) >= lsn; });
}

void Replica::Heard() {
  std::lock_guard lock(_mutex);
  _last_heard = std::chrono::steady_clock::now();
}

void Replica::SetState(ReplicaState state) {
  std::lock_guard lock(_mutex);
  _state = state;
}

void Replica::Run() {
  auto backoff = _options.min_backoff;
  for (;;) {
    absl::Status status = RunSession();
    std::unique_lock lock(_mutex);
    if (_stopped) break;
    _last_error = std::move(status);
    _state = ReplicaState::kConnecting;
    // A session that got somewhere says nothing about the next one.
    backoff = _made_progress ? _options.min_backoff : std::min(backoff * 2, _options.max_backoff);
    _made_progress = false;
    _cv.wait_for(lock, backoff, [this] { return _stopped; });
    if (_stopped) break;
  }
  std::lock_guard lock(_mutex);
  _state = ReplicaState::kStopped;
}

void Replica::Watchdog() {
  const auto period = std::max(_options.idle_timeout / 4, std::chrono::milliseconds(1));
  std::unique_lock lock(_mutex);
  while (!_stopped) {
    _cv.wait_for(lock, period);
    if (_connection != nullptr &&
        std::chrono::steady_clock::now() - _last_heard > _options.idle_timeout) {
      _connection->Close();
    }
  }
}

absl::Status Replica::RunSession() {
  {
    std::lock_guard lock(_mutex);
    ++_connections;
  }
  auto connected = _connector();
  if (!connected.ok()) return connected.status();
  Connection& connection = **connected;
  {
    std::lock_guard lock(_mutex);
    if (_stopped) return absl::CancelledError("Stopped");
    _connection = &connection;
    _last_heard = std::chrono::steady_clock::now();
  }
  ScopeGuard forget{[this] {
    std::lock_guard lock(_mutex);
    _connection = nullptr;
  }};

  const Engine::LogPosition local = _engine.Position();
  std::string hello = NewMessage(MessageType::kHello);
  PutFixed32(hello, kProtocolVersion);
  PutHistory(hello, local.history);
  PutFixed64(hello, local.lsn);
  {
    std::lock_guard lock(_mutex);
    hello.push_back(_force_checkpoint ? 1 : 0);
  }
  RETURN_IF_ERROR(connection.Send(hello));

  std::string message;
  RETURN_IF_ERROR(connection.Receive(&message));
  Heard();
  Decoder welcome(message);
  const auto type = static_cast<MessageType>(welcome.Byte());
  if (type == MessageType::kError) {
    return absl::FailedPreconditionError(welcome.String());
  }
  const HistoryId history = welcome.History();
  const uint64_t lsn = welcome.Fixed64();
  const bool checkpoint = welcome.Byte() != 0;
  std::vector<std::pair<uint32_t, std::string>> collections;
  const uint64_t collection_count = welcome.Varint();
  for (uint64_t i = 0; i < collection_count && welcome.ok(); ++i) {
    const uint32_t id = welcome.Fixed32();
    collections.emplace_back(id, std::string(welcome.String()));
  }
  if (!welcome.ok() || type != MessageType::kWelcome) {
    return absl::InvalidArgumentError("Expected a Welcome");
  }
  RETURN_IF_ERROR(_engine.RegisterCollections(collections));

  if (checkpoint) {
    RETURN_IF_ERROR(ReceiveCheckpoint(connection, {history, lsn}));
  } else if (lsn != local.lsn) {
    return absl::InternalError("The primary continues from another position");
  }
  return Stream(connection);
}

absl::Status Replica::ReceiveCheckpoint(Connection& connection,
                                        const Engine::LogPosition& position) {
  SetState(ReplicaState::kReceivingCheckpoint);
  auto stage = _engine.NewCheckpointStage();
  if (!stage.ok()) return stage.status();

  std::string message;
  std::string buffer;
  bool footer = false;
  while (!footer) {
    RETURN_IF_ERROR(connection.Receive(&message));
    Heard();
    Decoder decoder(message);
    const auto type = static_cast<MessageType>(decoder.Byte());
    (void)decoder.Fixed64();
    if (!decoder.ok() || type != MessageType::kCheckpointData) {
      return absl::InvalidArgumentError("Expected checkpoint data");
    }
    buffer.append(decoder.rest());

    // Verify as it arrives, so that the old state goes only for a good new one.
    std::string_view rest = buffer;
    size_t complete = 0;
    while (const size_t size = internal::CompleteFrameSize(rest)) {
      Frame frame;
      if (internal::ParseFrame(rest.substr(0, size), &frame) != ParseStatus::kOk) {
        return absl::DataLossError("Damaged checkpoint from the primary");
      }
      footer = footer || frame.type == FrameType::kCheckpointFooter;
      complete += size;
      rest.remove_prefix(size);
    }
    RETURN_IF_ERROR((*stage)->Add(std::string_view(buffer).substr(0, complete)));
    buffer.erase(0, complete);
    RETURN_IF_ERROR(SendAck(connection, 0));
  }

  RETURN_IF_ERROR(_engine.InstallCheckpoint(**stage, position));
  std::lock_guard lock(_mutex);
  ++_checkpoints_installed;
  _force_checkpoint = false;
  _made_progress = true;
  _primary_lsn = std::max(_primary_lsn, position.lsn);
  _cv.notify_all();
  return absl::OkStatus();
}

absl::Status Replica::Stream(Connection& connection) {
  SetState(ReplicaState::kStreaming);
  // Tell the primary where we are, which frees it to trim its log.
  RETURN_IF_ERROR(SendAck(connection, _engine._lsn.load(std::memory_order_acquire)));

  std::string message;
  std::string buffer;
  for (;;) {
    RETURN_IF_ERROR(connection.Receive(&message));
    Decoder decoder(message);
    const auto type = static_cast<MessageType>(decoder.Byte());
    const uint64_t primary_lsn = decoder.Fixed64();
    if (!decoder.ok() || (type != MessageType::kLogData && type != MessageType::kHeartbeat)) {
      return absl::InvalidArgumentError("Expected log data");
    }
    if (type == MessageType::kLogData) {
      buffer.append(decoder.rest());
      const size_t complete = CompleteFramesSize(buffer);
      if (complete > 0) {
        absl::Status applied =
            _engine.ApplyReplicated(std::string_view(buffer).substr(0, complete));
        if (!applied.ok()) {
          // The two have diverged, or the frame was damaged: start again from a clean image.
          std::lock_guard lock(_mutex);
          _force_checkpoint = applied.code() == absl::StatusCode::kDataLoss;
          return applied;
        }
        buffer.erase(0, complete);
      }
    }
    {
      std::lock_guard lock(_mutex);
      _last_heard = std::chrono::steady_clock::now();
      _primary_lsn = primary_lsn;
      _made_progress = true;
    }
    _cv.notify_all();
    RETURN_IF_ERROR(SendAck(connection, _engine._lsn.load(std::memory_order_acquire)));
  }
}

}  // namespace gendb
