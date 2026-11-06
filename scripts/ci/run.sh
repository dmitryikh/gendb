#!/usr/bin/env bash
# One CI job, in the Docker image from the Dockerfile:
#   scripts/ci/run.sh format|schema|test|asan|tsan|release|package|example|fuzz
# The same runs locally:
#   docker run --rm -v "$PWD":/workspace -w /workspace -v gendb-conan:/home/dev/.conan2 gendb-dev \
#     scripts/ci/run.sh test
set -euo pipefail
export PATH="$HOME/.local/bin:$PATH"

# A new Docker volume for the cache belongs to root.
if [ -d "$HOME/.conan2" ] && [ ! -w "$HOME/.conan2" ]; then sudo chown "$(id -u):$(id -g)" "$HOME/.conan2"; fi

install_dependencies() {  # <build directory> <build type> <extra conan arguments...>
  local build="$1" type="$2"
  shift 2
  conan profile detect --force >/dev/null
  conan install . --output-folder="$build" --build=missing -s build_type="$type" "$@"
}

configure() {  # <build directory> <build type> <extra cmake arguments...>
  local build="$1" type="$2"
  shift 2
  cmake -S . -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE="$type" \
    -DCMAKE_TOOLCHAIN_FILE="$PWD/$build/conan_toolchain.cmake" \
    -DCMAKE_CXX_COMPILER=clang++ "$@"
}

without_grpc=(-o "&:with_grpc=False")

case "${1:-}" in
  format)
    git ls-files '*.h' '*.cpp' | grep -vE '(^|/)generated/|^third_party/' |
      xargs clang-format --dry-run --Werror --style=file
    ;;
  schema)
    # What a schema must not have changed since the branch started. Needs the git history.
    python3 gen/schema_evolution.py --base "${SCHEMA_BASE:-origin/main}"
    ;;
  test)
    install_dependencies build/ci-debug Debug "${without_grpc[@]}"
    configure build/ci-debug Debug
    cmake --build build/ci-debug
    ctest --test-dir build/ci-debug --output-on-failure --timeout 300
    ;;
  asan)
    install_dependencies build/ci-asan Debug "${without_grpc[@]}"
    configure build/ci-asan Debug \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -DNDEBUG_SANITIZER" \
      -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
    cmake --build build/ci-asan
    ctest --test-dir build/ci-asan --output-on-failure --timeout 300
    ;;
  tsan)
    install_dependencies build/ci-tsan Debug "${without_grpc[@]}"
    configure build/ci-tsan Debug \
      -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer" \
      -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"
    cmake --build build/ci-tsan
    ctest --test-dir build/ci-tsan --output-on-failure --timeout 600
    ;;
  release)
    # The only job with gRPC, whose first build takes long.
    install_dependencies build/ci-release Release
    configure build/ci-release Release
    cmake --build build/ci-release
    ctest --test-dir build/ci-release --output-on-failure --timeout 300
    # Two real processes, over both transports.
    demo=$PWD/build/ci-release/examples/replication_demo
    for transport in grpc tcp; do
      dir=$(mktemp -d)
      "$demo" primary "$transport" 7555 "$dir/primary" 100 &
      primary=$!
      sleep 0.5
      "$demo" replica "$transport" 7555 "$dir/replica" 100
      wait "$primary"
    done
    ;;
  package)
    install_dependencies build/ci-package Release "${without_grpc[@]}"
    configure build/ci-package Release -DCMAKE_INSTALL_PREFIX="$PWD/build/ci-package/installed" \
      -DGENDB_BUILD_TESTS=OFF
    cmake --build build/ci-package
    cmake --install build/ci-package
    cmake -S tests/package -B build/ci-package/consumer -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE="$PWD/build/ci-package/conan_toolchain.cmake" \
      -DCMAKE_CXX_COMPILER=clang++ \
      -Dgendb_DIR="$PWD/build/ci-package/installed/lib/cmake/gendb"
    cmake --build build/ci-package/consumer
    ./build/ci-package/consumer/package_test
    ;;
  example)
    # An application that takes GenDB from a Conan package, and two processes that use it.
    conan profile detect --force >/dev/null
    use_clang=(-c 'tools.build:compiler_executables={"c": "clang", "cpp": "clang++"}')
    conan create . --build=missing -s build_type=Release "${without_grpc[@]}" "${use_clang[@]}" \
      -c tools.build:skip_test=True
    conan install examples/tictactoe --output-folder=build/ci-example --build=missing \
      -s build_type=Release "${use_clang[@]}"
    cmake -S examples/tictactoe -B build/ci-example -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE="$PWD/build/ci-example/conan_toolchain.cmake" \
      -DCMAKE_CXX_COMPILER=clang++
    cmake --build build/ci-example
    examples/tictactoe/run_match.sh build/ci-example/tictactoe --n=5000
    ;;
  fuzz)
    FUZZ_SECONDS="${FUZZ_SECONDS:-60}" scripts/ci/fuzz.sh
    ;;
  *)
    echo "usage: $0 format|schema|test|asan|tsan|release|package|example|fuzz" >&2
    exit 2
    ;;
esac
