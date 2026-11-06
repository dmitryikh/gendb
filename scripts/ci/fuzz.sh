#!/usr/bin/env bash
# Builds the fuzzers and runs each for FUZZ_SECONDS (default 60). A crash leaves its input in
# build/docker-fuzz/artifacts, to run again by passing it to the binary.
set -euo pipefail
seconds="${FUZZ_SECONDS:-60}"
build=build/docker-fuzz
flags="-fsanitize=fuzzer-no-link,address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -g"

conan install . --output-folder="$build" --build=missing -s build_type=Debug -o "&:with_grpc=False" >/dev/null
cmake -S . -B "$build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/$build/conan_toolchain.cmake" \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_CXX_FLAGS="$flags" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
  -DGENDB_BUILD_FUZZERS=ON >/dev/null
cmake --build "$build" --target fuzz_message fuzz_recovery fuzz_replication
mkdir -p "$build/artifacts"
for target in fuzz_message fuzz_recovery fuzz_replication; do
  corpus="$build/corpus/$target"
  mkdir -p "$corpus"
  echo "== $target"
  "./$build/fuzz/$target" "$corpus" \
    -max_total_time="$seconds" -max_len=2048 -print_final_stats=1 -timeout=20 \
    -artifact_prefix="$build/artifacts/$target-" 2>&1 | tail -12
done
