#!/bin/bash
# tools/make_dropin.sh <theme_park_enUS_1_0_33010.pkg> <version tag>: the drag-and-drop app ("Theme Park.app", disk image
# and zip in ~/ProjectBuilds/theme-park-macos/release) for players without Xcode tools, after the other ports' recomp
# setup (platform/setup: the same files as diablo-2-macos recomp/setup).
#
# The app holds the host, the launch window and a headless runner, compiled here and bound at load to the game library
# (weakly: they run before it exists); the bundled clang, ld64.lld and Python that build the game on the player's Mac;
# this project's tools, the pinned lifter and configuration; and unicorn's wheel. No game code, data or traces: setup
# takes the player's GOG copy (the installed app, its game folder, or the .pkg), traces its start, makes the sound
# drivers, translates and links it there (tools/dropin_build.py), and check_release.py fails the build otherwise.
# The package is needed here only to build and check this tree (tools/build_app.sh with TP_NO_INSTALL=1).
set -euo pipefail
export MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-14.0}"
cd "$(dirname "$0")/.."; ROOT="$PWD"; S="$ROOT/platform/setup"
PKG="${1:?usage: tools/make_dropin.sh <package.pkg> <version tag>}"; TAG="${2:?usage: tools/make_dropin.sh <package.pkg> <version tag>}"
NAME="Theme Park"; BIN=ThemePark; SLUG=OpenPark; REPO=Joshspeakman/OpenPark
REL="$HOME/ProjectBuilds/theme-park-macos/release"; mkdir -p "$REL"
TK="$HOME/ProjectBuilds/_toolkit/toolkit.tar.xz"; [ -f "$TK" ] || "$S/make_toolkit.sh" "$TK"
INC=vendor/freedom-force-recomp/recomp/runtime; GAMEDIR=work/extracted/payload/Contents/Resources/game
echo "== the game, built and checked from the package (tools/build_app.sh, not installed)"
TP_NO_INSTALL=1 TP_NO_DOWNLOADS=1 bash tools/build_app.sh "$PKG" > "$REL/build_app.log" 2>&1 || { tail -20 "$REL/build_app.log"; exit 1; }
GA=(work/app-release/game.a work/app-release-intro/game.a)
O="$ROOT/build/dropin"; rm -rf "${O:?}"; mkdir -p "$O/host" "$O/runner"
echo "== the host, the launch window and the runner"
HOST=(platform/macos/main.m platform/macos/audio.m platform/macos/launcher.m platform/macos/platform_macos.c runtime/cinepak.c runtime/host.c
      runtime/sha256.c runtime/sb16.c runtime/verify.c runtime/view.c runtime/native_land.c runtime/native_draw.c runtime/qol.c runtime/walk.c
      runtime/bigmap.c runtime/freemode.c)
RUN=(platform/macos/platform_macos.c runtime/host.c runtime/sha256.c runtime/sb16.c runtime/headless.c runtime/verify.c runtime/view.c
     runtime/native_land.c runtime/native_draw.c runtime/qol.c runtime/walk.c runtime/bigmap.c runtime/freemode.c)
cc1() { local out="$1" src="$2"; local arc=(); [[ $src == *.m ]] && arc=(-fobjc-arc)
        clang -c -arch arm64 -O2 ${arc[@]+"${arc[@]}"} -w -I "$INC" -I runtime -DDS_EASY_INSTALL -o "$out/$(basename "${src%.*}").o" "$src"; }
for f in "${HOST[@]}"; do cc1 "$O/host" "$f" & done; for f in "${RUN[@]}"; do cc1 "$O/runner" "$f" & done; wait
clang -c -arch arm64 -O2 -fobjc-arc -w -I "$S" -o "$O/host/setup_ui.o" "$S/setup_ui.m"
# what each takes from the game: placeholders in a stub library they link weakly (the real one is set up on the player's Mac)
nm -gU "${GA[@]}" 2>/dev/null | awk 'NF==3{print $2, $3}' | sort -u -k2 > "$O/game_syms.txt"
for side in host runner; do
  { echo '#include <stdint.h>'
    comm -12 <(nm -u "$O/$side"/*.o | awk '/^_/{print $1}' | sort -u) <(awk '{print $2}' "$O/game_syms.txt" | sort -u) | while read -r s; do
      t="$(awk -v s="$s" '$2==s{print $1; exit}' "$O/game_syms.txt")"; n="${s#_}"
      case "$t" in T|t) echo "void $n(void) {}";; *) echo "char $n[1];";; esac
    done; } > "$O/$side-stub.c"
  clang -arch arm64 -dynamiclib -install_name @rpath/libgame.dylib -o "$O/$side-libgame.dylib" "$O/$side-stub.c"
done
A="$REL/$NAME.app"; rm -rf "${A:?}"; mkdir -p "$A/Contents/MacOS" "$A/Contents/Resources/setup"
FW=(-framework Cocoa -framework QuartzCore -framework AVFoundation -framework AudioToolbox -framework UniformTypeIdentifiers -framework ImageIO
    -framework GameController -framework IOSurface -framework CoreImage)
clang -arch arm64 -o "$A/Contents/MacOS/$BIN" "$O"/host/*.o -Wl,-weak_library,"$O/host-libgame.dylib" -Wl,-export_dynamic "${FW[@]}" -lz
clang -arch arm64 -o "$A/Contents/MacOS/tp-headless" "$O"/runner/*.o -Wl,-weak_library,"$O/runner-libgame.dylib" -Wl,-export_dynamic -lz
echo "== setup files"
R="$A/Contents/Resources/setup"; mkdir -p "$R/tools" "$R/openpark/tools" "$R/openpark/config" "$R/openpark/vendor" "$R/wheels"
cp "$S/setup.py" "$R/"; cp -R "$S/include" "$R/include"; cp "$TK" "$R/toolkit.tar.xz"
cp tools/dropin_build.py tools/op_art.py "$R/tools/"
cp tools/*.py "$R/openpark/tools/"; cp config/*.json "$R/openpark/config/"; cp -R vendor/freedom-force-recomp "$R/openpark/vendor/"
tools/fetch_wheels.sh "$R/wheels"
CODE="$(cat tools/*.py config/*.json "$S"/include/* $(find vendor/freedom-force-recomp -type f | sort) | shasum -a 256 | cut -d' ' -f1)"
sed -e "s|@REPO@|$REPO|; s|@ASSET@|$SLUG.zip|" "$S/update.sh" > "$A/Contents/Resources/update"; chmod +x "$A/Contents/Resources/update"   # (the launch window's Updates row)
python3 - "$R/game.json" "$CODE" "$TAG" <<'PY'
import json, sys
c = {"name": "Theme Park", "folder": "Theme Park", "version": "GOG (Mac) 1.0", "title": "THEME PARK", "style": "bronze",
     "where": "Your Theme Park from GOG: the installed Theme Park app, its game folder, or GOG's Mac package (theme_park_enUS_1_0_33010.pkg)",
     "minutes": 20, "space_gb": 2, "build_gb": 2, "exe": "GAME/MAIN.EXE",
     "exe_sha256": {"fba72de2cee7f92dc26801ab2ee6a7c6f8c84aefb8cdab0da1fbcee532b5dc79": "GOG 1.0"},
     "inside": ["Contents/Resources/game"],
     "package": {"name": "theme_park_enUS_1_0_33010.pkg", "sha256": "942da3010a657c07621eb19c6f2dacbb71ce6f06c07f62914cb8f422e1305650",
                 "member": "package.pkg/Scripts", "payload": "./payload/Contents/Resources/game", "version": "GOG's Mac package"},
     "required": ["GAME/INTRO.EXE", "GAME/DATA", "GAME/HMIDRV.386", "GAME/HMIMDRV.386", "GAME/HMIDET.386", "THEME.CD/SNDSETUP.INF"],
     "libs": [{"file": "GAME/INTRO.EXE"}, {"file": "GAME/HMIDRV.386"}, {"file": "GAME/HMIMDRV.386"}, {"file": "GAME/HMIDET.386"}],
     "pipeline": ["dropin_build.py", "--game", "{game}", "--out", "{out}", "--work", "{work}", "--setup", "{setup}", "--llvm", "{llvm}",
                  "--runner", "{macos}/tp-headless"],
     "art": ["op_art.py", "{art}"], "log": "theme-park", "bound_symbol": "tp_dispatch", "code": sys.argv[2], "release": sys.argv[3]}
json.dump(c, open(sys.argv[1], "w"), indent=1)
PY
echo "== icon and bundle"
I="$(mktemp -d)"; clang -fobjc-arc -framework AppKit -framework CoreImage "$S/make_art.m" -I "$S" -o "$I/mk"; "$I/mk" icon bronze TP "$I/i.png"; mkdir "$I/i.iconset"
for n in 16 32 128 256 512; do sips -z $n $n "$I/i.png" --out "$I/i.iconset/icon_${n}x${n}.png" >/dev/null; sips -z $((n*2)) $((n*2)) "$I/i.png" --out "$I/i.iconset/icon_${n}x${n}@2x.png" >/dev/null; done
iconutil -c icns "$I/i.iconset" -o "$A/Contents/Resources/AppIcon.icns"
"$I/mk" dmg bronze "THEME PARK" "$REL/bg.png" 1; "$I/mk" dmg bronze "THEME PARK" "$REL/bg@2x.png" 2; tiffutil -cathidpicheck "$REL/bg.png" "$REL/bg@2x.png" -out "$REL/background.tiff" 2>/dev/null; rm -f "${REL:?}/bg.png" "${REL:?}/bg@2x.png"; rm -rf "${I:?}"
cat > "$A/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>$NAME</string><key>CFBundleDisplayName</key><string>$NAME</string>
  <key>CFBundleIdentifier</key><string>io.github.joshspeakman.openpark</string><key>CFBundleExecutable</key><string>$BIN</string>
  <key>CFBundleIconFile</key><string>AppIcon</string><key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>${TAG#v}</string><key>CFBundleVersion</key><string>${TAG#v}</string>
  <key>TPUpdateBranch</key><string>release</string><key>TPBuiltCommit</key><string>$TAG</string>
  <key>LSMinimumSystemVersion</key><string>$MACOSX_DEPLOYMENT_TARGET</string><key>LSApplicationCategoryType</key><string>public.app-category.simulation-games</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>CFBundleDocumentTypes</key><array><dict><key>CFBundleTypeName</key><string>Game Folder or Disc</string><key>CFBundleTypeRole</key><string>Viewer</string>
    <key>LSHandlerRank</key><string>Alternate</string><key>LSItemContentTypes</key><array><string>public.folder</string><string>public.data</string></array></dict></array>
</dict></plist>
PLIST
python3 "$S/check_release.py" "$A" "$GAMEDIR" "$R/game.json"
codesign --force --deep -s - "$A"; codesign -v "$A"
echo "== packaging"
PYV="$ROOT/.venv/bin/python3"; [ -x "$PYV" ] || { python3 -m venv "$ROOT/.venv"; "$PYV" -m pip install -q -r requirements.txt; }   # (the project's venv: tools/build_app.sh uses it too)
"$PYV" -c 'import dmgbuild' 2>/dev/null || "$PYV" -m pip install -q dmgbuild
D="$(mktemp -d)"; sed "s/@NAME@/$NAME/g" "$S/ReadMe.txt" > "$D/Read Me First.txt"
rm -f "${REL:?}/$SLUG.dmg" "${REL:?}/$SLUG.zip"
cat > "$D/settings.py" <<PY
files = ["$A", "$D/Read Me First.txt"]; symlinks = {"Applications": "/Applications"}; format = "UDZO"; filesystem = "HFS+"
badge_icon = None; icon = "$A/Contents/Resources/AppIcon.icns"; background = "$REL/background.tiff"; show_status_bar = False; show_tab_view = False; show_toolbar = False; show_pathbar = False; show_sidebar = False
window_rect = ((200, 120), (660, 420)); default_view = "icon-view"; icon_size = 112; text_size = 13
icon_locations = {"$NAME.app": (170, 230), "Applications": (490, 230), "Read Me First.txt": (330, 372)}
PY
"$(dirname "$PYV")/dmgbuild" -s "$D/settings.py" "$NAME" "$REL/$SLUG.dmg" 2>&1 | tail -1; rm -rf "${D:?}"
(cd "$REL"; ditto -c -k --keepParent "$NAME.app" "$SLUG.zip")
du -sh "$A" "$REL/$SLUG.dmg" | sed "s|$REL/||"
