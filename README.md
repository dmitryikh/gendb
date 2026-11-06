# gendb
Fast &amp; Convenient embedded Database for C++. With batteries.

(early development stage)

Features:
  * [x] Strong typed Key-Value collections.
  * [x] Schema-driven, code-generated C++ API, from its own schema language with no compiler to install
  * [x] Evolution rules as syntax: field numbers only grow, and old fields do not change
  * [x] Field defaults, kept apart from whether a field is set
  * [x] Secondary indexes: composite, unique, with open ranges, reverse order and limits
  * [x] Persistent storage
  * [x] Snapshot isolation (multi-version concurrency control)
  * [x] ACID transactions
  * [x] Own write-ahead log, durable or non-durable per commit, and background compaction into checkpoints
  * [x] Crash recovery, tested against simulated power loss at every disk operation
  * [x] Hot backup
  * [x] Durable atomic ID counters
  * [x] Ephemeral in-memory collections
  * [x] Change subscriptions: follow chosen tables after each commit, see what changed, and resume after being away
  * [x] Replication: a replica fetches a checkpoint and streams commits, over a transport you choose (gRPC included), and continually verifies its state
  * [x] Upgrades in any order: collections a side does not know are kept for it
  * Message format:
    * [x] Zero-copy, deserialization-free O(1) field reads
    * [x] Nested messages and repeated fields of any type, read without copying
    * [x] Forward and backward compatible schema evolution
    * [x] Unknown fields preserved through every update
    * [x] Partial updates, in place when the size does not change


Non goals:
  * Db schema updates without recompilation
  * Heavyweight server process (runs embedded in-process, not as a separate daemon)
  * Multi-language bindings (C++ is the first-class and only target)
  * Ad-hoc SQL query engine (no query parser or optimizer, API-only access)

Documentation:
  * Using it: [docs/api.md](docs/api.md)
  * Schema language: [docs/schema.md](docs/schema.md)
  * Message format: [specification](docs/message_format.md) and [interactive explorer](docs/message_format.html) (open it in a browser)
  * Storage engine: [docs/storage_engine.md](docs/storage_engine.md)
  * Replication and writing a transport: [docs/replication.md](docs/replication.md)
  * Build and test in Docker: [docs/docker_build.md](docs/docker_build.md)
  * Benchmarks: [benchmarks/README.md](benchmarks/README.md)

License: [MIT](LICENSE)
