#!/usr/bin/env python3
"""Extract the pinned owned GOG copy locally, without running its installer: GOG's Mac package (xar/cpio), or GOG's
Windows installer (InnoSetup, read by innoextract). Either way the game's own files end up in
work/extracted/payload/Contents/Resources/game, so every later step is the same."""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path
import pkgextract

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


# What GOG's Windows installer holds besides the game: its DOSBox, redistributables, installer support, temporary files
# and its own records of the install. They stay in work/extracted/installer; the game folder gets the rest.
INSTALLER_ONLY = {'DOSBOX', '__redist', '__support', 'commonappdata', 'tmp', 'app'}
INSTALLER_RECORDS = ('.hashdb', '.info', '.script')      # (goggame-<id>.*: its .ico stays, as in the Mac package)


def which_input(path, pins):
    """'package' or 'windows_installer': the pinned input this file is (size, then SHA-256), or None."""
    size, sha = path.stat().st_size, None
    for kind in ('package', 'windows_installer'):
        pin = pins.get(kind)
        if pin and size == pin['size']:
            sha = sha or digest(path)
            if sha == pin['sha256']:
                return kind
    return None


def extract_installer(installer, extracted, source):
    """GOG's Windows installer, read by innoextract (nothing in it runs), then its game files moved into `source`."""
    tool = shutil.which('innoextract')
    if not tool:
        raise SystemExit("reading GOG's Windows installer needs innoextract: pacman -S mingw-w64-clang-x86_64-innoextract "
                         '(MSYS2), apt install innoextract, or brew install innoextract')
    raw = extracted / 'installer'
    if raw.exists():
        raise SystemExit(f'{raw} is left from an earlier extraction: move it aside, then run this again')
    raw.mkdir(parents=True)
    # (English where GOG's files come in several languages, as in the Mac package: theme_park_enUS)
    subprocess.run([tool, '--silent', '--extract', '--language', 'en-US', '--output-dir', str(raw), str(installer)], check=True)
    source.mkdir(parents=True)
    for item in sorted(raw.iterdir()):
        if item.name not in INSTALLER_ONLY and not (item.name.startswith('goggame-') and item.name.endswith(INSTALLER_RECORDS)):
            shutil.move(str(item), str(source / item.name))
    # GOG's install copies __support/app over the game (goggame-*.script, "DOSBoxConfig"): THEME.CD's sound setup
    # (SNDSETUP.INF) and THEME.BAT, and DOSBox's two settings files. The DOSBox program and the cloud-save copies stay out.
    # The game folder is then the Mac package's, file for file (171 files, checked 2026-11-25).
    overlay = raw / '__support/app'
    for item in sorted(overlay.rglob('*')) if overlay.is_dir() else []:
        rel = item.relative_to(overlay)
        if item.is_file() and rel.parts[0] not in ('DOSBox', 'cloud_saves'):
            (source / rel).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(item, source / rel)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('package', type=Path, help="GOG's Mac package (.pkg) or GOG's Windows installer (.exe)")
    args = ap.parse_args()
    pins = json.loads((ROOT / 'config/inputs.json').read_text())
    package = args.package.resolve()
    kind = which_input(package, pins)
    if kind is None:
        raise SystemExit('unsupported package: size/SHA-256 mismatch; original not modified')
    work = ROOT / 'work'
    archive = work / 'package'
    extracted = work / 'extracted'
    source = extracted / 'payload/Contents/Resources/game'
    if not source.exists() and kind == 'windows_installer':
        extract_installer(package, extracted, source)
    if not source.exists():
        # Both extraction tools read only the hash-pinned package. No postinstall,
        # bundled launcher or DOSBox executable is executed.
        archive.mkdir(parents=True, exist_ok=True)
        scripts = archive / 'package.pkg/Scripts'
        # (macOS: Apple's xar and tar; elsewhere tools/pkgextract.py reads the same archives; TP_PKGEXTRACT=1 forces it)
        native = shutil.which('xar') is not None and not os.environ.get('TP_PKGEXTRACT')
        if not scripts.exists():
            if native:
                subprocess.run(['xar', '-xf', str(package), '-C', str(archive), 'package.pkg/Scripts'], check=True)
            else:
                pkgextract.xar_extract(package, 'package.pkg/Scripts', archive)
        if digest(scripts) != pins['package']['scripts_sha256']:
            raise SystemExit('cached package Scripts changed; preserve it and investigate')
        extracted.mkdir(parents=True, exist_ok=True)
        if native:
            subprocess.run(['tar', '-xf', str(scripts), '-C', str(extracted), './payload/Contents/Resources/game'], check=True)
        else:
            pkgextract.cpio_extract(scripts, extracted, './payload/Contents/Resources/game')
    for name, expected in pins['executables'].items():
        path = source / name
        if path.stat().st_size != expected['size'] or digest(path) != expected['sha256']:
            raise SystemExit(f'extracted executable changed: {name}; keep evidence and investigate')
    print(source)

if __name__ == '__main__':
    main()
