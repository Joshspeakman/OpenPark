# Theme Park — native macOS recompilation

A new Apple Silicon port of the user's own GOG Theme Park release. The target is
an ARM64 app that recompiles the original game code, with native platform
services. DOSBox, Wine and Rosetta are not part of the intended runtime.

**Status:** native game code now boots through setup, accepts keyboard/mouse
input, buys land and enters the first park. Eight million executed blocks match
the x86 original, including 13,678 DOS/BIOS boundaries, 6,882 scheduling boundaries,
the park framebuffer and setup-profile file. The same result runs without
Unicorn or Capstone at runtime. This is a bounded, headless development host,
not a finished app: real-time pacing, audio, full park save/load, build-operation
acceptance and native window/input presentation remain work.

The supplied Mac package contains DOS/4GW LE executables. This requires a DOS
platform layer; the Windows loaders in the other ports cannot load it directly.
See [the plan and findings](docs/PLAN.md) and [reference provenance](docs/PROVENANCE.md).

## Reproduce the current milestone

Run from this directory. Python 3.9+, Apple's command-line tools, `xar` and `tar`
are required. The survey uses Capstone; the development oracle uses Unicorn.
Versions are recorded in `requirements.txt`. This machine already has them in
the Freedom Force development environment, which these commands read without
modifying that checkout:

```sh
export TP_PYTHON="$HOME/Projects/freedom-force-macos/recomp/.venv/bin/python3"
python3 tools/prepare.py "$HOME/Downloads/theme_park_enUS_1_0_33010.pkg"
"$TP_PYTHON" tools/survey.py \
  work/extracted/payload/Contents/Resources/game/GAME/MAIN.EXE \
  --out work/main-survey.json
bash tests/check.sh
bash tests/check.sh \
  work/extracted/payload/Contents/Resources/game/GAME/MAIN.EXE \
  "$HOME/Projects/freedom-force-macos"
bash tests/check_macos.sh \
  work/extracted/payload/Contents/Resources/game/GAME/MAIN.EXE \
  "$HOME/Projects/freedom-force-macos"
```

`prepare.py` verifies the package and executable hashes, extracts only the game
payload into `work/`, and never runs the installer or its postinstall script.
The native proof verifies hashes of the reused lifter/header files before using
them. A future standalone build will vendor the reviewed shared components.

`tests/check.sh` without arguments uses synthetic fixtures and needs no game.
With both arguments it also generates, compiles and runs the native differential
proof. Generated code, binaries, extracted files and reports remain ignored in
`work/`. The proof library links only `libSystem`; Unicorn is used by the Python
test harness, not by the generated ARM64 routines.

`check_macos.sh` runs 17 parser/host tests, 124 synthetic instruction cases,
the 2,000 routine comparisons, and scripted startup through nickname entry and
the menu. It compares CPU-written pages at every service boundary, all 64 MiB
periodically and at completion, register/segment state, port traffic and the
final framebuffer. Arithmetic flags that may be undefined at arbitrary service
boundaries are excluded; IF/DF are checked. See [DOS contract](docs/DOS-CONTRACT.md)
for the limits of the shared host and deterministic input/timer model.

Add `--park` after the two paths to check setup, mouse interaction, buying UK
land and entering the first park. This compares eight million blocks, including
scheduling checkpoints and scratch profile bytes, then reproduces the same
result with the native-only runner. Each run creates fresh scratch profiles and
preserves them under work/; it does not reuse previous saved player data.

After the default menu check, run the compiled game without the development oracle:

```sh
python3 -S tools/run_native.py \
  work/extracted/payload/Contents/Resources/game/GAME/MAIN.EXE \
  --library work/check-native/startup.dylib \
  --services 12699 \
  --keys 60:1c0d,80:1454,82:1245,84:1f53,86:1454,88:1c0d
```

The frame is saved to `work/native-run/frame.png`. `-S` excludes third-party
Python packages; the game executes in the compiled ARM64 library, with a Python
development implementation of the DOS platform services. A library/input hash
manifest guards against using a mismatched build. Unknown code targets or
self-modified variants stop explicitly, without decoding or interpreting x86.

Nothing here installs an app or modifies the downloaded package, an installed
game, the user's saves, or another project. No game content belongs in git.
