"""The C toolchain for the host this runs on: macOS (Apple Silicon), Linux or Windows (MSYS2 CLANG64/UCRT64).

Every tool that compiles lifted code takes its compiler and flags from here, so a build on each system makes
the same objects the same way. TP_CC overrides the compiler (default clang)."""
import os
import platform
import sys

SYSTEM = 'macos' if sys.platform == 'darwin' else 'windows' if os.name == 'nt' else 'linux'
CC = os.environ.get('TP_CC', 'clang')
# macOS builds stay Apple Silicon code; elsewhere the compiler's own target (x86-64 or ARM64).
ARCH_FLAGS = ['-arch', 'arm64'] if SYSTEM == 'macos' else []
PIC = [] if SYSTEM == 'windows' else ['-fPIC']
SHARED = ['-dynamiclib'] if SYSTEM == 'macos' else ['-shared']
SHLIB = {'macos': '.dylib', 'linux': '.so', 'windows': '.dll'}[SYSTEM]
EXE = '.exe' if SYSTEM == 'windows' else ''
# (MinGW: localtime_r and friends are declared only when asked for)
DEFINES = ['-D_POSIX_THREAD_SAFE_FUNCTIONS=200112L'] if SYSTEM == 'windows' else []
LIBS = {'macos': ['-lz'], 'linux': ['-lz', '-lm', '-lpthread'],
        'windows': ['-static', '-lz', '-lwinmm', '-lpthread']}[SYSTEM]


def architecture():
    """The CPU the compiled code is for, as build.json records it."""
    if SYSTEM == 'macos':
        return 'arm64'
    machine = platform.machine().lower()
    return {'amd64': 'x86_64', 'x64': 'x86_64', 'aarch64': 'arm64'}.get(machine, machine)


def compile_object(source, obj, opt, includes):
    return [CC, *ARCH_FLAGS, *DEFINES, opt, '-c', *PIC, '-w', *(f for i in includes for f in ('-I', str(i))), str(source), '-o', str(obj)]


def shared_library(inputs, library, opt=None, includes=()):
    """A loadable library for ctypes (the differential tools); `inputs` are sources or objects."""
    return [CC, *ARCH_FLAGS, *DEFINES, *([opt] if opt else []), *SHARED, *(['-fPIC'] if SYSTEM == 'linux' else []), *(f for i in includes for f in ('-I', str(i))),
            *map(str, inputs), '-o', str(library)]


def static_archive(objects, archive):
    if SYSTEM == 'macos':
        return ['libtool', '-static', '-o', str(archive), *map(str, objects)]
    return [os.environ.get('TP_AR', 'ar'), 'rcs', str(archive), *map(str, objects)]


def executable(inputs, output, opt='-O2', includes=()):
    return [CC, *ARCH_FLAGS, *DEFINES, opt, '-w', *(f for i in includes for f in ('-I', str(i))), *map(str, inputs),
            *LIBS, '-o', str(output)]
