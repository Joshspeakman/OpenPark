#!/bin/bash
# Reference renders of the launch window for the other platforms' apps (platform/sdl matches it page by page): every
# page and the main window's states, drawn with the fallbacks only (TP_LAUNCHER_NO_ART=1: no GOG art, no park picture,
# so nothing game-derived); main.png at 2x, the rest at 1x. The settings shown are the defaults (runtime/app_spec.c). Bounded: each render is TP_LAUNCHER_SHOT (no window, no game).
#   tools/launcher_reference.sh [out-dir (default docs/launcher-reference)] [app (default build/app/Theme Park Nightly.app)]
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=${1:-docs/launcher-reference}
APP=${2:-build/app/Theme Park Nightly.app}
BIN="$APP/Contents/MacOS/ThemePark"
[[ -x $BIN ]] || { echo "no app at $APP (tools/build_app.sh with TP_NO_INSTALL=1 builds it)" >&2; exit 1; }
mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd)
scratch=$(mktemp -d)                     # (a data folder of its own: no saved park picture, no store art)
mkdir -p "$scratch/game/DATA"            # (the app looks for the game before its launch window; nothing is read from it)
# A copy with its own identifier: the settings shown are the defaults, not this Mac's player's.
cp -Rc "$APP" "$scratch/Reference.app"
/usr/libexec/PlistBuddy -c 'Set :CFBundleIdentifier com.joshspeakman.themepark.reference' "$scratch/Reference.app/Contents/Info.plist"
codesign --force --deep -s - "$scratch/Reference.app" >/dev/null 2>&1
defaults delete com.joshspeakman.themepark.reference >/dev/null 2>&1 || true
BIN="$scratch/Reference.app/Contents/MacOS/ThemePark"
shot() {   # name, env...
  local name=$1; shift
  env -i HOME="$HOME" PATH="$PATH" DS_TEST=1 TP_DATA="$scratch" TP_LAUNCHER_NO_ART=1 TP_LAUNCHER_TIME=0.9 TP_LAUNCHER_SHOT="$OUT/$name.png" "$@" \
    "$BIN" >/dev/null 2>&1 || true
  [[ -s $OUT/$name.png ]] && echo "  $OUT/$name.png" || { echo "  $name: not rendered" >&2; exit 1; }
}
shot main
shot main-hover-ticket TP_LAUNCHER_HOVER=301
shot main-hover-tile TP_LAUNCHER_HOVER=502
shot main-focus-play TP_LAUNCHER_FOCUS=12
shot main-fireworks TP_LAUNCHER_TIME=1.4
for page in game display sound "free mode" controls music effects; do
  shot "page-${page// /-}" TP_LAUNCHER_OPEN="$page"
done
shot page-display-focus TP_LAUNCHER_OPEN=display TP_LAUNCHER_FOCUS=4
# main.png stays at 2x (the detail: bulbs, ticket edges, type); the rest at 1x (1000 x 700), to keep the folder small
for f in "$OUT"/*.png; do [[ $(basename "$f") == main.png ]] || sips -Z 1000 "$f" >/dev/null; done
defaults delete com.joshspeakman.themepark.reference >/dev/null 2>&1 || true
rm -rf "$scratch"
