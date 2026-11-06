# Message benchmarks

`message_benchmark.cpp` measures the [message format](../docs/message_format.md), with standard
generated FlatBuffers tables as the baseline. Recorded results are in
[reference/message.md](reference/message.md).

## Run

From the repository root, with Docker running and the `gendb-dev` image built:

```bash
./scripts/docker-bench-release.sh
```

This builds a Release binary, pins it to one CPU, and writes `benchmarks/reference/message.json` and
`message.md`: seven randomly interleaved repetitions of at least 0.1 s each. Other settings:

```bash
BENCHMARK_MIN_TIME=0.2s BENCHMARK_REPETITIONS=9 ./scripts/docker-bench-release.sh build/my_run
```

Compare medians across more than one run. A benchmark can shift by tens of percent between
processes while its variation within one process stays low, so a low coefficient of variation alone
does not make a difference real.

## Records

All formats encode identical fields and values.

| Record | Present fields | Timed reads |
| --- | ---: | --- |
| Position | 6 | All six fields, including one string |
| AccountFull | 8 | All eight fields, including four strings |
| AccountSparse | 2 of 8 | Both present fields |
| WideFull64 | 64 | IDs 1, 2, 32, 64 |
| WideSparse8of64 | 8 of 64 | IDs 1, 2, 32, 64 |

## Operations

| Operation | What is timed |
| --- | --- |
| Build | Populate a fresh builder, serialize into a fresh buffer, destroy temporaries |
| SerializeOnly | Serialize an already populated builder into a fresh buffer |
| Open | Construct a view over serialized bytes |
| ReadHot | Read the listed fields from an already opened view |
| ReadCold | Open a view, then read the listed fields. The cache is warm: "cold" means a fresh view |
| EndToEnd | Build, serialize, open and read |
| PatchSameSize | Overwrite one scalar in place, with no full verification |
| RebuildVariable | Replace a string with one of a different length. AccountSparse inserts an absent field instead, which re-encodes the message |
| Verify | Full verification of untrusted bytes, for messages only |

`Position/Generated/*` repeats Build and ReadCold through the generated classes, to show they add
nothing over the core API.

The FlatBuffers builder copies a source-data snapshot rather than field setters, so its Build and
SerializeOnly nearly coincide. Its scalar patch uses the generated in-place mutator, and its rebuild
uses the object API, which does not preserve unknown fields.

These are warm-cache, single-threaded, per-message measurements, not database throughput. Every
fixture is checked before timing, and buffers escape through benchmark barriers.

## Database benchmarks

`db_benchmark.cpp` measures the generated API over 100,000 records: gets, index and collection
scans, commits durable and not, subscribers, checkpoints, recovery, and replication over an
in-process transport. The same script records them in [reference/db.txt](reference/db.txt).
