#!/usr/bin/env bash
set -euo pipefail

docker run --rm \
  -v "$PWD":/workspace \
  -w /workspace \
  gendb-dev \
  bash -lc '
    set -euo pipefail
    /home/dev/.local/bin/conan profile detect --force
    /home/dev/.local/bin/conan install . --output-folder=build/docker-release --build=missing -s build_type=Release
    cmake -S . -B build/docker-release -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=build/docker-release/conan_toolchain.cmake \
      -DCMAKE_C_COMPILER=clang \
      -DCMAKE_CXX_COMPILER=clang++
    cmake --build build/docker-release --target gendb_message_benchmarks --target flatbuffers_message_benchmarks
    ./build/docker-release/benchmarks/gendb_message_benchmarks --benchmark_min_time=0.05s
    ./build/docker-release/benchmarks/flatbuffers_message_benchmarks --benchmark_min_time=0.05s
  '
