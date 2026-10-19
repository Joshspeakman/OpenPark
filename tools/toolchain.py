"""The C toolchain for the machine the tools run on (docs/PORTING.md): one place for what used to be
`clang -arch arm64`, `-dynamiclib` and `libtool -static`. CC overrides the compiler."""
import os
import platform
import subprocess
import sys


def arch():
    m = platform.machine().lower()
    return {'arm64': 'arm64', 'aarch64': 'arm64', 'x86_64': 'x86_64', 'amd64': 'x86_64'}.get(m, m)


def cc():
    return [os.environ.get('CC', 'clang')] + (['-arch', arch()] if sys.platform == 'darwin' else [])


def shared():
    """Flags that make a shared library (the file name the callers choose is kept: loading does not mind)."""
    return ['-dynamiclib'] if sys.platform == 'darwin' else ['-shared', '-fPIC']   # (position-independent by default on macOS)


def static_lib(out, objects):
    if sys.platform == 'darwin':
        subprocess.run(['libtool', '-static', '-o', str(out), *map(str, objects)], check=True, capture_output=True)
    else:
        subprocess.run(['ar', 'rcs', str(out), *map(str, objects)], check=True)
