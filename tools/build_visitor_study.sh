#!/bin/bash
# Isolated study build. Uses local copies of the verified AOT archives and owned
# game resources in work/visitor-3d/native; never rebuilds or writes another tree.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD
CACHE="$ROOT/work/visitor-3d/native"
DATA="$ROOT/work/visitor-3d/profile"
NAME="OpenPark Visitor 3D Study"
ID="com.joshspeakman.openpark.visitor3dstudy"
MODE="faithful"
REQUIRE_COMPLETE=0
if [[ ${1:-} == --remaster ]]; then
  DATA="$ROOT/work/remaster/profile"
  # Reuse the one Reimagined bundle identity/install, never add another copy.
  NAME="OpenPark Reimagined Study"
  ID="com.joshspeakman.openpark.reimaginedstudy"
  MODE="reimagined"
  REQUIRE_COMPLETE=1
  if [[ ${TP_STUDY_NO_INSTALL:-0} != 1 ]]; then
    python3 - "$DATA/reimagined" <<'PY'
import hashlib,json,sys
from pathlib import Path
p=Path(sys.argv[1])
if not (p/'release.json').is_file() or not (p/'world.art').is_file():
    raise SystemExit('No complete Reimagined pack: refusing to install unfinished artwork.')
r=json.loads((p/'release.json').read_text())
if r.get('complete') is not True or r.get('world_sha256')!=hashlib.sha256((p/'world.art').read_bytes()).hexdigest():
    raise SystemExit('Reimagined release integrity check failed.')
PY
  fi
elif [[ ${1:-} == --reimagined ]]; then
  DATA="$ROOT/work/reimagined/profile"
  NAME="OpenPark Reimagined Study"
  ID="com.joshspeakman.openpark.reimaginedstudy"
  MODE="reimagined"
elif [[ ${1:-} == --faithful-layered ]]; then
  DATA="$ROOT/work/faithful-layered/profile"
  NAME="OpenPark Faithful Layered Study"
  ID="com.joshspeakman.openpark.faithfullayeredstudy"
elif [[ ${1:-} == --faithful-detail ]]; then
  DATA="$ROOT/work/faithful-detail/profile"
  NAME="OpenPark Faithful Detail Study"
  ID="com.joshspeakman.openpark.faithfuldetailstudy"
elif [[ $# != 0 ]]; then
  echo 'usage: build_visitor_study.sh [--remaster|--reimagined|--faithful-detail|--faithful-layered]' >&2; exit 2
fi
APP="$ROOT/build/visitor-study/$NAME.app"
DEST="$HOME/Applications/$NAME.app"
for f in main.a intro.a MAIN.TPIMG INTRO.TPIMG; do
  [[ -s "$CACHE/$f" ]] || { echo "Missing isolated native build input: $CACHE/$f" >&2; exit 1; }
done
[[ -d "$DATA/game/DATA" && -d "$DATA/saves" ]] || { echo "Missing isolated game/profile" >&2; exit 1; }
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
clang -arch arm64 -O2 -fobjc-arc -Wall -Wno-unused-function \
  -I vendor/freedom-force-recomp/recomp/runtime -I runtime \
  platform/macos/main.m platform/macos/audio.m platform/macos/launcher.m platform/macos/platform_macos.c \
  runtime/cinepak.c runtime/host.c runtime/sha256.c runtime/sb16.c runtime/verify.c runtime/view.c \
  runtime/native_land.c runtime/native_draw.c runtime/qol.c runtime/walk.c runtime/bigmap.c runtime/freemode.c \
  "$CACHE/main.a" "$CACHE/intro.a" \
  -framework Cocoa -framework QuartzCore -framework AVFoundation -framework AudioToolbox \
  -framework UniformTypeIdentifiers -framework ImageIO -framework GameController -framework IOSurface -lz \
  -o "$APP/Contents/MacOS/ThemePark"
cp "$CACHE/MAIN.TPIMG" "$CACHE/INTRO.TPIMG" "$APP/Contents/Resources/"
python3 - "$APP" "$DATA" "$NAME" "$ID" "$MODE" "$REQUIRE_COMPLETE" <<'PY'
import plistlib,sys
from pathlib import Path
app,data=map(Path,sys.argv[1:3]);name,identity,mode,require_complete=sys.argv[3:]
info=dict(CFBundleName=name,CFBundleDisplayName=name,
 CFBundleIdentifier=identity,CFBundleExecutable='ThemePark',
 CFBundlePackageType='APPL',CFBundleShortVersionString='0.2',CFBundleVersion='2',
 LSMinimumSystemVersion='13.0',NSHighResolutionCapable=True,
 LSApplicationCategoryType='public.app-category.simulation-games',TPDataRoot=str(data),TPStudyMapSize=128,TPStudyArtMode=mode,
 TPRequireCompleteRemaster=bool(int(require_complete)))
(app/'Contents/Info.plist').write_bytes(plistlib.dumps(info))
PY
# Preserve the cloned launcher's existing artwork. These are read-only inputs.
if [[ $REQUIRE_COMPLETE == 1 ]]; then
  LAUNCHER_ART="$HOME/Applications/Theme Park Nightly.app/Contents/Resources"
  for f in banner.png keyart.jpg logo.png AppIcon.icns; do
    [[ ! -f "$LAUNCHER_ART/$f" ]] || cp "$LAUNCHER_ART/$f" "$APP/Contents/Resources/$f"
  done
fi
codesign --force --sign - "$APP"
codesign --verify --deep --strict "$APP"
if [[ ${TP_STUDY_NO_INSTALL:-0} != 1 ]]; then
  if [[ -e "$DEST" ]]; then
    # Only this study's identity may be replaced; never Nightly or another app.
    [[ $(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$DEST/Contents/Info.plist") == "$ID" ]]
    mv "$DEST" "$HOME/.Trash/$NAME-$(date +%s).app"
  fi
  ditto "$APP" "$DEST"
  codesign --verify --deep --strict "$DEST"
  echo "Installed $DEST (isolated data: $DATA)"
fi
