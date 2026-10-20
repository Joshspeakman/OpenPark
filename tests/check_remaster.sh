#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
CC=${CC:-cc}
for name in remaster_art remaster_mesh; do
  "$CC" -std=gnu11 -O1 -g -Wall -Wno-unused-function -Wno-misleading-indentation \
    -fsanitize=address,undefined "tests/${name}_test.c" -lm -o "build/${name}_test"
  "build/${name}_test"
done
python3 -m unittest discover -s tests -p 'remaster_*_test.py'
"$CC" -std=gnu11 -O2 -Wall -Wno-unused-function tools/remaster_bake.c -lm -o build/remaster_bake
