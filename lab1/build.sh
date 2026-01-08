#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD="$ROOT/.build"
BIN="$ROOT/bin"

mkdir -p "$BUILD" "$BIN"

SRCS=(
  src/main.cpp
  src/daemon.cpp
  src/config.cpp
  src/file_worker.cpp
  src/daemon_utils.cpp
  src/utils.cpp
)

for s in "${SRCS[@]}"; do
  o="$BUILD/$(basename "${s%.cpp}.o")"
  g++ -std=c++17 -O2 -Wall -Werror -Isrc -c "$ROOT/$s" -o "$o"
done

g++ "$BUILD"/*.o -o "$BIN/lab1d"

rm -rf "$BUILD"
echo "Built: $BIN/lab1d"
