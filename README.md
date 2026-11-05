# gendb
Fast &amp; Convenient embedded Database for C++. With batteries.

(early development stage)

Features:
  * [x] Strong typed Key-Value collections.
  * [x] Schema-driven, code-generated C++ API
  * [x] Secondary indexes (efficient lookups on non-primary keys)
  * [ ] Persistent storage
  * [ ] Snapshot isolation (multi-version concurrency control)
  * [ ] ACID transactions
  * [ ] Write-ahead log (WAL) for crash recovery
  * [ ] Durable atomic ID counters
  * [ ] Ephemeral in-memory collections
  * [ ] Change data capture
  * [ ] Rocksdb as a storage engine
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
  * Generated database API: [docs/db_api_codegen.md](docs/db_api_codegen.md)
  * Build and test in Docker: [docs/docker_build.md](docs/docker_build.md)
  * Benchmarks: [benchmarks/README.md](benchmarks/README.md)
