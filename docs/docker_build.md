# Docker Build And Test

This project can be configured, built, and tested entirely inside the Docker container defined by [Dockerfile](../Dockerfile).

## Build Image

From the repo root:

```bash
docker build -t gendb-dev .
```

## Configure, Build, And Test

The named volume `gendb-conan` keeps Conan's package cache between runs, so RocksDB and the other dependencies are built only once. Run the full workflow from the repo root:

```bash
docker run --rm \
  -v "$PWD":/workspace \
  -w /workspace \
  -v gendb-conan:/home/dev/.conan2 \
  gendb-dev \
  bash -lc '
    set -euo pipefail
    sudo chown dev:dev /home/dev/.conan2
    /home/dev/.local/bin/conan profile detect --force
    /home/dev/.local/bin/conan install . --output-folder=build/docker-debug --build=missing -s build_type=Debug
    rm -f CMakeUserPresets.json
    cmake -S . -B build/docker-debug -G Ninja \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_TOOLCHAIN_FILE=$PWD/build/docker-debug/conan_toolchain.cmake \
      -DCMAKE_CXX_COMPILER=clang++
    cmake --build build/docker-debug
    ctest --test-dir build/docker-debug --output-on-failure
  '
```

The build regenerates the message classes and database code, including `lib/gendb/generated` and `tests/generated`. `ctest` runs the C++ tests and the generator's Python tests.

## Rebuild Using Existing `build/docker-debug`

```bash
docker run --rm \
  -v "$PWD":/workspace \
  -w /workspace \
  -v gendb-conan:/home/dev/.conan2 \
  gendb-dev \
  bash -lc '
    set -euo pipefail
    cmake --build build/docker-debug
    ctest --test-dir build/docker-debug --output-on-failure
  '
```

## Sanitizers

Reuses the Conan packages installed for `build/docker-debug`:

```bash
docker run --rm \
  -v "$PWD":/workspace \
  -w /workspace \
  -v gendb-conan:/home/dev/.conan2 \
  gendb-dev \
  bash -lc '
    set -euo pipefail
    cmake -S . -B build/docker-asan -G Ninja \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_TOOLCHAIN_FILE=$PWD/build/docker-debug/conan_toolchain.cmake \
      -DCMAKE_PREFIX_PATH=$PWD/build/docker-debug \
      -DCMAKE_CXX_COMPILER=clang++ \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer" \
      -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
    cmake --build build/docker-asan
    ctest --test-dir build/docker-asan --output-on-failure
  '
```

## Benchmarks

See [benchmarks/README.md](../benchmarks/README.md).
