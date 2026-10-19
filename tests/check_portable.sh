#!/bin/bash
# The portable runtime compiles without platform code (docs/PORTING.md): every runtime/*.c with the POSIX platform
# file, as C11 with GNU extensions, for this machine and (on macOS) also for x86-64. No game files; no linking (the
# recompiled game is not part of it). An undeclared function is an error: that is how a platform-only call shows.
set -euo pipefail
cd "$(dirname "$0")/.."
CC=${CC:-cc}
REF=${REF:-vendor/freedom-force-recomp}
out=$(mktemp -d)
targets=("")
[[ $(uname -s) == Darwin ]] && targets=("-arch arm64" "-arch x86_64")
for t in "${targets[@]}"; do
  for f in runtime/*.c; do
    # shellcheck disable=SC2086
    "$CC" $t -std=gnu11 -c -w -Werror=implicit-function-declaration -I runtime -I "$REF/recomp/runtime" "$f" -o "$out/$(basename "$f").o"
  done
  echo "portable runtime: $(ls runtime/*.c | wc -l | tr -d ' ') files compile${t:+ ($t)}"
done
rm -rf "$out"
