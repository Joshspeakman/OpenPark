#!/bin/bash
# tools/fetch_wheels.sh <folder>: unicorn's wheel for the drag-and-drop app's Python (3.12, arm64). Setup on the player's Mac
# installs it, with no download, to trace the game's start as tests/check_macos.sh --park does (tools/trace_startup.py),
# so the build has the traces of the player's own copy and the app ships none. unicorn is GPL-2.0 (the app runs it as a
# separate tool; its source: https://github.com/unicorn-engine/unicorn/tree/2.1.4). Cached in ~/ProjectBuilds/_toolkit/wheels.
set -euo pipefail
OUT="${1:?usage: fetch_wheels.sh <folder>}"; VER=2.1.4; CACHE="$HOME/ProjectBuilds/_toolkit/wheels"
mkdir -p "$CACHE" "$OUT"
ls "$CACHE"/unicorn-$VER-*macosx*arm64.whl >/dev/null 2>&1 ||
  python3 -m pip download -q --only-binary=:all: --platform macosx_14_0_arm64 --python-version 3.12 --implementation cp --no-deps "unicorn==$VER" -d "$CACHE"
cp "$CACHE"/unicorn-$VER-*macosx*arm64.whl "$OUT/"
