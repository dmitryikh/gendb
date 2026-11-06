# gendb
Fast &amp; Convenient embedded Database for C++. With batteries.

(early development stage)

Features:
  * [x] Strong typed Key-Value collections.
  * [x] Schema-driven, code-generated C++ API
  * [x] Secondary indexes (efficient lookups on non-primary keys)
  * [x] Persistent storage
  * [x] Snapshot isolation (multi-version concurrency control)
  * [x] ACID transactions
  * [x] Write-ahead log (WAL) for crash recovery
  * [x] Durable atomic ID counters
  * [x] Ephemeral in-memory collections
  * [x] Change subscriptions: follow chosen tables after each commit, with what changed
  * [x] Own write-ahead log, durable or non-durable per commit, and background compaction into checkpoints
  * [x] Replication: a replica fetches a checkpoint and streams commits, over a transport you choose (gRPC included)
  * Message format:
    * [x] Zero-copy, deserialization-free O(1) field reads
    * [x] Forward and backward compatible schema evolution
    * [x] Unknown fields preserved through every update
    * [x] Partial updates, in place when the size does not change


Non goals:
  * Db schema updates without recompilation
  * Heavyweight server process (runs embedded in-process, not as a separate daemon)
  * Multi-language bindings (C++ is the first-class and only target)
  * Ad-hoc SQL query engine (no query parser or optimizer, API-only access)

Documentation:
  * Message format: [specification](docs/message_format.md) and [interactive explorer](docs/message_format.html) (open it in a browser)
  * Storage engine: [docs/storage_engine.md](docs/storage_engine.md)
  * Replication and writing a transport: [docs/replication.md](docs/replication.md)
  * Generated database API: [docs/db_api_codegen.md](docs/db_api_codegen.md)
  * Build and test in Docker: [docs/docker_build.md](docs/docker_build.md)
  * Benchmarks: [benchmarks/README.md](benchmarks/README.md)
