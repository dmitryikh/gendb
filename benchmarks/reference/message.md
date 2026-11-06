# Message benchmarks

Recorded at `2026-11-06T11:23:29+00:00`. Median CPU nanoseconds per operation; [methodology](../README.md).

| Record | Bytes | Open | Read hot | Read cold | Build | Serialize only | Patch same size | Rebuild variable | End to end | Verify |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Position | 38 | 2.85 | 2.71 | 5.71 | 37.02 | 16.76 | 3.46 | 19.79 | 43.38 | 6.59 |
| AccountFull | 98 | 2.89 | 4.67 | 7.77 | 56.11 | 22.51 | 3.38 | 20.67 | 65.95 | 9.05 |
| AccountSparse | 20 | 2.62 | 0.70 | 3.12 | 26.98 | 13.37 | 3.15 | 57.56 | 30.43 | 6.47 |
| WideFull64 | 529 | 2.61 | 1.59 | 3.97 | 230.60 | 123.87 | 3.43 | — | 232.18 | 5.94 |
| WideSparse8of64 | 81 | 2.69 | 1.61 | 3.92 | 49.08 | 16.50 | 3.36 | — | 53.37 | 5.93 |

## Generated classes

| Operation | Position |
| --- | ---: |
| Build | 33.81 |
| Read cold | 4.82 |

## Run context

- Logical CPUs: 12; the runner is pinned to one CPU.
- Repetitions per benchmark: 7.
- Benchmark library build: `release`.
- Largest CPU-time coefficient of variation: 87.1%.
- Warm-cache, single-threaded microbenchmarks, not database throughput.
