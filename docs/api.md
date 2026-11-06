# Using gendb

What the generator produces, and how to use it. The design behind it is in
[storage_engine.md](storage_engine.md) and [replication.md](replication.md).

## Set up a project

Install the library (`cmake --install`), then in your project:

```cmake
find_package(gendb REQUIRED)

set(GENERATED ${CMAKE_CURRENT_BINARY_DIR}/generated)
gen_db_messages(messages ${CMAKE_CURRENT_SOURCE_DIR}/schemas ${GENERATED})
gen_db_schema(database ${CMAKE_CURRENT_SOURCE_DIR}/schemas/db.yaml ${GENERATED})

set_source_files_properties(${GENERATED}/database.cpp ${GENERATED}/database.h PROPERTIES GENERATED TRUE)
add_executable(app main.cpp ${GENERATED}/database.cpp)
target_include_directories(app PRIVATE ${GENERATED})
target_link_libraries(app PRIVATE gendb::gendb)   # and gendb::replication_grpc, if built
add_dependencies(app messages database)
```

The generators need Python 3 with `jinja2` and `PyYAML`, and `flatc` on the `PATH`. Paths in the YAML
file are relative to the top of your source tree. [tests/package](../tests/package) is a complete
example.

## Describe the data

Records are described in FlatBuffers schemas. `flatc` only parses them: stored bytes use the
[gendb message format](message_format.md).

```text
namespace app;

table Account {
  account_id: ulong;
  name: string;
  address: string;
  age: int;
  balance: float;
}
```

Fields can be added, and retired with `(deprecated)`. A field keeps its ID and type forever. See
[message_format.md](message_format.md).

The database is described in YAML:

```yaml
types:
  fbs_files:
    - schemas/account.fbs

options:
  cpp_namespace: "app"

collections:
  - name: accounts
    type: app.Account
    primary_key:      # One or more fields, set in every record.
      - account_id
  - name: sessions
    type: app.Session
    ephemeral: true   # Never logged or replicated; gone on restart.
    primary_key:
      - session_id

indices:
  - name: account_by_age
    collection: accounts
    fields:           # One or more fields. A record lacking any is not in the index.
      - age
  - name: account_by_address
    collection: accounts
    unique: true      # At most one record per value.
    fields:
      - address

sequences:
  - name: account_id_sequence
    type: ULong
```

## Messages

Each table generates three classes:

| Class | What it is |
| --- | --- |
| `Account` | A zero-copy view, with `age()` and `has_age()`. Does not own its bytes. |
| `AccountBuilder` | Makes a message: `.set_age(30).Build()`. Strings are borrowed until `Build()`. |
| `AccountPatchBuilder` | Makes a partial update: `.set_age(31).clear_name().Build()`. Owns its strings. |

## Open a database

```cpp
auto db = Db::Open({.path = "/var/lib/app/db"});   // Recovers what is there.
Db memory_only;                                    // Nothing on disk.
```

`DbOptions` is [`gendb::EngineOptions`](../lib/gendb/engine.h), documented where it is declared:
durability, log segments, compaction, the file system, the observer. One process at a time.

## Write

A writer is a transaction, and there is one at a time: `CreateWriter` blocks until the previous one
is destroyed. Readers are never blocked.

```cpp
auto writer = db->CreateWriter();

uint64_t id;
RETURN_IF_ERROR(writer.NextAccountIdSequence(id));   // Rolls back with the transaction.
RETURN_IF_ERROR(writer.PutAccount(AccountBuilder().set_account_id(id).set_age(30).Build()));
RETURN_IF_ERROR(writer.UpdateAccount(id, AccountPatchBuilder().set_age(31).Build()));
RETURN_IF_ERROR(writer.DeleteAccount(other_id));

RETURN_IF_ERROR(writer.Commit());                    // Durable when it returns, by default.
RETURN_IF_ERROR(writer.Commit({.sync = false}));     // Or not, for this commit.
```

Without `Commit`, the writer's destructor or `Rollback()` discards the changes. A writer can go on
after a commit.

| Call | Fails with |
| --- | --- |
| `PutAccount(message)` | `InvalidArgument` if malformed or missing a primary key field; `AlreadyExists` if it takes a value a unique index has elsewhere |
| `UpdateAccount(key, patch)` | `NotFound`; `InvalidArgument` if the patch changes a primary key field; `AlreadyExists` as above |
| `DeleteAccount(key)` | `NotFound` |
| `Commit()` | The log's error; on a replica, `FailedPrecondition` unless only ephemeral collections changed |

The key always comes from the message, so the two cannot disagree. For a primary key of several
fields, a struct `AccountKey` holds them.

### What a writer sees

All reads through a writer include its own uncommitted changes, so a loop can build on its earlier
iterations:

```cpp
for (const Order& order : orders) {
  Account account;
  RETURN_IF_ERROR(writer.GetAccount(order.account_id(), account));   // Sees earlier updates.
  RETURN_IF_ERROR(writer.UpdateAccount(order.account_id(),
                                       AccountPatchBuilder().set_balance(account.balance() - order.price()).Build()));
}
```

`GetX`, `ScanX` and index queries all work this way, and a message from them is invalidated by the next
write to that key and by `Commit`. Read what you need from it before writing. Writing or committing
also ends a scan in progress.

## Read

A snapshot is a consistent view of committed state, at no cost to writers. Messages from it stay valid
until it is destroyed; holding it keeps the versions it can see in memory.

```cpp
auto snapshot = db->CreateSnapshot();
Account account;
RETURN_IF_ERROR(snapshot.GetAccount(id, account));   // Zero copy; NotFound if absent.
for (auto it = snapshot.ScanAccount(); it.Valid(); it.Next()) { use(it.Value()); }   // In no order.
```

### Index queries

For an index on `fields: [account_id, instrument]`:

```cpp
// All fields, or the first ones.
snapshot.GetPositionByAccountAndInstrumentEqual(42, "EURUSD");
snapshot.GetPositionByAccountAndInstrumentEqual(42);

// A range over one field, given the ones before it: [min, max), with std::nullopt for an open side.
snapshot.GetPositionByAccountAndInstrumentRange(42, "A", "F");
snapshot.GetPositionByAccountAndInstrumentRange(42, "F", std::nullopt);
snapshot.GetPositionByAccountAndInstrumentRange(10, 20);        // Over account_id, and all instruments.

// Any of them can go backwards, and stop early.
snapshot.GetAccountByAgeRange(18, std::nullopt, {.reverse = true, .limit = 10});
```

Results come ordered by the rest of the index, then by primary key. A step backwards costs a search
rather than a pointer. A unique index also gets a direct lookup:

```cpp
RETURN_IF_ERROR(snapshot.GetAccountByAddress("ann@example.com", account));
```

## Follow changes

Another part of the program can follow the changes to chosen tables:

```cpp
auto subscription = db->Subscribe({
    .on_account = [](const AccountChange& change) {
      if (change.kind() == gendb::ChangeKind::kUpdate && change.balance_changed()) {
        Moved(change.before().balance(), change.after().balance());
      }
    },
    .on_commit = [](const gendb::ChangePosition& position) { Store(position); },
});
```

To resume after being away, from a position stored earlier:

```cpp
auto subscription = db->SubscribeFrom(stored, handlers);
if (absl::IsOutOfRange(subscription.status())) { /* The log is gone: start over from a snapshot. */ }
```

See [the storage engine](storage_engine.md#change-subscriptions) for delivery, and what catching up
gives.

## Operate

```cpp
db->engine().Checkpoint();                 // Compact the log now, rather than when it has grown.
db->engine().SyncWal();                    // Make non-durable commits durable.
db->engine().Backup("/backups/today");     // A copy to open, while everything keeps running.
db->engine().Stats();                      // Positions, sizes, snapshots, the latest error.
```

Metrics and logs come through an [`EngineObserver`](../lib/gendb/observer.h): the library writes
nothing to stderr by itself.
