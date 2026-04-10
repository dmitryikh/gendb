# Docker Build And Test

This project can be configured, built, and tested entirely inside the Docker container defined by [Dockerfile](/Users/dmitry/code/gendb/Dockerfile).

## Build Image

From the repo root:

```bash
docker build -t gendb-dev .
```

## Clean Build Directory

If you want a fully fresh local build tree:

```bash
rm -rf build
```

## Configure, Build, And Test In Docker

Run the full workflow from the repo root:

```bash
docker run --rm \
  -v "$PWD":/workspace \
  -w /workspace \
  gendb-dev \
  bash -lc '
    set -euo pipefail
    /home/dev/.local/bin/conan profile detect --force
    /home/dev/.local/bin/conan install . --output-folder=build/docker-debug --build=missing -s build_type=Debug
    python3 gen/generate_message_class.py --fbs_dir lib/schemas --output_dir lib/gendb/generated
    python3 gen/generate_message_class.py --fbs_dir tests/schemas --output_dir tests/generated
    cmake -S . -B build/docker-debug -G Ninja \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_TOOLCHAIN_FILE=build/docker-debug/conan_toolchain.cmake \
      -DCMAKE_C_COMPILER=clang \
      -DCMAKE_CXX_COMPILER=clang++
    cmake --build build/docker-debug
    ctest --test-dir build/docker-debug --output-on-failure
  '
```

## Rebuild Using Existing `build/docker-debug`

If the build directory already exists and you only need to regenerate code, rebuild, and rerun tests:

```bash
docker run --rm \
  -v "$PWD":/workspace \
  -w /workspace \
  gendb-dev \
  bash -lc '
    set -euo pipefail
    python3 gen/generate_message_class.py --fbs_dir lib/schemas --output_dir lib/gendb/generated
    python3 gen/generate_message_class.py --fbs_dir tests/schemas --output_dir tests/generated
    cmake --build build/docker-debug
    ctest --test-dir build/docker-debug --output-on-failure
  '
```

## Notes

- The container installs Conan with `pipx`, so the Conan binary path is `/home/dev/.local/bin/conan`.
- The build output used in the validated workflow is `build/docker-debug`.
- The generated message headers under `lib/gendb/generated` and `tests/generated` are part of the normal Docker build flow.
