#!/bin/bash
# The SDL2 app (platform/sdl/, Linux and Windows) compiles clean with today's runtime and links, against stand-ins for
# the recompiled game (tests/stub_game.c): no game files (the CI). Then it starts and stops cleanly at "no game files"
# (SDL's dummy video driver where there is no display). The build with the game is tools/build_linux.sh /
# tools/build_windows.sh; the checks with it are tests/check_native.sh --park.
#   tests/check_sdl.sh            (CC: the compiler; MSYS2 CLANG64 on Windows, any Linux, macOS for development)
set -euo pipefail
cd "$(dirname "$0")/.."
CC=${CC:-cc}
command -v sdl2-config >/dev/null || command -v pkg-config >/dev/null || { echo 'SDL2 development files not found' >&2; exit 1; }
SDL_CFLAGS=$(sdl2-config --cflags 2>/dev/null || pkg-config --cflags sdl2); SDL_LIBS=$(sdl2-config --libs 2>/dev/null || pkg-config --libs sdl2)
RUNTIME=(runtime/host.c runtime/sha256.c runtime/sb16.c runtime/verify.c runtime/view.c runtime/native_land.c runtime/native_draw.c
         runtime/qol.c runtime/walk.c runtime/bigmap.c runtime/freemode.c runtime/cinepak.c)
case $(uname -s) in
  Darwin) PLAT=(platform/macos/platform_macos.c); LIBS=(-lz) ;;
  MINGW*|MSYS*|CYGWIN*) PLAT=(platform/windows/platform_windows.c runtime/platform_pool.c); LIBS=(-lz -lwinmm -lpthread) ;;
  *) PLAT=(runtime/platform_posix.c runtime/platform_pool.c); LIBS=(-lz -lm -lpthread) ;;
esac
mkdir -p build/check-sdl
INC=vendor/freedom-force-recomp/recomp/runtime
# The app's own files: warnings are errors (the runtime is checked by tests/check_portable.sh)
# shellcheck disable=SC2086
"$CC" -std=gnu11 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wno-sign-compare -Werror -I "$INC" -I runtime $SDL_CFLAGS \
  -c platform/sdl/main.c -o build/check-sdl/main.o
# shellcheck disable=SC2086
"$CC" -std=gnu11 -O1 -Wall -Wextra -Wno-unused-parameter -Werror -I "$INC" -I runtime $SDL_CFLAGS -c platform/sdl/audio.c -o build/check-sdl/audio.o
# shellcheck disable=SC2086
"$CC" -std=gnu11 -O1 -Wall -Wextra -Wno-unused-parameter -Werror -I runtime $SDL_CFLAGS -c platform/sdl/settings.c -o build/check-sdl/settings.o
# shellcheck disable=SC2086
"$CC" -std=gnu11 -O1 -w -I "$INC" -I runtime build/check-sdl/main.o build/check-sdl/audio.o build/check-sdl/settings.o \
  "${RUNTIME[@]}" "${PLAT[@]}" tests/stub_game.c $SDL_LIBS "${LIBS[@]}" -o build/check-sdl/ThemePark
echo "the SDL app compiles and links ($(uname -s))"
scratch=$(mktemp -d)
set +e
out=$(DS_TEST=1 W32_AUDIO_MUTE=1 TP_DATA="$scratch/none" SDL_VIDEODRIVER=${SDL_VIDEODRIVER:-dummy} SDL_AUDIODRIVER=dummy \
      build/check-sdl/ThemePark 2>&1); rc=$?
set -e
rm -rf "$scratch"
if [[ $rc -ne 1 || $out != *"game files were not found"* ]]; then echo "the app did not stop cleanly without game files (exit $rc):" >&2; echo "$out" >&2; exit 1; fi
echo "the SDL app starts and stops cleanly without game files"
