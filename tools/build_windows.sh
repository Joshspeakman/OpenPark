#!/bin/bash
# Build Theme Park for Windows from the owned package and install it for this user (the counterpart of
# tools/build_app.sh on macOS). Run in MSYS2's CLANG64 shell (CLANGARM64 on ARM64 Windows):
#   pacman -S mingw-w64-clang-x86_64-clang mingw-w64-clang-x86_64-SDL2 mingw-w64-clang-x86_64-zlib mingw-w64-clang-x86_64-pkgconf
#   py -3 -m venv .venv && .venv/Scripts/python.exe -m pip install -r requirements.txt    (python.org's Python)
#   tools/build_windows.sh [package.pkg] [reference-checkout (default: the vendored copy)]
# Installs the program in %LOCALAPPDATA%\Programs\ThemeParkNative with a Start menu entry. Game files are copied
# from the verified extraction into %LOCALAPPDATA%\ThemeParkNative\game (never into git); saves live in
# %LOCALAPPDATA%\ThemeParkNative\saves. A replaced install goes to the Recycle Bin.
# TP_FLUIDSYNTH=1 (with mingw-w64-clang-x86_64-fluidsynth): the music through FluidSynth and a SoundFont of the
# player's (settings.ini SoundBank) instead of Windows' own General MIDI synthesizer.
set -euo pipefail
cd "$(dirname "$0")/.."
command -v cygpath >/dev/null || { echo 'run this in an MSYS2 CLANG64 shell' >&2; exit 1; }
win() { cygpath -w "$1"; }                    # (python.org's Python takes Windows paths)
PKG=${1:-$(cygpath -u "$USERPROFILE")/Downloads/theme_park_enUS_1_0_33010.pkg}
REF=${2:-vendor/freedom-force-recomp}            # (the pinned lifter, vendored; config/reference.json checks it)
if [[ -n ${TP_PYTHON:-} ]]; then PYTHON=$TP_PYTHON
elif [[ -x .venv/Scripts/python.exe ]]; then PYTHON=.venv/Scripts/python.exe
else echo 'no Python: py -3 -m venv .venv && .venv/Scripts/python.exe -m pip install -r requirements.txt' >&2; exit 1; fi
LOCAL=$(cygpath -u "$LOCALAPPDATA")
DATA=${TP_DATA:-$LOCAL/ThemeParkNative}
DEST=${TP_INSTALL_DIR:-$LOCAL/Programs/ThemeParkNative}
GAMEDIR=work/extracted/payload/Contents/Resources/game
EXE=$GAMEDIR/GAME/MAIN.EXE
OUT=build/windows
CC=${TP_CC:-clang}
export TP_CC=$CC
command -v llvm-ar >/dev/null && export TP_AR=${TP_AR:-llvm-ar}
command -v "$CC" >/dev/null || { echo "$CC not found: pacman -S mingw-w64-clang-x86_64-clang" >&2; exit 1; }
pkg-config --exists sdl2 || { echo 'SDL2 not found: pacman -S mingw-w64-clang-x86_64-SDL2' >&2; exit 1; }
[[ -f $EXE ]] || "$PYTHON" tools/prepare.py "$(win "$PKG")"
[[ -f work/main-survey.json ]] || "$PYTHON" tools/survey.py "$EXE" --out work/main-survey.json
# The verified trace supplies observed block extents and the self-modified
# allocation variant; build it with tests/check_native.sh ... --park first.
TRACE=work/check-startup/trace.json
[[ -f $TRACE ]] || { echo "missing $TRACE: run tests/check_native.sh $EXE $REF --park" >&2; exit 1; }
# HMI sound drivers the game loads at run time (work/drv-ADDR-MODE-NAME.bin, see tools/dump_drivers.md):
# compiled at their load addresses. Made from the owned package on the first build; TP_NO_SOUND=1 skips them.
if [[ -z ${TP_NO_SOUND:-} ]] && ! ls work/drv-*-*-*.bin >/dev/null 2>&1; then
  echo "== making the sound driver images from the game (once, a few minutes)"
  "$PYTHON" tools/make_drivers.py "$EXE" --reference "$REF" --out work | tail -1 || echo "   (failed: building without sound)" >&2
fi
HAVE_DRIVERS=0; [[ -z ${TP_NO_SOUND:-} ]] && ls work/drv-*-*-*.bin >/dev/null 2>&1 && HAVE_DRIVERS=1
[[ $HAVE_DRIVERS == 1 ]] || echo "warning: no sound driver images; the game will run with sound off" >&2
DRV=(); [[ $HAVE_DRIVERS == 1 ]] && DRV=(--drivers work)
"$PYTHON" tools/build_game.py "$EXE" --reference "$REF" --trace "$TRACE" --survey work/main-survey.json --out work/app-release --opt=-O2 ${DRV[@]+"${DRV[@]}"} >/dev/null
# INTRO.EXE: its own image, linked beside MAIN.EXE's with an intro_ prefix.
INTRO=$GAMEDIR/GAME/INTRO.EXE
[[ -f work/intro-survey.json ]] || "$PYTHON" tools/survey.py "$INTRO" --out work/intro-survey.json
[[ -f work/intro-trace/trace.json ]] || DS_TEST=1 W32_AUDIO_MUTE=1 "$PYTHON" tools/trace_startup.py "$INTRO" --out work/intro-trace --quantum 1000 --blocks 30000000 --seconds 240 >/dev/null
"$PYTHON" tools/build_game.py "$INTRO" --reference "$REF" --trace work/intro-trace/trace.json --survey work/intro-survey.json --out work/app-release-intro --opt=-O2 --prefix intro_ ${DRV[@]+"${DRV[@]}"} >/dev/null

mkdir -p "$OUT"
NEW=$OUT/ThemeParkNative.new
rm -rf "$NEW"; mkdir -p "$NEW"
INC="$REF/recomp/runtime"
# The icon (GOG's, from the package) and the manifest, into the program.
ICO=$GAMEDIR/goggame-1207660423.ico
{ [[ -f $ICO ]] && printf '1 ICON "%s"\n' "$(cygpath -m "$PWD/$ICO")"; printf '1 24 "%s"\n' "$(cygpath -m "$PWD/app-sdl/windows.manifest")"; } > "$OUT/app.rc"
WINDRES=$(command -v llvm-windres || command -v windres)
"$WINDRES" "$OUT/app.rc" -O coff -o "$OUT/app.res.o"
[[ -f $ICO ]] && "$PYTHON" tools/ico_image.py "$ICO" --bmp "$NEW/icon.bmp" >/dev/null || true
SYNTH=(); SYNTH_LIBS=(-static)
if [[ -n ${TP_FLUIDSYNTH:-} ]]; then
  pkg-config --exists fluidsynth || { echo 'FluidSynth not found: pacman -S mingw-w64-clang-x86_64-fluidsynth' >&2; exit 1; }
  SYNTH=(-DTP_FLUIDSYNTH $(pkg-config --cflags fluidsynth)); SYNTH_LIBS=($(pkg-config --libs fluidsynth))
fi
RUNTIME=(runtime/host.c runtime/sb16.c runtime/verify.c runtime/view.c runtime/native_land.c runtime/native_draw.c runtime/qol.c
         runtime/walk.c runtime/bigmap.c runtime/freemode.c runtime/platform.c)
"$CC" -O2 -Wall -Wno-unused-function -D_POSIX_THREAD_SAFE_FUNCTIONS=200112L -I "$INC" -I runtime $(pkg-config --cflags sdl2) ${SYNTH[@]+"${SYNTH[@]}"} \
  app-sdl/main.c app-sdl/audio.c app-sdl/settings.c "${RUNTIME[@]}" work/app-release/game.a work/app-release-intro/game.a "$OUT/app.res.o" \
  ${SYNTH_LIBS[@]+"${SYNTH_LIBS[@]}"} $(pkg-config --static --libs sdl2) -lz -lwinmm -lpthread -o "$NEW/ThemePark.exe"
if [[ -n ${TP_FLUIDSYNTH:-} ]]; then                 # (FluidSynth links dynamically: its DLLs beside the program)
  ldd "$NEW/ThemePark.exe" | awk '/clang64|clangarm64|ucrt64/ {print $3}' | while read -r dll; do cp "$dll" "$NEW/"; done
fi
"$PYTHON" tools/export_image.py "$EXE" "$NEW/MAIN.TPIMG" >/dev/null
"$PYTHON" tools/export_image.py "$INTRO" "$NEW/INTRO.TPIMG" >/dev/null

# Replaced things go to the Recycle Bin, never deleted.
recycle() {
  [[ -e $1 ]] || return 0
  powershell.exe -NoProfile -Command "Add-Type -AssemblyName Microsoft.VisualBasic; [Microsoft.VisualBasic.FileIO.FileSystem]::DeleteDirectory('$(win "$1")', 'OnlyErrorDialogs', 'SendToRecycleBin')" \
    || mv "$1" "$1.replaced-$(date +%s)"
}
APPDIR=$OUT/ThemeParkNative
recycle "$APPDIR"; mv "$NEW" "$APPDIR"
# Game data: the GAME folder of the verified extraction (DATA, SAVE, drivers); the DOS executables are not
# copied (the program runs compiled code).
mkdir -p "$DATA/saves"
if [[ ! -d "$DATA/game/DATA" ]]; then
  "$PYTHON" - "$(win "$GAMEDIR/GAME")" "$(win "$DATA/game")" <<'PY'
import shutil, sys
shutil.copytree(sys.argv[1], sys.argv[2], ignore=shutil.ignore_patterns('*.EXE', '*.exe'), dirs_exist_ok=True)
PY
fi
# GOG's sound setup (Sound Blaster 16 at 220h/IRQ 7/DMA 1, General MIDI at 330h).
if [[ $HAVE_DRIVERS == 1 && ! -e "$DATA/game/sndsetup.inf" ]]; then
  cp "$GAMEDIR/THEME.CD/SNDSETUP.INF" "$DATA/game/sndsetup.inf"
fi
if [[ ${TP_NO_INSTALL:-0} != 1 ]]; then
  recycle "$DEST"
  mkdir -p "$(dirname "$DEST")"; cp -R "$APPDIR" "$DEST"
  START=$(cygpath -u "$APPDATA")/Microsoft/Windows/Start\ Menu/Programs
  powershell.exe -NoProfile -Command "\$s = (New-Object -ComObject WScript.Shell).CreateShortcut('$(win "$START/Theme Park.lnk")'); \$s.TargetPath = '$(win "$DEST/ThemePark.exe")'; \$s.WorkingDirectory = '$(win "$DEST")'; \$s.Save()" \
    || echo "   (no Start menu entry)"
  echo "Installed $(win "$DEST") (data $(win "$DATA"); Theme Park in the Start menu)"
else
  echo "Built $(win "$APPDIR") (data $(win "$DATA"))"
fi
