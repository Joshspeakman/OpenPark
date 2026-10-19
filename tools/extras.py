#!/usr/bin/env python3
"""Import extra content from the owner's other Theme Park discs (like OpenRA's content installer).

  tools/extras.py [--data DIR] SOURCE...        import what is recognised
  tools/extras.py --list [--data DIR]           what is installed

A SOURCE is a .cue (with its .bin files), an .iso or .img disk image (mounted read-only for the scan), a
mounted volume or any folder. config/extras.json says what each recognised disc gives; it holds hashes only.
Imported files go to DIR/extras/<pack>/ (DIR: TP_DATA or ~/Games/ThemeParkNative) with a manifest.json the app
reads. Nothing is copied into this project.

Today: the Amiga CD32 disc's 25 audio tracks become the "Amiga CD32" music (Launcher > Sound > Music Type).
Other discs are recognised and reported (docs/reports/2026-10-19-other-editions.md).
"""
import argparse
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SECTOR = 2352


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def parse_cue(path):
    """[(track number, mode, file path, INDEX 01 sector)] from a cue sheet (one FILE per track, Redump style,
    or one FILE for all tracks)."""
    tracks, cur_file = [], None
    for line in path.read_text(errors='replace').splitlines():
        line = line.strip()
        if line.startswith('FILE '):
            cur_file = path.parent / re.match(r'FILE\s+"?([^"]+?)"?\s+\w+$', line).group(1)
        elif line.startswith('TRACK '):
            n, mode = line.split()[1:3]
            tracks.append([int(n), mode, cur_file, None])
        elif line.startswith('INDEX 01') and tracks:
            mm, ss, ff = map(int, line.split()[2].split(':'))
            tracks[-1][3] = (mm * 60 + ss) * 75 + ff
    return tracks


def cue_tracks(path):
    """{track: (mode, bytes from INDEX 01 to the track's end)}."""
    tracks, out = parse_cue(path), {}
    for k, (n, mode, f, start) in enumerate(tracks):
        data = f.read_bytes()
        nxt = tracks[k + 1] if k + 1 < len(tracks) else None
        end = nxt[3] * SECTOR if nxt and nxt[2] == f else len(data)
        out[n] = (mode, data[start * SECTOR:end])
    return out


def data_track_user(raw):
    return b''.join(raw[i + 16:i + 16 + 2048] for i in range(0, len(raw) - SECTOR + 1, SECTOR))


def find_ci(root, rel):
    p = Path(root)
    for part in rel.split('/'):
        if not p.is_dir():
            return None
        match = [c for c in p.iterdir() if c.name.lower() == part.lower()]
        if not match:
            return None
        p = match[0]
    return p if p.is_file() else None


def identify_folder(root, packs):
    for name, pack in packs.items():
        rel, want = pack['key_file']
        f = find_ci(root, rel)
        if f and sha256(f.read_bytes()) == want:
            return name
    return None


def wav(pcm):
    return (b'RIFF' + struct.pack('<I', 36 + len(pcm)) + b'WAVEfmt ' + struct.pack('<IHHIIHH', 16, 1, 2, 44100, 176400, 4, 16)
            + b'data' + struct.pack('<I', len(pcm)) + pcm)


def import_cd32_audio(audio, pack, dest):
    want, got, ok = pack['audio_tracks'], {}, True
    for n, pcm in sorted(audio.items()):
        h = sha256(pcm)
        if want.get(str(n)) != h:
            ok = False
        got[n] = pcm
    missing = [int(k) for k in want if int(k) not in got]
    if missing:
        return 'audio tracks missing: %s' % ', '.join(map(str, missing))
    dest.mkdir(parents=True, exist_ok=True)
    for n, pcm in got.items():
        (dest / ('track%02d.wav' % n)).write_bytes(wav(pcm))
    manifest = {'pack': 'amiga-cd32', 'title': pack['title'], 'tracks': sorted(got),
                'tune_tracks': pack['tune_tracks'], 'verified': ok}
    (dest / 'manifest.json').write_text(json.dumps(manifest, indent=1) + '\n')
    return '%d audio tracks imported%s' % (len(got), '' if ok else ' (some differ from the known dump: imported anyway)')


def scan(source, packs, data):
    src = Path(source).expanduser()
    if src.suffix.lower() == '.cue':
        tracks = cue_tracks(src)
        data_tr = [t for t in tracks.values() if t[0].startswith('MODE')]
        user = data_track_user(data_tr[0][1]) if data_tr else b''
        h = sha256(user)
        for name, pack in packs.items():
            if pack.get('data_track_sha256') == h:
                if name != 'amiga-cd32':
                    return name, pack['gives']
                audio = {n: d for n, (mode, d) in tracks.items() if mode == 'AUDIO'}
                return name, import_cd32_audio(audio, pack, data / 'extras' / name)
        return None, 'not a known disc'
    if src.suffix.lower() in ('.iso', '.img', '.dmg'):
        raw = src.read_bytes()
        for name, pack in packs.items():
            if pack.get('data_track_sha256') == sha256(raw):
                return name, 'data track only: the audio tracks are missing (dump the whole disc as bin/cue)'
        mounted = already_mounted(src)                   # (attached already: scan where it is)
        if mounted:
            return scan_folder(mounted, packs)
        mnt = tempfile.mkdtemp(prefix='tp-extras-')
        r = subprocess.run(['hdiutil', 'attach', '-readonly', '-nobrowse', '-mountpoint', mnt, str(src)], capture_output=True, text=True)
        if r.returncode:
            return None, 'could not be mounted: ' + r.stderr.strip().splitlines()[-1]
        try:
            return scan_folder(mnt, packs)
        finally:
            subprocess.run(['hdiutil', 'detach', mnt], capture_output=True)
    if src.is_dir():
        cues = sorted(src.glob('*.cue')) or sorted(src.glob('*/*.cue'))
        if cues:
            return scan(cues[0], packs, data)
        return scan_folder(src, packs)
    return None, 'not a disc image, cue sheet or folder'


def already_mounted(image):
    info = subprocess.run(['hdiutil', 'info'], capture_output=True, text=True).stdout
    for block in info.split('================================================'):
        m = re.search(r'image-path\s*:\s*(.+)', block)
        if m and Path(m.group(1).strip()).resolve() == image.resolve():
            mp = re.findall(r'^/dev/\S+\s+\S*\s*(/Volumes/.+)$', block, re.M)
            if mp:
                return mp[0].strip()
    return None


def scan_folder(root, packs):
    name = identify_folder(root, packs)
    if not name:
        return None, 'not a known disc'
    if name == 'amiga-cd32':
        return name, 'data track only: the audio tracks are missing (dump the whole disc as bin/cue)'
    return name, packs[name]['gives']


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('sources', nargs='*')
    ap.add_argument('--data', type=Path, default=Path(os.environ.get('TP_DATA', Path.home() / 'Games/ThemeParkNative')))
    ap.add_argument('--list', action='store_true')
    args = ap.parse_args()
    packs = json.loads((ROOT / 'config/extras.json').read_text())['packs']
    if args.list or not args.sources:
        for name, pack in packs.items():
            m = args.data / 'extras' / name / 'manifest.json'
            print('%-12s %-34s %s' % (name, pack['title'], 'installed' if m.exists() else '-'))
        return
    for s in args.sources:
        name, what = scan(s, packs, args.data)
        print('%s: %s%s' % (s, (packs[name]['title'] + ': ') if name else '', what))


if __name__ == '__main__':
    main()
