#!/bin/bash
# Visitor View on a synthetic park (no game data): tests/walk_test.c and the view test (tests/walk_view_test.c, which
# links view.c and its platform file, runtime/platform.h), any C compiler (macOS, Linux). Output in build/.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
CC=${CC:-cc}
PLAT=runtime/platform_posix.c; [[ $(uname -s) == Darwin ]] && PLAT=platform/macos/platform_macos.c
"$CC" -std=gnu11 -O1 -g -Wall -Wno-unused-function -Wno-misleading-indentation -fsanitize=address,undefined \
  tests/walk_test.c runtime/walk.c runtime/native_land.c runtime/native_draw.c -lm -lpthread -o build/walk_test
TP_WALK_MESH=0 build/walk_test
TP_WALK_MESH=1 build/walk_test
"$CC" -std=gnu11 -O1 -g -w -fsanitize=address,undefined -I runtime \
  tests/walk_view_test.c runtime/view.c runtime/walk.c runtime/native_land.c runtime/native_draw.c "$PLAT" -lm -lpthread -o build/walk_view_test
build/walk_view_test
"$CC" -std=gnu11 -O1 -g -Wall -Wno-unused-function -Wno-misleading-indentation -fsanitize=address,undefined \
  tests/walk_mesh_test.c -lm -o build/walk_mesh_test
build/walk_mesh_test
"$CC" -std=gnu11 -O1 -g -Wall -Wno-unused-function -Wno-misleading-indentation -fsanitize=address,undefined \
  tests/walk_model_test.c -lm -o build/walk_model_test
build/walk_model_test
"$CC" -std=gnu11 -O1 -g -Wall -Wno-unused-function -Wno-misleading-indentation -fsanitize=address,undefined \
  tests/walk_rig_test.c -lm -o build/walk_rig_test
build/walk_rig_test
