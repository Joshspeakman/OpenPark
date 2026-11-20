#!/usr/bin/env python3
"""dropin_build.py --game DIR --out DIR --work DIR --setup DIR --llvm DIR --runner PATH: the drag-and-drop app's build of
the player's own Theme Park on their Mac (recomp/setup/setup.py runs it as game.json's "pipeline"; tools/make_dropin.sh
makes the app). No Xcode tools there: the app's own clang and ld64.lld, its headers (setup/include) and its Python.

  1. The tools, the pinned lifter and the configuration shipped in the app (setup/openpark) are copied into the work
     folder (they write beside themselves); unicorn is installed there from the app's wheel (setup/wheels).
  2. The game's start is surveyed and traced from the player's own executables, as tools/build_app.sh and
     tests/check_macos.sh --park do (the traces hold the game's own bytes: the app ships none).
  3. The HMI sound drivers are made from the game's own files (tools/make_drivers.py, with the app's headless runner).
  4. MAIN.EXE and INTRO.EXE are translated and compiled; the objects become <out>/libgame.dylib.
  5. Their images (tools/export_image.py) go beside it: MAIN.TPIMG, INTRO.TPIMG.
Progress lines "@progress <0..1> <text>" go to stdout for the setup window."""
import argparse
import os
import shlex
import shutil
import subprocess
import sys
from pathlib import Path


def say(frac, text):
    print('@progress %.3f %s' % (frac, text), flush=True)


def main():
    ap = argparse.ArgumentParser()
    for a in ('--game', '--out', '--work', '--setup', '--llvm', '--runner'):
        ap.add_argument(a, type=Path, required=True)
    a = ap.parse_args()
    op = a.work / 'openpark'
    if op.exists():
        shutil.rmtree(op)
    shutil.copytree(a.setup / 'openpark', op, symlinks=True)
    w = op / 'work'
    w.mkdir(parents=True, exist_ok=True)
    site = a.work / 'site'
    say(0.0, 'Getting the tools ready')
    r = subprocess.run([sys.executable, '-m', 'pip', 'install', '-q', '--disable-pip-version-check', '--no-index', '--find-links',
                        str(a.setup / 'wheels'), '--target', str(site), 'unicorn'], capture_output=True, text=True)
    if r.returncode:
        sys.exit('the tools could not be installed: ' + (r.stderr or r.stdout).strip()[-300:])
    env = dict(os.environ, CC=str(a.llvm / 'clang'), TP_LD=str(a.llvm / 'ld64.lld'), TP_RUNNER=str(a.runner),
               TP_CFLAGS='--target=arm64-apple-macos14 -nostdlibinc -isystem %s' % shlex.quote(str(a.setup / 'include')),   # (paths with spaces)
               PYTHONPATH=os.pathsep.join([str(site), os.environ.get('PYTHONPATH', '')]), DS_TEST='1', W32_AUDIO_MUTE='1')
    py, ref = sys.executable, op / 'vendor/freedom-force-recomp'
    main_exe, intro_exe = a.game / 'GAME/MAIN.EXE', a.game / 'GAME/INTRO.EXE'

    def run(frac, text, *cmd):
        say(frac, text)
        r = subprocess.run([py, *map(str, cmd)], cwd=op, env=env, capture_output=True, text=True)
        if r.returncode:
            sys.exit('%s failed: %s' % (Path(str(cmd[0])).name, (r.stderr or r.stdout).strip()[-400:]))

    run(0.02, "Reading the game's code", op / 'tools/survey.py', main_exe, '--out', w / 'main-survey.json')
    run(0.03, "Reading the game's code", op / 'tools/survey.py', intro_exe, '--out', w / 'intro-survey.json')
    # the --park run's input and bounds (tests/check_macos.sh), with room for a slower Mac to reach the block bound
    run(0.04, "Following the game's start", op / 'tools/trace_startup.py', main_exe, '--out', w / 'check-startup', '--quantum', '1000',
        '--blocks', '8000000', '--seconds', '1200', '--keys', '60:1c0d,80:1454,82:1245,84:1f53,86:1454,88:1c0d,120:3b00,300:3d00',
        '--clicks', '180:564:316,220:280:90', '--block-clicks', '2450000:72:310,2650000:568:316,2800000:604:380')
    run(0.10, "Following the intro", op / 'tools/trace_startup.py', intro_exe, '--out', w / 'intro-trace', '--quantum', '1000',
        '--blocks', '30000000', '--seconds', '2400')
    run(0.20, "Making the sound drivers from the game's files", op / 'tools/make_drivers.py', main_exe, '--reference', ref, '--out', w)
    run(0.55, "Translating the game's code", op / 'tools/build_game.py', main_exe, '--reference', ref, '--trace', w / 'check-startup/trace.json',
        '--survey', w / 'main-survey.json', '--out', w / 'app-release', '--opt=-O2', '--drivers', w, '--no-link')
    run(0.85, 'Translating the intro', op / 'tools/build_game.py', intro_exe, '--reference', ref, '--trace', w / 'intro-trace/trace.json',
        '--survey', w / 'intro-survey.json', '--out', w / 'app-release-intro', '--opt=-O2', '--prefix', 'intro_', '--drivers', w, '--no-link')
    say(0.92, 'Putting it together')
    objects = (w / 'app-release/objects.txt').read_text().splitlines() + (w / 'app-release-intro/objects.txt').read_text().splitlines()
    a.out.mkdir(parents=True, exist_ok=True)
    sys.path.insert(0, str(op / 'tools'))
    os.environ.update(TP_LD=env['TP_LD'])
    import toolchain
    toolchain.link_game_library(a.out / 'libgame.dylib.tmp', objects)
    os.replace(a.out / 'libgame.dylib.tmp', a.out / 'libgame.dylib')
    snd = a.game / 'GAME/sndsetup.inf'                     # sound on, as tools/build_app.sh sets the installed game up
    if not snd.exists():
        shutil.copy(a.game / 'THEME.CD/SNDSETUP.INF', snd)
    for exe, name in ((main_exe, 'MAIN.TPIMG'), (intro_exe, 'INTRO.TPIMG')):
        run(0.96, 'Putting it together', op / 'tools/export_image.py', exe, a.out / name)
    say(1.0, 'Built')


if __name__ == '__main__':
    main()
