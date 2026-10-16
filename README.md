# Theme Park — native macOS recompilation

A new Apple Silicon port of the user's own GOG Theme Park release. The target is
an ARM64 app that recompiles the original game code, with native platform
services. DOSBox, Wine and Rosetta are not part of the intended runtime.

**Status:** binary survey and a first native-code proof. The game does not boot
or display a menu yet. Two original integer/memory routines compile into an
ARM64 library and match the x86 originals over 2,000 differential cases.

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

Nothing here installs an app or modifies the downloaded package, an installed
game, the user's saves, or another project. No game content belongs in git.
