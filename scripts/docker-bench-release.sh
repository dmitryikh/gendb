#!/usr/bin/env bash
# Builds the message benchmarks in Release inside Docker and records them as
# <output prefix>.json and <output prefix>.md (default: benchmarks/reference/message), and the
# database benchmarks as benchmarks/reference/db.txt.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"
output_prefix="${1:-benchmarks/reference/message}"
mkdir -p "$(dirname "$output_prefix")"

docker run --rm \
  -v "$repo_root":/workspace \
  -w /workspace \
  -v gendb-conan:/home/dev/.conan2 \
  -e OUTPUT_PREFIX="$output_prefix" \
  -e BENCHMARK_MIN_TIME="${BENCHMARK_MIN_TIME:-0.1s}" \
  -e BENCHMARK_REPETITIONS="${BENCHMARK_REPETITIONS:-7}" \
  gendb-dev \
  bash -lc '
    set -euo pipefail
    sudo chown dev:dev /home/dev/.conan2
    /home/dev/.local/bin/conan profile detect --force
    /home/dev/.local/bin/conan install . --output-folder=build/docker-release --build=missing -s build_type=Release
    cmake -S . -B build/docker-release -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=$PWD/build/docker-release/conan_toolchain.cmake \
      -DCMAKE_CXX_COMPILER=clang++
    cmake --build build/docker-release --target message_benchmarks db_benchmarks
    taskset -c 0 ./build/docker-release/benchmarks/message_benchmarks \
      --benchmark_min_time="$BENCHMARK_MIN_TIME" \
      --benchmark_repetitions="$BENCHMARK_REPETITIONS" \
      --benchmark_enable_random_interleaving=true \
      --benchmark_report_aggregates_only=true \
      --benchmark_out="$OUTPUT_PREFIX.json" \
      --benchmark_out_format=json
    python3 benchmarks/summarize.py "$OUTPUT_PREFIX.json" --output "$OUTPUT_PREFIX.md"
    ./build/docker-release/benchmarks/db_benchmarks --benchmark_min_time=0.5s \
      --benchmark_report_aggregates_only=true \
      --benchmark_out=benchmarks/reference/db.txt --benchmark_out_format=console
  '
