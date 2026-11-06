---
applyTo: "**"
---
# DB Code Generation Instructions

## General Flow
1. The database schema is defined in yaml files. Example of such file is `tests/schemas/db.yaml`. This file includes references to FlatBuffers schema files (with `.fbs` extension) that define the structure of the data.

2. The CMakeLists.txt file contains a custom CMake function `gen_db_schema` that is responsible for generating the database code based on the provided schema files. Example usage of this function can be found in `tests/CMakeLists.txt`. `gen_db_messages` is a similar function that generates only message classes.

3. The code generation is performed by a python script `gen/gen_database.py`. The script uses jinja2 templates for generating the C++ code. The database templates are located at `gen/templates/database_template.h.jinja2` and `gen/templates/database_template.cpp.jinja2`.

`gen/generate_message_class.py` is a similar script that generates only message classes. Message template is located at `gen/templates/message_class_template.h.jinja2`.

## Naming Conventions
- Use PascalCase for Class/Function/Enum names.
- Use snake_case for variable and function argument names.

## Message Format
`docs/message_format.md` specifies the bytes of every stored record and lists its principles: zero-copy reads, forward and backward compatibility, unknown field preservation. Read it before changing `lib/gendb/message.h`, `lib/gendb/message.cpp` or the message template, and keep it and `docs/message_format.html` in sync with the code.

Schemas evolve only in backward-compatible ways: a field keeps its ID and type forever, new fields are appended, and unused fields get `(deprecated)` instead of being removed.

## Storage Engine
`docs/storage_engine.md` describes transactions, snapshots, durability, compaction and subscriptions. Read it before changing `lib/gendb/engine.*`, `record_table.h`, `ordered_index.h`, `wal.*`, `checkpoint.*`, `log_format.h` or `file_io.*`. Their concurrency is lock-free with one writer: run `SnapshotTest` and `ReplicationTest` under ThreadSanitizer after any change. Files on disk must stay readable across versions, or bump `kFormatVersion`.

## Replication
`docs/replication.md` describes the protocol and how transports plug in. The core in `lib/gendb/replication.*` must not depend on any network library: gRPC stays in `replication_grpc.*` and its optional target.

## Generated Code
`tests/generated` and `lib/gendb/generated` are generated and checked in. Edit the templates and regenerate by building; never edit the output by hand.
