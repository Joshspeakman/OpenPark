#!/bin/bash
# Visitor View on a synthetic park (no game data): tests/walk_test.c, any C compiler (Linux too); the view test
# (tests/walk_view_test.c) links view.c, which uses Mach timing and libdispatch, so it runs on macOS only. Output in build/.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
CC=${CC:-cc}
"$CC" -std=gnu11 -O1 -g -Wall -Wno-unused-function -Wno-misleading-indentation -fsanitize=address,undefined \
  tests/walk_test.c runtime/walk.c runtime/native_land.c runtime/native_draw.c -lm -lpthread -o build/walk_test
build/walk_test
[[ $(uname -s) == Darwin ]] || { echo 'walk view test: skipped (macOS only)'; exit 0; }
"$CC" -std=gnu11 -O1 -g -w -fsanitize=address,undefined \
  tests/walk_view_test.c runtime/view.c runtime/walk.c runtime/native_land.c runtime/native_draw.c -lm -lpthread -o build/walk_view_test
build/walk_view_test
