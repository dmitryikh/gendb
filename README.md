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
    * [x] Deserialization-free O(1) field reads
    * [x] Full forward and backward compatible schema evolution
    * [x] Partial updates


Non goals:
  * Db schema updates without recompilation
  * Heavyweight server process (runs embedded in-process, not as a separate daemon)
  * Multi-language bindings (C++ is the first-class and only target)
  * Ad-hoc SQL query engine (no query parser or optimizer, API-only access)
