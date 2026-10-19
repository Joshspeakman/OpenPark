#!/bin/bash
# Build Theme Park for Linux from the owned package and install it for this user (the counterpart of
# tools/build_app.sh on macOS).
#   tools/build_linux.sh [package.pkg] [reference-checkout (default: the vendored copy)]
# Needs clang, SDL2 (libsdl2-dev), zlib and Python 3 with requirements.txt; FluidSynth (libfluidsynth-dev and a
# General MIDI SoundFont such as fluid-soundfont-gm) for the music, optional.
# Installs the program in ~/.local/lib/theme-park-native, a menu entry and ~/.local/bin/theme-park. Game files are
# copied from the verified extraction into ~/.local/share/ThemeParkNative/game (never into git); saves live in
# ~/.local/share/ThemeParkNative/saves. A replaced install goes to the Trash.
set -euo pipefail
cd "$(dirname "$0")/.."
PKG=${1:-$HOME/Downloads/theme_park_enUS_1_0_33010.pkg}
REF=${2:-vendor/freedom-force-recomp}            # (the pinned lifter, vendored; config/reference.json checks it)
if [[ -n ${TP_PYTHON:-} ]]; then PYTHON=$TP_PYTHON                     # (python3 -m venv .venv && .venv/bin/pip install -r requirements.txt)
elif [[ -x .venv/bin/python3 ]]; then PYTHON=.venv/bin/python3
else PYTHON=python3; fi
DATA=${TP_DATA:-${XDG_DATA_HOME:-$HOME/.local/share}/ThemeParkNative}
DEST=${TP_INSTALL_DIR:-$HOME/.local/lib/theme-park-native}
GAMEDIR=work/extracted/payload/Contents/Resources/game
EXE=$GAMEDIR/GAME/MAIN.EXE
OUT=build/linux
CC=${TP_CC:-clang}
command -v "$CC" >/dev/null || { echo "$CC not found (install clang, or set TP_CC)" >&2; exit 1; }
command -v sdl2-config >/dev/null || { echo 'SDL2 not found: install libsdl2-dev (or your distribution'"'"'s SDL2 development package)' >&2; exit 1; }
[[ -f $EXE ]] || "$PYTHON" tools/prepare.py "$PKG"
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
NEW=$OUT/theme-park-native.new
rm -rf "$NEW"; mkdir -p "$NEW"
INC="$REF/recomp/runtime"
# FluidSynth for the General MIDI music when it is there (TP_NO_FLUIDSYNTH=1: without it).
SYNTH=(); SYNTH_LIBS=()
if [[ -z ${TP_NO_FLUIDSYNTH:-} ]] && pkg-config --exists fluidsynth 2>/dev/null; then
  SYNTH=(-DTP_FLUIDSYNTH $(pkg-config --cflags fluidsynth)); SYNTH_LIBS=($(pkg-config --libs fluidsynth))
else
  echo "warning: FluidSynth not found (libfluidsynth-dev): the music will be off" >&2
fi
RUNTIME=(runtime/host.c runtime/sb16.c runtime/verify.c runtime/view.c runtime/native_land.c runtime/native_draw.c runtime/qol.c
         runtime/walk.c runtime/bigmap.c runtime/freemode.c runtime/platform.c)
"$CC" -O2 -Wall -Wno-unused-function -I "$INC" -I runtime $(sdl2-config --cflags) ${SYNTH[@]+"${SYNTH[@]}"} \
  app-sdl/main.c app-sdl/audio.c app-sdl/settings.c "${RUNTIME[@]}" work/app-release/game.a work/app-release-intro/game.a \
  $(sdl2-config --libs) ${SYNTH_LIBS[@]+"${SYNTH_LIBS[@]}"} -lz -lm -lpthread -o "$NEW/ThemePark"
"$PYTHON" tools/export_image.py "$EXE" "$NEW/MAIN.TPIMG" >/dev/null
"$PYTHON" tools/export_image.py "$INTRO" "$NEW/INTRO.TPIMG" >/dev/null
# The icon: GOG's, from the package (the window's and the menu entry's).
ICO=$GAMEDIR/goggame-1207660423.ico
ICON_SIZE=
if [[ -f $ICO ]] && ICON_SIZE=$("$PYTHON" tools/ico_image.py "$ICO" --png "$NEW/icon.png" --bmp "$NEW/icon.bmp"); then :
else ICON_SIZE=; echo "   (no icon: GOG's could not be read)"; fi

# Replaced things go to the Trash (freedesktop.org's), never deleted.
trash() {
  [[ -e $1 ]] || return 0
  if command -v gio >/dev/null && gio trash "$1" 2>/dev/null; then return 0; fi
  local bin=${XDG_DATA_HOME:-$HOME/.local/share}/Trash/files
  mkdir -p "$bin"; mv "$1" "$bin/$(basename "$1")-$(date +%s)"
}
APPDIR=$OUT/theme-park-native
trash "$APPDIR"; mv "$NEW" "$APPDIR"
# Game data: the GAME folder of the verified extraction (DATA, SAVE, drivers); the DOS executables are not
# copied (the program runs compiled code).
mkdir -p "$DATA/saves"
if [[ ! -d "$DATA/game/DATA" ]]; then
  "$PYTHON" - "$GAMEDIR/GAME" "$DATA/game" <<'PY'
import shutil, sys
shutil.copytree(sys.argv[1], sys.argv[2], ignore=shutil.ignore_patterns('*.EXE', '*.exe'), dirs_exist_ok=True)
PY
fi
# GOG's sound setup (Sound Blaster 16 at 220h/IRQ 7/DMA 1, General MIDI at 330h).
if [[ $HAVE_DRIVERS == 1 && ! -e "$DATA/game/sndsetup.inf" ]]; then
  cp "$GAMEDIR/THEME.CD/SNDSETUP.INF" "$DATA/game/sndsetup.inf"
fi
if [[ ${TP_NO_INSTALL:-0} != 1 ]]; then
  trash "$DEST"
  mkdir -p "$(dirname "$DEST")"; cp -R "$APPDIR" "$DEST"
  BIN=$HOME/.local/bin; mkdir -p "$BIN"
  ln -sfn "$DEST/ThemePark" "$BIN/theme-park"
  APPS=${XDG_DATA_HOME:-$HOME/.local/share}/applications; mkdir -p "$APPS"
  ICON_LINE=
  if [[ -n $ICON_SIZE ]]; then
    ICONS=${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor/$ICON_SIZE/apps; mkdir -p "$ICONS"
    cp "$DEST/icon.png" "$ICONS/theme-park-native.png"; ICON_LINE="Icon=theme-park-native"
  fi
  cat > "$APPS/theme-park-native.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=Theme Park
Comment=Bullfrog's Theme Park, recompiled to native code ($(git rev-parse --short HEAD))
Exec="$DEST/ThemePark"
$ICON_LINE
Categories=Game;Simulation;
Terminal=false
DESKTOP
  echo "Installed $DEST (data $DATA; run theme-park or Theme Park from the menu)"
else
  echo "Built $APPDIR (data $DATA)"
fi
