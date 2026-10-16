#!/usr/bin/env python3
"""Extract the pinned owned GOG package locally, without running its installer."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('package', type=Path)
    args = ap.parse_args()
    pins = json.loads((ROOT / 'config/inputs.json').read_text())
    package = args.package.resolve()
    expected = pins['package']
    if package.stat().st_size != expected['size'] or digest(package) != expected['sha256']:
        raise SystemExit('unsupported package: size/SHA-256 mismatch; original not modified')
    work = ROOT / 'work'
    archive = work / 'package'
    extracted = work / 'extracted'
    source = extracted / 'payload/Contents/Resources/game'
    if not source.exists():
        # Both extraction tools read only the hash-pinned package. No postinstall,
        # bundled launcher or DOSBox executable is executed.
        archive.mkdir(parents=True, exist_ok=True)
        scripts = archive / 'package.pkg/Scripts'
        if not scripts.exists():
            subprocess.run(['xar', '-xf', str(package), '-C', str(archive), 'package.pkg/Scripts'], check=True)
        if digest(scripts) != pins['package']['scripts_sha256']:
            raise SystemExit('cached package Scripts changed; preserve it and investigate')
        extracted.mkdir(parents=True, exist_ok=True)
        subprocess.run(['tar', '-xf', str(scripts), '-C', str(extracted), './payload/Contents/Resources/game'], check=True)
    for name, expected in pins['executables'].items():
        path = source / name
        if path.stat().st_size != expected['size'] or digest(path) != expected['sha256']:
            raise SystemExit(f'extracted executable changed: {name}; keep evidence and investigate')
    print(source)

if __name__ == '__main__':
    main()
