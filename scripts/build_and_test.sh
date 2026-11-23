#!/usr/bin/env bash
# Configures, builds, and tests custom-memory-allocator from a clean
# clone:
#   - Debug build + CTest (25 GoogleTest cases: arena, free-list,
#     pmr adapter, randomized property test)
#   - AddressSanitizer + UndefinedBehaviorSanitizer build + CTest
#   - ThreadSanitizer build + CTest
#   - Release build of the benchmark CLI (NDEBUG, so the debug-only
#     O(n) invariant-check assertions are compiled out - see README
#     "Benchmark methodology" for why this matters) + a real
#     benchmark run against malloc/new, written to results/
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

if ! command -v cmake >/dev/null 2>&1; then
  echo "cmake not found on PATH; bootstrapping a project-local copy via pip (no sudo, no global install)..."
  python3 -m venv "$ROOT_DIR/.venv"
  "$ROOT_DIR/.venv/bin/pip" install --quiet --upgrade pip
  "$ROOT_DIR/.venv/bin/pip" install --quiet cmake ninja
  export PATH="$ROOT_DIR/.venv/bin:$PATH"
fi

GENERATOR="Unix Makefiles"
if command -v ninja >/dev/null 2>&1; then
  GENERATOR="Ninja"
fi

build_and_test() {
  local dir="$1"
  shift
  echo "=== Configuring $dir ($*) ==="
  cmake -S . -B "$dir" -G "$GENERATOR" -DCMAKE_BUILD_TYPE=Debug "$@"
  echo "=== Building $dir ==="
  cmake --build "$dir" --
  echo "=== Testing $dir ==="
  ctest --test-dir "$dir" --output-on-failure
}

build_and_test build
build_and_test build-asan -DENABLE_ASAN=ON
build_and_test build-tsan -DENABLE_TSAN=ON

echo "=== Configuring build-release (Release, benchmarks only) ==="
cmake -S . -B build-release -G "$GENERATOR" -DCMAKE_BUILD_TYPE=Release -DCMA_BUILD_TESTS=OFF
echo "=== Building build-release ==="
cmake --build build-release --

echo "=== Running benchmark (Release) -> results/benchmark_raw.csv ==="
mkdir -p results
./build-release/cma_benchmark 20000 > results/benchmark_raw.csv
cat results/benchmark_raw.csv

if [ ! -d "$ROOT_DIR/.venv" ] || [ ! -x "$ROOT_DIR/.venv/bin/python" ]; then
  python3 -m venv "$ROOT_DIR/.venv"
fi
"$ROOT_DIR/.venv/bin/pip" install --quiet --upgrade pip
"$ROOT_DIR/.venv/bin/pip" install --quiet -r requirements.txt
echo "=== Plotting benchmark results ==="
"$ROOT_DIR/.venv/bin/python" benchmarks/plot_results.py

echo
echo "All builds and tests passed."
