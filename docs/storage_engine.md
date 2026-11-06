# Storage engine

The engine under the generated `Db` classes: transactions, snapshots, durability and change
subscriptions. The code is in `lib/gendb/`: `engine.{h,cpp}`, `record_table.h`, `ordered_index.h`,
`log_format.h`, `wal.{h,cpp}`, `checkpoint.{h,cpp}` and `file_io.{h,cpp}`. Replication has
[its own document](replication.md). **This design is awaiting approval**; see
[Decisions to confirm](#decisions-to-confirm).

## Overview

All data lives in memory, where reads use it in place. The disk holds a write-ahead log and a
checkpoint, and is read only when the database opens. There are no other dependencies: the log,
the checkpoints and their file handling are about 1,200 lines of this repository.

```text
 Snapshot (any thread)            ScopedWrite (one at a time)
   |  lock-free reads               |  private write set; reads its own writes
   |                                |  Commit:
   |                                |    1. one frame in the log ---------+
   |                                |       write, and fsync if durable   |
   v                                v                                     v
 +---------------------------------------+               <dir>/wal-<first LSN>.log ...
 | Record tables    key -> version chain |   loaded      <dir>/checkpoint-<LSN>.gdb
 | Ordered indices  field + key          | <------------         ^
 +---------------------------------------+   on open             |
       |                            |    2. link new versions, publish
       |  a pinned snapshot         |    3. deliver to subscribers
       +--- background checkpoint --+    4. free what no snapshot can reach
```

## API

```cpp
auto db = Db::Open({.path = "/var/lib/app/db"});       // or Db db; for memory only
if (!db.ok()) return db.status();

{
  auto writer = (*db)->CreateWriter();                 // blocks other writers, never readers
  uint64_t id;
  RETURN_IF_ERROR(writer.NextAccountIdSequence(id));
  AccountBuilder account;
  account.set_account_id(id).set_name("Ann").set_age(30);
  RETURN_IF_ERROR(writer.PutAccount(id, account.Build()));
  RETURN_IF_ERROR(writer.UpdateAccount(id, AccountPatchBuilder().set_age(31).Build()));
  RETURN_IF_ERROR(writer.Commit());                    // durable when it returns
}                                                      // no commit means rollback

auto snapshot = (*db)->CreateSnapshot();               // repeatable, never blocks a commit
Account account;
RETURN_IF_ERROR(snapshot.GetAccount(id, account));     // zero-copy view
for (auto it = snapshot.GetAccountByAgeRange(18, 65); it.Valid(); it.Next()) {
  use(it.Value().name());
}
for (auto it = snapshot.ScanAccount(); it.Valid(); it.Next()) { /* any order */ }
```

Collections are declared in the schema YAML. `ephemeral: true` keeps one out of the log, checkpoints
and replication; it can still be subscribed to:

```yaml
collections:
  - name: sessions
    type: app.Session
    ephemeral: true
    primary_key:
      - session_id
```

## Guarantees

| Property | How it holds |
| --- | --- |
| **Atomicity** | A commit is one checksummed frame in the log, and becomes visible by publishing one sequence number. Dropping a transaction discards it. |
| **Consistency** | Indices and sequences change in the same transaction as the records. |
| **Isolation** | Writers are serial. Each snapshot sees exactly the commits up to its sequence number: snapshot isolation for readers, serializable for writers. |
| **Durability** | See the next section. |
| **Crash recovery** | The log is replayed up to the last intact frame. Partial commits cannot appear, across collections included. |

## Durable and non-durable commits

`sync_commits` picks the default, and `CommitOptions::sync` overrides it per commit:

| Mode | `Commit` returns after | Process dies | Power fails | Cost here |
| --- | --- | --- | --- | ---: |
| Durable (`true`, default) | `write` + `fdatasync` (`F_FULLFSYNC` on macOS) | nothing lost | nothing lost | 164 µs |
| Non-durable (`false`) | `write` | nothing lost | latest commits lost | 1.2 µs |

Non-durable commits always leave a consistent prefix: the log is ordered and each frame is
checksummed, so a lost commit takes only later commits with it. Two tools narrow the window:
`background_sync_interval` fsyncs periodically, and `engine().SyncWal()` does it on demand. A durable
commit also covers every commit before it.

A failed write or fsync stops the log: what reached the disk is unknown, and after a failed fsync
the OS may have dropped the dirty pages, so retrying could report success falsely. Commits then fail,
reads keep working, and reopening the database recovers from what is really on disk.

## Files

```text
<dir>/LOCK                                    flock: one process at a time
<dir>/wal-00000000000000000001.log            log segments, named by their first LSN
<dir>/wal-00000000000000012345.log
<dir>/checkpoint-00000000000000012300.gdb     the latest checkpoint, named by its LSN
```

Everything is a sequence of frames, `[crc32c: u32] [size: u32] [type: u8] [body]`, defined in
`log_format.h`. A commit frame moves byte for byte between the log, the network and a replica's log.

The **LSN** numbers logged commits contiguously: a gap in the log is detectable. Commits that touch
only ephemeral collections do not get one. Collections are identified in frames by the CRC of their
name, so reordering them in the schema is safe, at 4 bytes per entry.

**Log format.** Each segment starts with a header (format version, history ID, first LSN, collection
names). A commit is `[lsn: u64] [count: u32]`, then for each record `[collection: u32] [op: u8] [key
size: varint] [value size: varint] key value`, which is 21 bytes per commit and 7 or more per record
on top of keys and values. The operation is a put, a delete or a patch.

**Patches.** An `Update` is logged as its patch, when that is smaller than the record:

| A commit changing one float | Whole record | Patch |
| --- | ---: | ---: |
| 57-byte account | 93 bytes | 47 bytes |
| Record with a 1 KB string | 1,087 bytes | 47 bytes |

The value is `[crc32c of the resulting record: u32] [patch]`, and the patch is field IDs and values,
so it needs no schema and unknown fields travel too. Several `Update`s of one record in a transaction
make one patch against the committed record. A record created or replaced in the transaction is
logged whole, as is a `Put`, which says nothing about what changed.

**The checksum** is what makes this safe. A replay recomputes the record from the previous one, and
the frame's own checksum cannot tell if that previous state was wrong. Comparing the result catches
it at the first patch to that record: recovery reports `DataLoss`, and a replica fetches a new
checkpoint. Records logged whole need no such check, as they do not depend on earlier state.

This makes **the bytes a patch produces part of the on-disk format**: `Rebuild` and
`ApplyPatchInplace` must keep giving the same result for logs already written, and
`ResultBytesArePartOfTheLogFormat` pins them. A change to them has to bump `kFormatVersion` and keep
old logs replayable, for instance by taking a checkpoint with the old code first.

**A new segment** starts at `wal_segment_bytes`. The old one is fsynced before the new one exists,
which is what makes the next rule safe.

## Recovery

1. Remove leftover `*.tmp` files.
2. Load the newest checkpoint, rebuilding indices.
3. Replay the log after it, checking that LSNs are contiguous.

| Found | Means | Result |
| --- | --- | --- |
| Torn or zeroed frame at the end of the **last** segment | The crash interrupted a write | Cut it off and continue; it was never acknowledged as durable |
| Damage in any **other** segment, or a gap in LSNs | The disk lost acknowledged data | `DataLoss` |
| Damage in the checkpoint | The same | `DataLoss` |
| A collection the schema does not have | A rollback, a removal, or a newer primary | Kept as a foreign collection |

Damage in the middle of the last segment looks the same as a torn tail, so it loses the commits after
it without an error. Telling them apart reliably needs a record of the synced position, which the
format does not hold yet.

## Collections the schema does not have

A database can hold durable data of a collection its schema lacks: the program was rolled back, the
collection was removed from the YAML, a newer primary sent it, or it is now ephemeral here. Dropping
that data silently would be the worst outcome, and refusing to open would block the rollback, so it
is kept, as a **foreign collection**:

- It has no indices, the API cannot read it, and subscribers do not hear of it.
- It is logged, checkpointed and replicated like any other, under the name and ID it arrived with.
- A later schema that has it finds its data, and builds indices for it on open as usual.
- If the name is ephemeral here, its records stay apart from the local ephemeral ones.

`EngineStats::foreign_collections` lists them, as they use memory and disk with nothing visibly using
them. To drop one for good, open once with `discard_unknown_collections` and call `Checkpoint()`.

## Compaction

The log only grows, so a checkpoint replaces it: the full state at one LSN, in a new file, after
which older segments can go. `Engine::Checkpoint` does this on demand, and a background thread does
it automatically:

1. Under the writer lock for a moment, take the LSN, start a new log segment, and pin an MVCC
   snapshot at exactly that commit.
2. With no lock held, scan the snapshot into `checkpoint-<LSN>.gdb.tmp`. Writers and readers keep
   running; this is what MVCC buys.
3. Fsync, rename, fsync the directory. Remove the previous checkpoint.
4. Remove segments holding only older commits, except for `wal_retain_bytes`, and what connected
   replicas still need, up to `wal_replica_limit_bytes`.

A crash at any point leaves the old checkpoint and its log intact.

The automatic trigger is the log growing by `max(checkpoint_wal_bytes, size of the last checkpoint)`.
Writing a checkpoint costs in proportion to the data, so tying it to the checkpoint's size keeps the
total written to about twice the log's size, for any amount of data, while disk use stays below about
three times the data plus the thresholds.

The cost during a checkpoint is memory: versions replaced while it runs stay until the snapshot is
released. On a 100,000-record database a checkpoint takes about 11 ms, at about 11 M records/s.

## Snapshots and versions

A **record** is a key and a newest-first chain of immutable versions, each stamped with the
sequence number of its commit. A snapshot pins a sequence number `s` and reads the first version
with `seq <= s`. A commit prepends versions and then publishes its number, so readers ignore a
half-applied commit, and readers never block it.

Tables and indices have exactly one writer, so readers can be lock-free with plain atomics:

- `RecordTable` is an open-addressing table of pointers. A lookup is a hash and a few atomic loads.
  Growing it publishes a new slot array.
- `OrderedIndex` is a skip list of `encoded field + primary key`, using the order-preserving
  `key_codec`. Entries carry `created` and `deleted` sequence numbers.

### Reclamation

Memory is freed only by the writer, at the end of a commit. A queue ordered by sequence number
holds what commits superseded; an item at `S` is released once the oldest pinned snapshot is at `S`
or later. Removing something from a structure takes two steps: unlink it, then free it one commit
later, as a reader already inside may be standing on it. Unpinning is a short mutex-protected map
update, so the cost of a snapshot does not depend on its lifetime, but what it keeps does: a
snapshot held across many commits keeps every version they replaced. `engine().Stats()` reports it.

## Commit path

1. **Write set.** `Put`, `Update` and `Delete` build new versions privately. Index changes are
   computed here from the old and new messages, and a change and its opposite cancel out.
2. **Log.** All the changes go into one frame, as patches where possible. On failure nothing has been applied, and the
   transaction is rolled back.
3. **Apply.** Versions and index entries are linked in, then the sequence number is published.
4. **Deliver and reclaim.**

The in-memory apply cannot fail except by running out of memory, which is treated as fatal.

## Counters

A sequence is a record in the private `metadata` collection, so it commits and rolls back with its
transaction: IDs are durable, atomic with the data, and committed IDs have no gaps.

## Change subscriptions

Another part of the program can follow the changes to chosen collections after each commit:

```cpp
auto subscription = db->Subscribe({
    .on_account = [](const AccountChange& change) {
      switch (change.kind()) {
        case gendb::ChangeKind::kInsert: Added(change.after()); break;
        case gendb::ChangeKind::kDelete: Removed(change.before()); break;
        case gendb::ChangeKind::kUpdate:
          if (change.balance_changed()) {
            Moved(change.before().balance(), change.after().balance());
          }
          break;
      }
    },
    .on_position = [](const PositionChange& change) { /* ... */ },
    .on_commit = [](uint64_t seq) { /* all changes of this commit have been delivered */ },
});
// Ends when `subscription` is destroyed.
```

Only collections with a handler are delivered. All handlers of a subscription run on one thread in
commit order, so changes to different collections stay in order, and `on_commit` marks transaction
boundaries. Rolled-back transactions are never seen.

**What changed** is answered by comparison, not by a patch. Each change has the whole message
`before()` and `after()`, and a generated `<field>_changed()` for each field. This costs nothing
unless asked, and has one meaning for every way of writing: a `Put` that replaces a record reports
exactly the fields that differ, which a patch could not tell. A patch would also say what was
requested, and a field set to its current value would look changed.

| | `Delivery::kQueued` (default) | `Delivery::kInline` |
| --- | --- | --- |
| Runs | On the subscription's own thread | During `Commit`, on the writer's thread |
| Handler may | Anything, including commit | Read through a snapshot; must not start a transaction |
| Views | Copied per change | Zero-copy |
| Commit, 1 scalar patch | 1.9 µs | 513 ns (483 ns with no subscriber) |
| A slow handler | Fills the queue | Delays every writer directly |

A queued subscription never loses a change: once its queue reaches `max_queue_bytes`, new
transactions wait until it has drained to half, except those started by a handler, which would
otherwise wait for themselves. The limit is soft, as a transaction already running can still commit.
`WaitUntilDelivered()` blocks until the handler has seen every commit made so far.

Subscribers also work on a replica, where a whole checkpoint arrives as one commit reporting just the
records that differ. Delivery is in memory only: a subscriber that must not miss changes across
restarts should compare with a snapshot when it starts.

## Performance

Measured on an Apple M4 Pro in a Docker Linux VM, Release, 100,000 accounts of about 100 bytes. Raw
results are in [benchmarks/reference/db.txt](../benchmarks/reference/db.txt). The comparison is with
the RocksDB backend this replaced, built from commit `589e03e` and run alternately with this code in
one session. The machine itself drifts between runs by up to 50%, so only the ratios in this table
mean much.

| Operation | RocksDB backend | Now |
| --- | ---: | ---: |
| Commit, non-durable | 1.94 µs | 1.19 µs |
| Commit, durable | 166 µs | 164 µs |
| Open with 100,000 records, from the log | 112 ms | 118 ms |
| Open with 100,000 records, from a checkpoint | n/a | 115 ms |
| Write a checkpoint of 100,000 records | n/a | 11 ms |
| Get by key, 1 thread | 101 ns | 106 ns |
| Index scan | 19.0 M rows/s | 17.1 M rows/s |
| Full collection scan | 17.7 M rows/s | 17.1 M rows/s |
| Commit one scalar patch, memory only | 465 ns | 468 ns |
| Commit under a long-lived snapshot | 350 ns | 366 ns |

Logging patches, compared with the same code logging whole records, one changed field per commit:

| Operation | Whole records | Patches |
| --- | ---: | ---: |
| Non-durable commit, small record | 525 ns | 569 ns |
| Non-durable commit, record with a 1 KB string | 1.15 µs | 850 ns |
| Log for 100,000 updates of 57-byte accounts | 9.3 MB | 4.7 MB |
| Replaying those updates, with opening | 122 ms | 127 ms |

Non-durable commits are 40% faster. Everything else is within this machine's noise, as expected: the
read path did not change, and a durable commit is one `fdatasync` either way. Most of the time to open
goes to inserting into the skip list; building indices from sorted keys would be the next thing to
try.

The binary also loses RocksDB and its compression libraries, the write buffers and block cache (up to
72 MB) are gone, and the log is directly useful for replication.

Memory per record, beyond key and value: 48 bytes of headers for the node and version, 11–23 bytes
of table slots, and about 43 bytes per index entry plus its key, before allocator overhead.

## Limitations

- **The whole dataset must fit in memory**, and on disk too.
- Open time grows with the data, as indices are rebuilt in memory.
- One writer at a time: a long transaction delays other writers, though no reader. It also delays
  anything that needs a moment of the writer lock: starting a checkpoint or a replica's session.
- Each durable commit pays its own fsync, as commits are serial; there is no group commit. Batch work
  into one transaction, or commit non-durably and call `SyncWal()` periodically.
- `Checkpoint()` must not be called while holding a transaction on the same thread.
- Views from a `ScopedWrite` are invalidated by writing that key and by `Commit`. Views from a
  `Snapshot` live until it is destroyed.
- A replica that crashes just after logging a commit that does not fit its state, before it can fetch
  a checkpoint, then fails to open with `DataLoss`. Delete its directory to seed it again. This takes
  a replica already wrong through a bug or a damaged disk.
- Tested by killing processes and by cutting and damaging files. Power loss itself and failing disks
  are not simulated, so the ordering of fsyncs is checked by review only. The kill tests use `fork`
  and are skipped under ThreadSanitizer.

## Decisions to confirm

1. **Memory-first with a log and checkpoints**, rather than an on-disk structure. This is what
   provides zero-copy reads, at the price of holding everything in RAM. A dataset larger than memory
   would need a read-through tier.
2. **Full checkpoints**, rather than incremental ones. The write cost is the whole dataset each time,
   amortized by the trigger above. Far simpler, and the same bytes seed a replica.
3. **Patches in the log, with a checksum of the result.** Smaller logs and less to send, at the price
   of recovery depending on the patch code, whose output is now part of the format, and about 8% on
   commits of small records.
4. **Before and after states for subscribers**, rather than patches, as explained above.
5. **Backpressure rather than loss** when a queued subscriber falls behind.
6. **Keeping foreign collections** rather than refusing to open or dropping them. Safe by default, at
   the price of memory that nothing visibly uses, which `Stats()` reports.
7. **Sync by default.** Safe, and about 140 times slower than no sync here.
8. **`Guard` is gone**: `SharedLock()` became `CreateSnapshot()`, as a snapshot is equally cheap and
   does not block commits. The accessor style in `db_api_codegen.md` (`db.Accounts().Get`) is not
   implemented; the flat `GetAccount` names stayed.
