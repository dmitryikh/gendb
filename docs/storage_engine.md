# Storage engine

The engine under the generated `Db` classes: transactions, snapshots, durability, and change
capture. The code is in `lib/gendb/engine.{h,cpp}`, `record_table.h`, `ordered_index.h` and
`rocksdb_backend.{h,cpp}`. **This design is awaiting approval**; see
[Decisions to confirm](#decisions-to-confirm).

## Overview

All data lives in memory, where reads use it in place. RocksDB holds the durable copy and is read
only when the database opens.

```text
 Snapshot (any thread)            ScopedWrite (one at a time)
   |  lock-free reads               |  private write set; reads its own writes
   |                                |  Commit:
   |                                |    1. one atomic batch ---------> RocksDB
   |                                |       (records, last seq,        (WAL + LSM)
   v                                v        change log)
 +---------------------------------------+       |
 | Record tables    key -> version chain | <-----+ loaded on open;
 | Ordered indices  field + key          |         indices are rebuilt
 +---------------------------------------+
                                    |    2. link new versions, publish seq
                                    |    3. notify change listeners
                                    |    4. free what no snapshot can reach
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

Collections are declared in the schema YAML. `ephemeral: true` keeps one out of RocksDB:

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
| **Atomicity** | A commit is one RocksDB write batch, and becomes visible by publishing one sequence number. Dropping a transaction discards it. |
| **Consistency** | Indices and sequences change in the same transaction as the records. |
| **Isolation** | Writers are serial. Each snapshot sees exactly the commits up to its sequence number: snapshot isolation for readers, serializable for writers. |
| **Durability** | With `sync_commits` (the default), `Commit` returns after the WAL is fsynced. Without it, a crash can lose the latest commits but leaves a consistent prefix of them; `engine().SyncWal()` closes that window on demand. |
| **Crash recovery** | RocksDB replays its WAL up to the last intact batch (point-in-time recovery). Partial commits cannot appear, across collections included. |

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
2. **Durable write.** Everything goes in one `WriteBatch`: records, the new sequence number, and
   change log entries. On failure nothing has been applied, and the transaction is rolled back.
3. **Apply.** Versions and index entries are linked in, then the sequence number is published.
4. **Notify and reclaim.**

The in-memory apply cannot fail except by running out of memory, which is treated as fatal.

## Recovery

`Db::Open` loads each persistent collection and rebuilds its indices, so open time and memory are
proportional to the data: 100,000 records take about 85 ms in the benchmark below. Column
families are named after collections, so reordering collections in the schema is safe, and those of
removed collections stay on disk untouched.

## Counters

A sequence is a record in the private `metadata` collection, so it commits and rolls back with its
transaction: IDs are durable, atomic with the data, and committed IDs have no gaps.

## Change data capture

Two consumers, both in commit order:

| | Live listener | Durable change log |
| --- | --- | --- |
| API | `engine().AddChangeListener(fn)` | `EngineOptions/DbOptions::change_log`, `engine().ReadChanges(after_seq, fn)`, `TruncateChanges(seq)` |
| Delivery | During `Commit`, on the writer's thread | Polled; the consumer stores its position, so delivery is at-least-once across restarts |
| Ephemeral collections | Included | Excluded |
| Cost | None without listeners; a slow listener delays writers | Doubles the bytes written, only if enabled |

A `Change` holds the collection, key, whether it was deleted, and the full new message.

## Performance

Measured on an Apple M4 Pro in a Docker Linux VM, Release, 100,000 accounts of about 100 bytes, with
the previous implementation for comparison: `unordered_map` behind a `shared_mutex`, with locks held
for a reader's whole lifetime. Raw results are in
[benchmarks/reference/db.txt](../benchmarks/reference/db.txt).

| Operation | Before | Now |
| --- | ---: | ---: |
| Get by key, 1 thread | 323 ns | 95 ns |
| Get by key, 8 threads (per thread) | 279 ns | 74 ns |
| Index scan | 3.2 M rows/s | 23.8 M rows/s |
| Commit one scalar patch, memory only | 535 ns | 478 ns |
| Commit under a long-lived snapshot | blocked until it ends | 289 ns |
| Snapshot open and close, uncontended | — | 20 ns |
| Snapshot open and close, 8 threads | — | 497 ns |
| Full collection scan | — | 22 M rows/s |
| Persistent commit, no fsync | — | 1.9 µs |
| Persistent commit, fsync per commit | — | 149 µs |

Memory per record, beyond key and value: 48 bytes of headers for the node and version, 11–23 bytes
of table slots, and about 43 bytes per index entry plus its key, before allocator overhead.
RocksDB's write buffers are capped at 64 MB, and its block cache at 8 MB, since reads do not use it.

## Limitations

- **The whole dataset must fit in memory**, and on disk too.
- Open time grows with the data, as nothing in memory is checkpointed.
- One writer at a time: a long transaction delays other writers, though no reader.
- Each synced commit pays its own fsync, as commits are serial. Batch work into one transaction, or
  commit with `{.sync = false}` and call `SyncWal()` periodically.
- Views from a `ScopedWrite` are invalidated by writing that key and by `Commit`. Views from a
  `Snapshot` live until it is destroyed.
- The crash test uses `fork` and is skipped under ThreadSanitizer. Disk failures are not injected.

## Decisions to confirm

1. **Memory-first with RocksDB as the durable copy**, rather than reading from RocksDB. This is what
   provides zero-copy reads and the numbers above, at the price of holding everything in RAM. A
   dataset larger than memory would need a read-through tier.
2. **RocksDB's WAL as the WAL**, rather than a custom log and checkpoints. It is less code and
   already handles torn writes, at the price of a heavy dependency for little of its functionality.
3. **`Guard` is gone**: `SharedLock()` became `CreateSnapshot()`, as a snapshot is equally cheap and
   does not block commits. The accessor style in `db_api_codegen.md` (`db.Accounts().Get`) is not
   implemented; the flat `GetAccount` names stayed.
4. **A separate change log column family**, rather than tailing RocksDB's WAL with
   `GetUpdatesSince`. It doubles writes when enabled, but retention follows consumers, not WAL TTLs.
5. **Sync by default.** Safe, and about 80 times slower than no sync here.
