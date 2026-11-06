# Replication

A second program connects to the first, fetches the database, and then receives every commit as it
happens. The network is the application's choice: the library needs one small interface, and comes
with a gRPC implementation. The code is in `lib/gendb/replication.{h,cpp}`. **This design is awaiting
approval**; see [Decisions to confirm](#decisions-to-confirm).

## Usage

On the primary, over gRPC:

```cpp
auto db = Db::Open({.path = "/var/lib/app/primary"});
gendb::ReplicationServer replication((*db)->engine());

gendb::GrpcReplicationService service(replication);
grpc::ServerBuilder builder;
builder.AddListeningPort("0.0.0.0:7000", credentials);
builder.RegisterService(service.service());              // can share a server with other services
auto server = builder.BuildAndStart();
// ... use the database as usual ...
replication.Shutdown();                                  // first: it ends the streaming calls
server->Shutdown();
```

On the replica, in another process:

```cpp
auto db = Db::Open({.path = "/var/lib/app/replica", .replica = true});   // or Db db; memory only
auto channel = grpc::CreateChannel("primary:7000", credentials);
gendb::Replica replica((*db)->engine(), gendb::GrpcConnector(channel));

auto snapshot = (*db)->CreateSnapshot();                 // read as usual, at any time
auto subscription = (*db)->Subscribe({.on_account = ...});  // subscriptions work here too
replica.Status();                                        // state, LSNs, last error
```

`examples/replication_demo.cpp` is a complete pair of programs, for gRPC and for raw TCP.

## Transports

To replicate over anything else, implement `gendb::Connection`: one reliable, ordered connection that
keeps message boundaries.

```cpp
class Connection {
 public:
  virtual absl::Status Send(std::string_view message) = 0;   // blocks; flow control is yours
  virtual absl::Status Receive(std::string* message) = 0;    // blocks until the next message
  virtual void Close() = 0;                                  // any thread; unblocks the others
};
```

Then connect the two ends:

| Side | What to do |
| --- | --- |
| Primary | For each accepted connection, call `ReplicationServer::Serve(connection)` on a thread of your choice. It blocks for the session. |
| Replica | Pass `Replica` a function returning a new `std::unique_ptr<Connection>`. It calls it whenever it needs to connect. |

The library owns no sockets, threads for accepting, or addresses, and encryption and authentication
are the transport's. Messages never exceed `kMaxReplicationMessageBytes` (about 1 MiB), so transports
with a message limit need nothing special.

Three transports exist, all under test:

| Transport | Where | Size |
| --- | --- | ---: |
| gRPC | `lib/gendb/replication_grpc.{h,cpp}`, the optional target `gendb_replication_grpc` | 140 lines |
| Raw TCP | `examples/tcp_transport.h`, without encryption, as an example | 125 lines |
| In-process | `tests/replication_test.cpp` | 60 lines |

gRPC is optional: `conan install -o "&:with_grpc=False"` builds everything else without it.

## How it works

```text
 replica                                  primary
   Hello (history, LSN)  ------------->
                         <-------------   Welcome (position, mode, collection IDs and names)
                         <-------------   checkpoint bytes        if it cannot continue its log
   Ack                   ------------->
                         <-------------   commit frames, or a heartbeat when idle, forever
   Ack                   ------------->
```

**Catching up.** The replica says where it is. If that is a point in the primary's history whose next
commit is still available, streaming starts there. Otherwise the primary pins an MVCC snapshot and
sends a checkpoint, then streams from the checkpoint's LSN. No checkpoint file is needed, so this
works on a memory-only primary, and takes no lock while it runs.

**The stream is the log.** Commit frames travel byte for byte as the primary logged them, and the
replica puts the same bytes in its own log, with the same LSNs. Checkpoint data has the same format as
a checkpoint file; a persistent replica's copy of it simply becomes its checkpoint. Both are cut into
messages anywhere, so a commit can be larger than a message.

**Where commits come from.** Each is copied into a ring in memory, so that following the primary
touches no disk, and adds no cost until a `ReplicationServer` exists. A replica farther behind than
`commit_ring_bytes` reads the log files instead. Failing that, as on a memory-only primary or after
the log was removed, it gets a new checkpoint.

**Only durable commits leave.** With non-durable commits, the sender fsyncs the log before sending, so
that a replica cannot hold a commit the primary would lose in a power failure. This adds latency to
replication, not to the commits.

**Applying.** Each commit is applied like a local one, indices included, under one sequence number:
readers on the replica see whole commits only. A batch of commits arriving together pays one fsync.

**Installing a checkpoint** on a replica that already has data does not disturb its readers. The
incoming data is held aside and verified, on disk or in memory, then linked in under one sequence
number, with records that did not change kept as they are and records that are gone removed. Readers
see the old state until the new one is whole, and subscribers see one commit of just the differences.

**After a failure** of any kind the replica reconnects with backoff, and continues from its position.
A replica opened from its own disk resumes the same way, with no checkpoint if the primary still has
the log.

## Consistency

- Replication is **asynchronous**: `Commit` on the primary does not wait for replicas, and a failed
  primary can take the latest commits with it. `ConnectedReplica::acked_lsn` tells how far each has
  got, on which an application can wait.
- A replica shows **a consistent prefix** of the primary's commits, in order, always.
- A replica rejects local commits to durable collections with `FailedPrecondition`. **Ephemeral
  collections are local on each side**: never sent, and writable on a replica. Sequences live in a
  durable collection, so they follow the primary.

## Histories and failover

Each database has a random **history ID**. The same ID and LSN mean the same state, which is how a
primary knows it can continue a replica's log. A replica takes its primary's ID with a checkpoint.

To fail over, stop the `Replica` and call `engine().PromoteToPrimary()`. This starts a new history.
Followers of the old one, including the old primary when it rejoins as a replica, get a checkpoint
from the new primary and lose whatever only they had. There is no election or fencing: choosing the
primary and keeping the old one from taking writes are the application's job.

A replica ahead of its primary, with a higher LSN in the same history, means the primary lost data,
as by restoring a backup. It gets a checkpoint too. A commit that fails to apply, or arrives damaged,
also causes one.

A memory-only primary starts a new history on each start, so its replicas follow it to an empty state.

## Schema changes

Each side has its own schema and is compiled separately, and **they can be upgraded in any order**.
Messages are forward and backward compatible, so fields can differ freely. For collections:

| Change | What happens |
| --- | --- |
| The primary has a collection the replica does not | The primary names its collections when a session starts. The replica keeps the unknown one as a [foreign collection](storage_engine.md#collections-the-schema-does-not-have): stored, logged and passed on, but not readable. Upgrading the replica makes it readable, indices included, with no resync. |
| The replica has a collection the primary does not | It stays empty until the primary is upgraded. |
| Removed from the primary's schema | The primary keeps it as foreign, so replicas keep receiving it in checkpoints, until discarded on the primary. A later checkpoint then removes it from replicas. |
| Durable on the primary, ephemeral on the replica | The primary's records are kept as foreign, apart from the local ones. |
| Add or change indices | Independent: each side computes its own from the records. |

Only the ID and the name travel. Indices need no metadata, as each side derives its own from its code
when it knows the collection.

## Performance

On the same machine as [the engine's numbers](storage_engine.md#performance), over an in-process
transport with no network, 100,000 accounts of about 100 bytes:

| Operation | Time |
| --- | ---: |
| Seed a new memory-only replica, from connecting to fully readable | 72 ms |
| Commit on the primary and apply on a following replica | 2.3 µs per commit |

## Operations

- `ReplicationServer::Replicas()` lists sessions and their positions; `Replica::Status()` is the view
  from the other end.
- Silence is detected at both ends: heartbeats flow every `heartbeat_interval`, a replica drops a
  connection after `idle_timeout`, and a primary drops a replica after `replica_timeout`. A `Send`
  blocked on a dead peer relies on the transport's own keepalive, so enable it (`SO_KEEPALIVE`,
  `GRPC_ARG_KEEPALIVE_TIME_MS`).
- A session holds resources while a replica lags. While sending a checkpoint, the pinned snapshot keeps
  every version replaced since it began. While a replica is behind, its position keeps log files, up to
  `wal_replica_limit_bytes`, after which it will get a new checkpoint.
- A replica installing a checkpoint needs room for it twice: in a staged file on disk, or transiently
  in memory if it has no directory.

## Limitations

- Asynchronous only; no quorum commits.
- One session thread per replica on the primary, plus one for acks.
- A new memory-only primary's replicas restart from nothing, as above.
- No built-in encryption or authentication: both belong to the transport. The gRPC transport takes
  whatever credentials its server and channel are built with.
- Tested with process kills and dropped connections, over in-process, TCP and gRPC transports.
  Network partitions with a silently dead peer are not simulated.

## Decisions to confirm

1. **Physical replication of the log**, rather than logical change events. Simple and fast, and the
   replica's log matches the primary's.
2. **Transport as a blocking `Connection`**, rather than an asynchronous interface. Very little to
   implement, at the price of a thread per replica. A primary with hundreds of replicas would want
   otherwise.
3. **Protocol messages are hand-encoded and opaque to the transport**, rather than protobuf. The core
   library then has no serialization dependency, and gRPC just carries bytes.
4. **A replica is read-only except for ephemeral collections.**
5. **`PromoteToPrimary` is a manual primitive**, with election left to the application.
