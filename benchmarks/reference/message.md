# Message benchmarks

Recorded at `2026-11-05T22:29:30+00:00`. Median CPU nanoseconds per operation; [methodology](../README.md).

| Record | Message bytes | FlatBuffers bytes |
| --- | ---: | ---: |
| Position | 38 | 60 |
| AccountFull | 98 | 160 |
| AccountSparse | 20 | 40 |
| WideFull64 | 529 | 656 |
| WideSparse8of64 | 81 | 208 |

## Position

| Format | Open | Read hot | Read cold | Build | Serialize only | Patch same size | Rebuild variable | End to end | Verify |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Message | 2.77 | 2.61 | 5.55 | 33.11 | 16.38 | 3.25 | 18.95 | 41.60 | 6.43 |
| FlatBuffers | 0.29 | 2.10 | 2.61 | 38.19 | 38.19 | 0.51 | 64.52 | 40.39 | — |

## AccountFull

| Format | Open | Read hot | Read cold | Build | Serialize only | Patch same size | Rebuild variable | End to end | Verify |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Message | 2.78 | 4.74 | 7.57 | 54.38 | 22.24 | 3.28 | 19.92 | 64.12 | 8.64 |
| FlatBuffers | 0.29 | 3.03 | 3.40 | 66.95 | 66.97 | 0.51 | 105.94 | 70.35 | — |

## AccountSparse

| Format | Open | Read hot | Read cold | Build | Serialize only | Patch same size | Rebuild variable | End to end | Verify |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Message | 2.52 | 0.70 | 2.94 | 30.72 | 13.11 | 3.02 | 56.96 | 29.51 | 6.28 |
| FlatBuffers | 0.34 | 0.69 | 1.05 | 24.81 | 24.81 | 0.52 | 57.13 | 25.22 | — |

## WideFull64

| Format | Open | Read hot | Read cold | Build | Serialize only | Patch same size | Rebuild variable | End to end | Verify |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Message | 2.53 | 1.54 | 3.74 | 218.01 | 117.66 | 3.10 | — | 225.70 | 5.80 |
| FlatBuffers | 0.29 | 1.45 | 1.73 | 185.35 | 186.18 | 0.52 | — | 187.73 | — |

## WideSparse8of64

| Format | Open | Read hot | Read cold | Build | Serialize only | Patch same size | Rebuild variable | End to end | Verify |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Message | 2.52 | 1.55 | 3.68 | 46.76 | 16.94 | 3.12 | — | 50.61 | 5.79 |
| FlatBuffers | 0.29 | 1.37 | 1.68 | 38.03 | 38.15 | 0.52 | — | 40.12 | — |

## Generated classes

| Operation | Position |
| --- | ---: |
| Build | 33.58 |
| Read cold | 4.67 |

## Run context

- Logical CPUs: 12; the runner is pinned to one CPU.
- Repetitions per benchmark: 7.
- Benchmark library build: `release`.
- Largest CPU-time coefficient of variation: 49.7%.
- Warm-cache, single-threaded microbenchmarks, not database throughput.
