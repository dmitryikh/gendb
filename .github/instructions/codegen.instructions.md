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

## API
`docs/api.md` describes the generated API. Keep it in step with the database templates.

## Storage Engine
`docs/storage_engine.md` describes transactions, snapshots, durability, compaction and subscriptions. Read it before changing `lib/gendb/engine*`, `transaction.cpp`, `cursors.cpp`, `subscription.cpp`, `record_table.h`, `ordered_index.h`, `wal.*`, `checkpoint.*`, `log_format.h` or `file_*`. Their concurrency is lock-free with one writer: run the tests under ThreadSanitizer after any change. All disk access goes through `FileSystem`, and anything that changes what is synced when needs `fault_injection_test`, which cuts the power at every operation; try the wrong order too, to see that the test catches it. Files on disk must stay readable across versions, or bump `kFormatVersion`. The log holds patches with a checksum of their result, so the bytes `Rebuild` and `ApplyPatchInplace` produce are part of that format too: never update the pinned values in `ResultBytesArePartOfTheLogFormat` without handling old logs.

## Checks
`scripts/ci/run.sh` runs what CI runs, by job: see `docs/docker_build.md`. Run it in Docker.

## Replication
`docs/replication.md` describes the protocol and how transports plug in. The core in `lib/gendb/replication.*` must not depend on any network library: gRPC stays in `replication_grpc.*` and its optional target.

## Generated Code
`tests/generated` and `lib/gendb/generated` are generated and checked in. Edit the templates and regenerate by building; never edit the output by hand.
