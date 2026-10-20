#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
CC=${CC:-cc}
for name in remaster_art remaster_mesh remaster_rgba remaster_cache; do
  extra=""; [[ "$name" == remaster_rgba ]] && extra="runtime/native_land.c"
  "$CC" -std=gnu11 -O1 -g -Wall -Wno-unused-function -Wno-misleading-indentation \
    -fsanitize=address,undefined "tests/${name}_test.c" $extra -lm -o "build/${name}_test"
  "build/${name}_test"
done
"$CC" -std=gnu11 -O1 -g -Wall -Wno-unused-function -Wno-misleading-indentation \
  -fsanitize=address,undefined tests/remaster_snapshot_test.c runtime/native_land.c \
  runtime/native_draw.c -lm -lpthread -o build/remaster_snapshot_test
build/remaster_snapshot_test
"$CC" -std=gnu11 -O1 -g -Wall -Wno-unused-function -Wno-misleading-indentation \
  -fsanitize=address,undefined tests/remaster_tracks_test.c runtime/native_land.c \
  runtime/native_draw.c -lm -lpthread -o build/remaster_tracks_test
build/remaster_tracks_test
"$CC" -std=gnu11 -O1 -g -Wall -Wno-unused-function -Wno-misleading-indentation \
  -fsanitize=address,undefined tests/remaster_cars_test.c runtime/native_land.c \
  runtime/native_draw.c -lm -lpthread -o build/remaster_cars_test
"$CC" -std=gnu11 -O1 -g -Wall -Wno-unused-function -fsanitize=address,undefined \
  tests/remaster_motion_test.c -lm -o build/remaster_motion_test
build/remaster_motion_test
build/remaster_cars_test
"$CC" -std=gnu11 -O1 -g -Wall -Wno-unused-function -Wno-misleading-indentation \
  -fsanitize=address,undefined tests/remaster_camera_test.c runtime/native_land.c \
  runtime/native_draw.c -lm -lpthread -o build/remaster_camera_test
build/remaster_camera_test
PLAT=runtime/platform_posix.c; [[ $(uname -s) == Darwin ]] && PLAT=platform/macos/platform_macos.c
"$CC" -std=gnu11 -O1 -g -Wall -Wno-unused-function -Wno-unused-variable -Wno-misleading-indentation \
  -fsanitize=address,undefined -I runtime tests/remaster_view_test.c runtime/walk.c runtime/native_land.c \
  runtime/native_draw.c "$PLAT" -lm -lpthread -o build/remaster_view_test
build/remaster_view_test
"$CC" -std=gnu11 -O2 -Wall -Wno-unused-function tools/remaster_bake.c -lm -o build/remaster_bake
python3 -m unittest discover -s tests -p 'remaster_*_test.py'
