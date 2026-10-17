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
# see tools/dump_drivers.md): compiled at their load addresses.
HAVE_DRIVERS=0; ls work/drv-*-*-*.bin >/dev/null 2>&1 && HAVE_DRIVERS=1
[[ $HAVE_DRIVERS == 1 ]] || echo "warning: no sound driver images; the app will run with sound off" >&2
DRV=(); [[ $HAVE_DRIVERS == 1 ]] && DRV=(--drivers work)
"$PYTHON" tools/build_game.py "$EXE" --reference "$REF" --trace "$TRACE" --survey work/main-survey.json --out work/app-release --opt=-O2 "${DRV[@]}" >/dev/null
# INTRO.EXE: its own image, linked beside MAIN.EXE's with an intro_ prefix.
INTRO=$GAMEDIR/GAME/INTRO.EXE
[[ -f work/intro-survey.json ]] || "$PYTHON" tools/survey.py "$INTRO" --out work/intro-survey.json
[[ -f work/intro-trace/trace.json ]] || DS_TEST=1 W32_AUDIO_MUTE=1 "$PYTHON" tools/trace_startup.py "$INTRO" --out work/intro-trace --quantum 1000 --blocks 30000000 --seconds 240 >/dev/null
"$PYTHON" tools/build_game.py "$INTRO" --reference "$REF" --trace work/intro-trace/trace.json --survey work/intro-survey.json --out work/app-release-intro --opt=-O2 --prefix intro_ "${DRV[@]}" >/dev/null
mkdir -p "$OUT"
APP="$OUT/$NAME.app"
if [[ -e "$APP.new" ]]; then mv "$APP.new" "$HOME/.Trash/$NAME-partial-$(date +%s).app"; fi
mkdir -p "$APP.new/Contents/MacOS" "$APP.new/Contents/Resources"
INC="$REF/recomp/runtime"
clang -arch arm64 -O2 -fobjc-arc -Wall -Wno-unused-function -I "$INC" -I runtime \
  app/main.m app/audio.m app/launcher.m runtime/host.c runtime/sb16.c runtime/verify.c runtime/view.c runtime/native_land.c runtime/native_draw.c runtime/qol.c work/app-release/game.a work/app-release-intro/game.a \
  -framework Cocoa -framework QuartzCore -framework AVFoundation -framework AudioToolbox -framework UniformTypeIdentifiers -framework ImageIO -framework GameController -lz \
  -o "$APP.new/Contents/MacOS/ThemePark"
"$PYTHON" tools/export_image.py "$EXE" "$APP.new/Contents/Resources/MAIN.TPIMG" >/dev/null
"$PYTHON" tools/export_image.py "$INTRO" "$APP.new/Contents/Resources/INTRO.TPIMG" >/dev/null
# Launcher art from the player's own copy: the title screen (its "theme PARK" box heads the
# launch window), rendered by running the compiled game headless (muted, scratch).
clang -arch arm64 -O2 -w -I "$INC" -I runtime runtime/host.c runtime/sb16.c runtime/headless.c runtime/verify.c runtime/view.c runtime/native_land.c runtime/native_draw.c runtime/qol.c \
  work/app-release/game.a -lz -o "$OUT/tp-headless"
ART=$(mktemp -d "$OUT/art-XXXXXXXX")
DS_TEST=1 W32_AUDIO_MUTE=1 "$OUT/tp-headless" --game "$GAMEDIR/GAME" --overlay "$PWD/$ART/o1" \
  --image "$APP.new/Contents/Resources/MAIN.TPIMG" --out "$ART/title" --quantum 1000 --blocks 6000000 >/dev/null
[[ -f $ART/title/frame.png ]] && cp "$ART/title/frame.png" "$APP.new/Contents/Resources/banner.png"
mv "$ART" "$HOME/.Trash/theme-park-art-$(date +%s)"
# Theme Park's store art (key art, logo, box art) from GOG's public store API, for the launch window
# and icon on this Mac only: kept in $DATA/art, never in this project. TP_NO_DOWNLOADS=1 skips it;
# without it the window uses the title screen's logo and the icon GOG's icon from the package.
fetch_art() {
  [[ -z ${TP_NO_DOWNLOADS:-} ]] || return 0
  mkdir -p "$DATA/art"
  local links
  links=$(curl -fsSL -m 30 "https://api.gog.com/v2/games/1207660423?locale=en-US" | python3 -c '
import json, sys
l = json.load(sys.stdin)["_links"]
for name, key in (("keyart.jpg", "galaxyBackgroundImage"), ("logo.png", "logo"), ("box.jpg", "boxArtImage")):
    if key in l: print(name, l[key]["href"])') || { echo "   (GOG's store art could not be listed)"; return 0; }
  while read -r name url; do
    [[ -s $DATA/art/$name ]] && continue
    if curl -fsSL -m 60 -o "$DATA/art/$name.part" "$url" && [[ $(wc -c < "$DATA/art/$name.part") -gt 20000 ]] \
       && sips -g pixelWidth "$DATA/art/$name.part" >/dev/null 2>&1; then
      mv "$DATA/art/$name.part" "$DATA/art/$name"
    else
      rm -f "$DATA/art/$name.part"; echo "   ($name could not be downloaded from GOG)"
    fi
  done <<< "$links"
}
fetch_art
for f in keyart.jpg logo.png; do [[ -s $DATA/art/$f ]] && cp "$DATA/art/$f" "$APP.new/Contents/Resources/$f"; done
# The icon: the park planet (GOG's box art, else GOG's icon from the package) on a Nightly tile like
# the other ports' (tools/tp_icon.m); GOG's own icon if that cannot be made.
ICO=$GAMEDIR/goggame-1207660423.ico
ICONSET=$OUT/AppIcon.iconset
mkdir -p "$ICONSET"
sips -s format png "$ICO" --out "$OUT/icon-256.png" >/dev/null
SRC=$OUT/icon-256.png
if clang -fobjc-arc -framework AppKit tools/tp_icon.m -o "$OUT/tp_icon"; then
  if [[ -s $DATA/art/box.jpg ]] && "$OUT/tp_icon" "$DATA/art/box.jpg" "$OUT/icon-1024.png" --nightly --box; then SRC=$OUT/icon-1024.png
  elif "$OUT/tp_icon" "$OUT/icon-256.png" "$OUT/icon-1024.png" --nightly; then SRC=$OUT/icon-1024.png; fi
fi
for s in 16 32 128 256 512; do
  sips -z $s $s "$SRC" --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
  d=$((s*2)); sips -z $d $d "$SRC" --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
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
if [[ $HAVE_DRIVERS == 1 && ! -e "$DATA/game/sndsetup.inf" ]]; then
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
