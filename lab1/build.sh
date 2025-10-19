#!/usr/bin/env bash
set -euo pipefail

# Папки
ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD="$ROOT/.build"
BIN="$ROOT/bin"

mkdir -p "$BUILD" "$BIN"

# Компиляция в объектные файлы (промежуточные артефакты)
g++ -std=c++17 -O2 -Wall -Werror -Isrc -c src/main.cpp -o "$BUILD/main.o"

# Линковка в один бинарник
g++ "$BUILD/main.o" -o "$BIN/lab1d"

# Очистка промежуточных файлов
rm -rf "$BUILD"

echo "Built: $BIN/lab1d"