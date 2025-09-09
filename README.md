# gendb
Fast &amp; Convenient embedded Database for C++. With batteries.

(early development stage)

Features:
  * [x] Strong typed & fast code generated DB.
  * [x] Secondary indices
  * [ ] Persistent storage
  * [ ] Snapshot isolation
  * [ ] ACID transactions
  * [ ] Durable transaction log
  * [ ] Counters
  * [ ] In-memory collections
  * [ ] Change data capture
  * [ ] Rocksdb as a storage engine.
  * Message format:
    * [x] Deserialization-free O(1) field reads
    * [x] Full forward and backward compatible schema evolution
    * [x] Partial updates
