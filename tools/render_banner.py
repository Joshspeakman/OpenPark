#!/usr/bin/env python3
"""render_banner.py GAME_DIR MAIN.TPIMG GAME_A OUT.png: the player's own title screen for the launch window, rendered by
the compiled game run headless (muted, on a scratch overlay), as tools/build_app.sh does on the Mac. Its "theme PARK"
box is the launch window's logo when GOG's logo isn't there (platform/sdl/launcher.c banner_logo, launcher.m
pixel_crop). The Windows and Linux build scripts run it; it never fails a build: it says what happened and exits 0.

  GAME_DIR   the game's GAME folder (DATA, SAVE, drivers)
  MAIN.TPIMG the image tools/export_image.py made
  GAME_A     the translated game (work/app-release/game.a)
  OUT.png    where the title screen goes (the program's art/banner.png)
"""
import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import toolchain  # noqa: E402


def main():
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    game, image, game_a, out = sys.argv[1:5]
    inc = os.path.join(toolchain.ROOT, 'vendor', 'freedom-force-recomp', 'recomp', 'runtime')
    with tempfile.TemporaryDirectory() as t:
        runner = os.path.join(t, 'tp-headless' + toolchain.EXE)
        r = subprocess.run([*toolchain.cc(), '-O2', '-w', '-I', inc, '-I', os.path.join(toolchain.ROOT, 'runtime'),
                            *toolchain.host_sources(), game_a, *toolchain.LIBS, '-o', runner], capture_output=True, text=True)
        if r.returncode:
            print('   (the title render could not be built: the launch window goes without its banner)')
            return
        overlay = os.path.join(t, 'o1')
        os.mkdir(overlay)
        env = dict(os.environ, DS_TEST='1', W32_AUDIO_MUTE='1')
        try:
            subprocess.run([runner, '--game', game, '--overlay', overlay, '--image', image, '--out', os.path.join(t, 'title'),
                            '--quantum', '1000', '--blocks', '6000000'], env=env, capture_output=True, timeout=300)
        except subprocess.TimeoutExpired:
            pass
        frame = os.path.join(t, 'title', 'frame.png')
        if not os.path.exists(frame):
            print('   (the title render stopped: the launch window goes without its banner)')
            return
        os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
        shutil.copy(frame, out)
        print('   the launch window\'s banner: your title screen')


if __name__ == '__main__':
    main()
