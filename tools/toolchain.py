"""The C toolchain for the machine the tools run on (docs/PORTING.md): one place for what used to be
`clang -arch arm64`, `-dynamiclib` and `libtool -static`. CC overrides the compiler; TP_CFLAGS adds options to it, and
TP_LD links a game library with that linker instead (the drag-and-drop app's setup: its own clang and ld64.lld, no Xcode
tools on the player's Mac)."""
import os
import platform
import shlex
import subprocess
import sys


def arch():
    m = platform.machine().lower()
    return {'arm64': 'arm64', 'aarch64': 'arm64', 'x86_64': 'x86_64', 'amd64': 'x86_64'}.get(m, m)


def cc():
    extra = shlex.split(os.environ.get('TP_CFLAGS', ''))
    return [os.environ.get('CC', 'clang')] + ([] if extra else ['-arch', arch()] if sys.platform == 'darwin' else []) + extra


def link_game_library(out, objects):
    """The lifted objects as a library the app (or the headless runner) binds at load: `@rpath/libgame.dylib`, the
    host's own symbols left to it (TP_LD: ld64.lld with no SDK; else clang)."""
    ld = os.environ.get('TP_LD')
    if ld:
        cmd = [ld, '-dylib', '-arch', 'arm64', '-platform_version', 'macos', '14.0', '14.0', '-undefined', 'dynamic_lookup',
               '-install_name', '@rpath/libgame.dylib', '-o', str(out), *map(str, objects)]
    else:
        cmd = [*cc(), *shared(), '-undefined', 'dynamic_lookup', '-install_name', '@rpath/libgame.dylib', '-o', str(out), *map(str, objects)]
    subprocess.run(cmd, check=True)


def shared():
    """Flags that make a shared library (the file name the callers choose is kept: loading does not mind)."""
    return ['-dynamiclib'] if sys.platform == 'darwin' else ['-shared', '-fPIC']   # (position-independent by default on macOS)


def static_lib(out, objects):
    if sys.platform == 'darwin':
        subprocess.run(['libtool', '-static', '-o', str(out), *map(str, objects)], check=True, capture_output=True)
    else:
        subprocess.run(['ar', 'rcs', str(out), *map(str, objects)], check=True)
