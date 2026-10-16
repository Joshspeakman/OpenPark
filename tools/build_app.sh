#!/bin/bash
# Build "Theme Park Nightly.app" from the owned package and install it locally.
#   tools/build_app.sh [package.pkg] [reference-checkout]
# Game files are copied from the verified extraction into ~/Games/ThemeParkNative/game
# (never into git); saves live in ~/Games/ThemeParkNative/saves. An existing app is
# moved to the Trash before the new one is installed.
set -euo pipefail
cd "$(dirname "$0")/.."
PKG=${1:-$HOME/Downloads/theme_park_enUS_1_0_33010.pkg}
REF=${2:-$HOME/Projects/freedom-force-macos}
PYTHON=${TP_PYTHON:-$HOME/Projects/freedom-force-macos/recomp/.venv/bin/python3}
DATA=${TP_DATA:-$HOME/Games/ThemeParkNative}
DEST=${TP_INSTALL_DIR:-$HOME/Applications}
NAME="Theme Park Nightly"
GAMEDIR=work/extracted/payload/Contents/Resources/game
EXE=$GAMEDIR/GAME/MAIN.EXE
OUT=build/app
[[ $(uname -m) == arm64 ]] || { echo 'Apple Silicon required' >&2; exit 1; }
[[ -f $EXE ]] || python3 tools/prepare.py "$PKG"
[[ -f work/main-survey.json ]] || "$PYTHON" tools/survey.py "$EXE" --out work/main-survey.json
# The verified trace supplies observed block extents and the self-modified
# allocation variant; build it with tests/check_macos.sh ... --park first.
TRACE=work/check-startup/trace.json
[[ -f $TRACE ]] || { echo "missing $TRACE: run tests/check_macos.sh $EXE $REF --park" >&2; exit 1; }
# HMI sound drivers the game loads at run time (work/drv-ADDR-MODE-NAME.bin,
# made by tools/dump_drivers.sh): compiled at their load addresses.
LOADED=()
for f in work/drv-*-*-*.bin; do
  [[ -e $f ]] || continue
  IFS=- read -r _ addr mode _ <<< "$(basename "$f" .bin)"
  if [[ $mode == cs ]]; then LOADED+=(--loaded-code "$addr:$f:cs"); else LOADED+=(--loaded-code "$addr:$f"); fi
done
[[ ${#LOADED[@]} -gt 0 ]] || echo "warning: no sound driver images; the app will run with sound off" >&2
"$PYTHON" tools/build_game.py "$EXE" --reference "$REF" --trace "$TRACE" --survey work/main-survey.json --out work/app-release --opt=-O2 "${LOADED[@]}" >/dev/null
mkdir -p "$OUT"
APP="$OUT/$NAME.app"
if [[ -e "$APP.new" ]]; then mv "$APP.new" "$HOME/.Trash/$NAME-partial-$(date +%s).app"; fi
mkdir -p "$APP.new/Contents/MacOS" "$APP.new/Contents/Resources"
INC="$REF/recomp/runtime"
clang -arch arm64 -O2 -fobjc-arc -Wall -Wno-unused-function -I "$INC" -I runtime \
  app/main.m app/audio.m runtime/host.c runtime/sb16.c runtime/verify.c work/app-release/game.a \
  -framework Cocoa -framework QuartzCore -framework AVFoundation -framework AudioToolbox -lz \
  -o "$APP.new/Contents/MacOS/ThemePark"
"$PYTHON" tools/export_image.py "$EXE" "$APP.new/Contents/Resources/MAIN.TPIMG" >/dev/null
ICO=$GAMEDIR/goggame-1207660423.ico
ICONSET=$OUT/AppIcon.iconset
mkdir -p "$ICONSET"
sips -s format png "$ICO" --out "$OUT/icon-256.png" >/dev/null
for s in 16 32 128 256 512; do
  sips -z $s $s "$OUT/icon-256.png" --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
  d=$((s*2)); sips -z $d $d "$OUT/icon-256.png" --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o "$APP.new/Contents/Resources/AppIcon.icns"
COMMIT=$(git rev-parse --short HEAD)
cat > "$APP.new/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>$NAME</string>
  <key>CFBundleDisplayName</key><string>$NAME</string>
  <key>CFBundleIdentifier</key><string>com.joshspeakman.themepark.nightly</string>
  <key>CFBundleExecutable</key><string>ThemePark</string>
  <key>CFBundleIconFile</key><string>AppIcon</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>0.1 nightly ($COMMIT)</string>
  <key>CFBundleVersion</key><string>$COMMIT</string>
  <key>LSMinimumSystemVersion</key><string>13.0</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.simulation-games</string>
  <key>NSHighResolutionCapable</key><true/>
</dict></plist>
PLIST
codesign --force --sign - "$APP.new" >/dev/null 2>&1
if [[ -e "$APP" ]]; then mv "$APP" "$HOME/.Trash/$NAME-build-$(date +%s).app"; fi
mv "$APP.new" "$APP"
# Game data: the GAME folder of the verified extraction (DATA, SAVE, drivers).
mkdir -p "$DATA/saves"
if [[ ! -d "$DATA/game/DATA" ]]; then
  mkdir -p "$DATA/game"
  # The app runs compiled code: the DOS executables are not copied.
  rsync -a --exclude '*.EXE' "$GAMEDIR/GAME/" "$DATA/game/"
fi
# GOG's sound setup (Sound Blaster 16 at 220h/IRQ 7/DMA 1, General MIDI at 330h).
if [[ ${#LOADED[@]} -gt 0 && ! -e "$DATA/game/sndsetup.inf" ]]; then
  cp "$GAMEDIR/THEME.CD/SNDSETUP.INF" "$DATA/game/sndsetup.inf"
fi
if [[ ${TP_NO_INSTALL:-0} != 1 ]]; then
  mkdir -p "$DEST"
  if [[ -e "$DEST/$NAME.app" ]]; then mv "$DEST/$NAME.app" "$HOME/.Trash/$NAME-$(date +%s).app"; fi
  ditto "$APP" "$DEST/$NAME.app"
  echo "Installed $DEST/$NAME.app (data $DATA)"
else
  echo "Built $APP (data $DATA)"
fi
