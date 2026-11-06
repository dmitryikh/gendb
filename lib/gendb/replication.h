#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gendb/engine.h"

// Streams a database to other processes: a replica fetches a checkpoint, then follows the commit
// log. The network is the application's choice, behind Connection. docs/replication.md is the
// design and shows how to write a transport; replication_grpc.h is a ready-made one.
namespace gendb {

// The largest message Send is asked to carry.
inline constexpr size_t kMaxReplicationMessageBytes = (size_t{1} << 20) + 64;

// The session protocol's versions this code can speak. Each side says its range when connecting,
// and they use the highest they share, or refuse with both ranges in the error.
inline constexpr uint32_t kReplicationProtocolVersion = 1;
inline constexpr uint32_t kOldestReplicationProtocol = 1;

// One reliable, ordered connection that keeps message boundaries. All it takes to run replication
// over some transport is to implement this, and to call ReplicationServer::Serve for each accepted
// connection on the primary, and to give Replica a function that connects on the replica.
//
// Send and Receive may run at once on different threads, but never two Sends or two Receives.
// Both block; flow control is the transport's. An error from either ends the session.
class Connection {
 public:
  virtual ~Connection() = default;
  virtual absl::Status Send(std::string_view message) = 0;
  // Replaces `message` with the next one.
  virtual absl::Status Receive(std::string* message) = 0;
  // Callable from any thread and more than once. Makes blocked and later calls fail promptly. May
  // discard what was sent and not yet delivered.
  virtual void Close() = 0;
};

using Connector = std::function<absl::StatusOr<std::unique_ptr<Connection>>()>;

struct ReplicationServerOptions {
  // Recent commits kept in memory. Replicas within this distance are served without touching disk;
  // farther ones read the WAL. With no WAL, as on a memory-only primary, a replica that falls
  // farther behind than this, including while it receives its checkpoint, starts over.
  size_t commit_ring_bytes = size_t{64} << 20;
  // A replica that hears nothing else gets a heartbeat this often. It answers each.
  std::chrono::milliseconds heartbeat_interval{1000};
  // A replica silent for this long is disconnected.
  std::chrono::milliseconds replica_timeout{30000};
  uint32_t oldest_protocol = kOldestReplicationProtocol;
  uint32_t newest_protocol = kReplicationProtocolVersion;
};

struct ConnectedReplica {
  uint64_t session_id;
  uint64_t sent_lsn;
  uint64_t acked_lsn;  // Applied by the replica.
  bool receiving_checkpoint;
  uint32_t protocol;  // As agreed.
};

// The primary's side. It adds nothing to commits until constructed, and then one copy of each
// commit into the ring.
class ReplicationServer {
 public:
  explicit ReplicationServer(Engine& engine, ReplicationServerOptions options = {});
  // Calls Shutdown. The engine must outlive this.
  ~ReplicationServer();
  ReplicationServer(const ReplicationServer&) = delete;
  ReplicationServer& operator=(const ReplicationServer&) = delete;

  // Runs one replica's session on the calling thread until it ends, then closes the connection.
  // Any number can run at once, each with its own thread.
  absl::Status Serve(Connection& connection);
  // Ends all sessions, waits for them to return, and refuses new ones.
  void Shutdown();
  std::vector<ConnectedReplica> Replicas() const;

 private:
  struct Session;

  absl::Status RunSession(Connection& connection, Session& session);
  static void WaitForHangUp(const Session& session);

  Engine& _engine;
  const ReplicationServerOptions _options;
  mutable std::mutex _mutex;
  std::condition_variable _sessions_cv;
  std::vector<Session*> _sessions;
  uint64_t _next_session_id = 1;
  bool _shutdown = false;
};

struct ReplicaOptions {
  std::chrono::milliseconds min_backoff{100};
  std::chrono::milliseconds max_backoff{5000};
  // Without a message for this long, the connection is considered dead. Keep it above the primary's
  // heartbeat interval.
  std::chrono::milliseconds idle_timeout{10000};
  uint32_t oldest_protocol = kOldestReplicationProtocol;
  uint32_t newest_protocol = kReplicationProtocolVersion;
};

enum class ReplicaState { kConnecting, kReceivingCheckpoint, kStreaming, kStopped };

struct ReplicaStatus {
  ReplicaState state;
  uint64_t applied_lsn;
  uint64_t primary_lsn;  // As last heard.
  uint64_t connections;  // Sessions started, successful or not.
  uint64_t checkpoints_installed;
  uint32_t protocol;  // As agreed in the latest session; zero before any.
  // The latest commit at which this replica's whole state was found to equal the primary's.
  uint64_t verified_lsn;
  // Times it was found not to, each of which made it fetch a new checkpoint.
  uint64_t divergences;
  absl::Status last_error;
};

// The follower's side: runs a background thread that connects, catches up and follows, and
// reconnects after any failure. The engine stays readable throughout and each commit becomes
// visible whole; a checkpoint replaces the old state in one step. Only its ephemeral collections
// take local commits.
//
// The engine should be opened with EngineOptions::replica, so that nothing can write to it before
// this object exists.
class Replica {
 public:
  Replica(Engine& engine, Connector connector, ReplicaOptions options = {});
  // Calls Stop. The engine must outlive this.
  ~Replica();
  Replica(const Replica&) = delete;
  Replica& operator=(const Replica&) = delete;

  void Stop();
  ReplicaStatus Status() const;
  // True once this engine has applied `lsn`, or false after `timeout`.
  bool WaitForLsn(uint64_t lsn, std::chrono::milliseconds timeout) const;

 private:
  void Run();
  void Watchdog();
  absl::Status RunSession();
  absl::Status ReceiveCheckpoint(Connection& connection, const Engine::LogPosition& position);
  absl::Status Stream(Connection& connection);
  // After applying everything up to `lsn`, compares with the primary's state then.
  absl::Status CheckDigest(uint64_t lsn, uint64_t digest);
  void Heard();
  void SetState(ReplicaState state);

  Engine& _engine;
  const Connector _connector;
  const ReplicaOptions _options;

  mutable std::mutex _mutex;
  mutable std::condition_variable _cv;
  bool _stopped = false;
  Connection* _connection = nullptr;
  std::chrono::steady_clock::time_point _last_heard;
  ReplicaState _state = ReplicaState::kConnecting;
  uint64_t _primary_lsn = 0;
  uint64_t _connections = 0;
  uint64_t _checkpoints_installed = 0;
  uint32_t _protocol = 0;
  uint64_t _verified_lsn = 0;
  uint64_t _divergences = 0;
  absl::Status _last_error;
  bool _made_progress = false;
  bool _force_checkpoint = false;

  std::thread _thread;
  std::thread _watchdog;
};

}  // namespace gendb
