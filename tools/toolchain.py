"""The C toolchain for the machine the tools run on (docs/PORTING.md): one place for what used to be
`clang -arch arm64`, `-dynamiclib` and `libtool -static`. CC overrides the compiler; TP_CFLAGS adds options to it, and
TP_LD links a game library with that linker instead (the drag-and-drop app's setup: its own clang and ld64.lld, no Xcode
tools on the player's Mac)."""
import os
import platform
import shlex
import subprocess
import sys

SYSTEM = 'macos' if sys.platform == 'darwin' else 'windows' if os.name == 'nt' else 'linux'
EXE = '.exe' if SYSTEM == 'windows' else ''          # (programs the tools build: tp-headless)
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# runtime/ files every host build links (the app's, the headless runner's: + headless.c), and each system's platform
# files (runtime/platform.h).
RUNTIME = ['host.c', 'sha256.c', 'sb16.c', 'verify.c', 'view.c', 'native_land.c', 'native_draw.c', 'qol.c', 'walk.c',
           'bigmap.c', 'freemode.c', 'cinepak.c']
PLATFORM = {'macos': ['platform/macos/platform_macos.c'],
            'linux': ['runtime/platform_posix.c', 'runtime/platform_pool.c'],
            'windows': ['platform/windows/platform_windows.c', 'runtime/platform_pool.c']}[SYSTEM]
LIBS = {'macos': ['-lz'], 'linux': ['-lz', '-lm', '-lpthread'], 'windows': ['-static', '-lz', '-lwinmm', '-lpthread']}[SYSTEM]


def host_sources(headless=True):
    """The C host's sources (repository paths) for this system: runtime/ (+ headless.c) and the platform files."""
    return [os.path.join(ROOT, 'runtime', f) for f in RUNTIME + (['headless.c'] if headless else [])] + \
           [os.path.join(ROOT, f) for f in PLATFORM]


def pic():
    """Position-independent code for objects that go into a shared library (Windows' PE code needs no flag)."""
    return [] if SYSTEM == 'windows' else ['-fPIC']


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
        cmd = [*cc(), *shared(), *(['-undefined', 'dynamic_lookup', '-install_name', '@rpath/libgame.dylib'] if SYSTEM == 'macos' else []),
               '-o', str(out), *map(str, objects)]   # (Linux: a shared library may leave symbols to its loader by default)
    subprocess.run(cmd, check=True)


def shared():
    """Flags that make a shared library (the file name the callers choose is kept: loading does not mind)."""
    return ['-dynamiclib'] if SYSTEM == 'macos' else ['-shared', *pic()]   # (position-independent by default on macOS)


def static_lib(out, objects):
    if sys.platform == 'darwin':
        subprocess.run(['libtool', '-static', '-o', str(out), *map(str, objects)], check=True, capture_output=True)
    else:
        subprocess.run([os.environ.get('AR', 'ar'), 'rcs', str(out), *map(str, objects)], check=True)   # (AR: llvm-ar in MSYS2)
