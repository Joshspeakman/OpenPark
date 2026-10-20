#!/usr/bin/env python3
"""Compile canonical model bakes into a world-only remaster pack.

Production compilation requires the complete coverage audit. There is no
partial-pack release switch. Does not modify original resources or install apps.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import os

from remaster_catalog import audit, pixel_hash


def read_bake(path):
    raw = path.read_bytes()
    if len(raw) < 16 or raw[:8] != b'TPBAKE01':
        raise ValueError(f'Invalid model bake: {path}')
    w, h = struct.unpack_from('<II', raw, 8)
    if not 1 <= w <= 2048 or not 1 <= h <= 2048 or len(raw) != 16 + w*h*4:
        raise ValueError(f'Invalid bake dimensions: {path}')
    return w, h, raw[16:]


def quantize(bake, w, h, palette):
    """Sample at pixel centres. The UI's 256-colour palette is never replaced."""
    bw, bh, rgba = bake
    if bw % w or bh % h or bw//w != bh//h:
        raise ValueError('Bake must be an integer uniform scale of its source part')
    colours = [tuple(min(255, v*255//63) for v in palette[i:i+3]) for i in range(0,768,3)]
    cache, rows = {}, []
    for y in range(h):
        row = []
        for x in range(w):
            sx = min(bw-1, int((x+.5)*bw/w))
            sy = min(bh-1, int((y+.5)*bh/h))
            c = tuple(rgba[(sy*bw+sx)*4:(sy*bw+sx)*4+4])
            if c[3] < 128:
                row.append(None)
                continue
            rgb = c[:3]
            if rgb not in cache:
                cache[rgb] = min(range(256), key=lambda i: sum((rgb[j]-colours[i][j])**2 for j in range(3)))
            row.append(cache[rgb])
        rows.append(row)
    # Native skip/run decoder cannot represent two adjacent skip commands.
    for row in rows:
        gap = 0
        for p in row:
            if p is None:
                gap += 1
            else:
                if gap > 128:
                    raise ValueError('Bake has an internal transparent gap wider than the native blitter supports')
                gap = 0
    return rows


def encode(records):
    """Synthetic fixtures may call this directly; production CLI audits first."""
    out = bytearray(b'TPRART01' + struct.pack('<I',len(records)))
    previous = None
    for record, rows in records:
        key = record['bank'],record['index']
        w,h = record['width'],record['height']
        if (previous is not None and key <= previous) or key[0] not in (0,1) or not 0 <= key[1] < 8192:
            raise ValueError('Unsorted or invalid world part ID')
        if not 1 <= w <= 255 or not 1 <= h <= 255 or len(rows)!=h or any(len(row)!=w for row in rows):
            raise ValueError('Invalid world part dimensions')
        previous = key
        out += struct.pack('<HHHHQ',*key,w,h,int(record['source_hash'],16))
        for row in rows:
            for pixel in row:
                if pixel is not None and not 0 <= pixel <= 255:
                    raise ValueError('Invalid palette index')
                out += struct.pack('<H',256 if pixel is None else pixel)
    return bytes(out)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('ledger',type=Path)
    p.add_argument('completion',type=Path)
    p.add_argument('--palette',required=True,type=Path)
    p.add_argument('--out',required=True,type=Path)
    a = p.parse_args()
    ledger=json.loads(a.ledger.read_text());completion=json.loads(a.completion.read_text())
    root=a.completion.parent
    report=audit(ledger,completion,root)
    if not report['complete']:
        raise SystemExit(f"Refusing incomplete remaster release: {report['blocker_count']} unresolved requirements")
    palette=a.palette.read_bytes()
    if len(palette)!=768 or any(v>63 for v in palette):
        raise SystemExit('Expected the original 6-bit VGA palette')
    records=[];aliases={}
    for part in sorted(ledger['parts'],key=lambda p:(p['bank'],p['index'])):
        if not part['required']:
            continue
        done=completion['parts'][part['id']]
        bake=read_bake(root/done['overhead_bake']['path'])
        rows=quantize(bake,part['width'],part['height'],palette)
        if pixel_hash(rows)==int(part['source_hash'],16):
            raise SystemExit(f"{part['id']}: unchanged original pixels are not a remaster")
        # Native draw logs identify a shared RLE pointer, not an alias's table
        # slot. Conflicting replacements for the same source would be ambiguous.
        key=part['bank'],part['source_offset'],part['width'],part['height']
        if key in aliases and aliases[key]!=rows:
            raise SystemExit(f"{part['id']}: conflicting artwork for aliased source data")
        aliases[key]=rows
        records.append((part,rows))
    payload=encode(records)
    a.out.mkdir(parents=True,exist_ok=True)
    # Publish the completion receipt last, after the new payload is in place.
    fd,name=tempfile.mkstemp(prefix='world-',suffix='.tmp',dir=a.out)
    with os.fdopen(fd,'wb') as f:
        f.write(payload);f.flush();os.fsync(f.fileno())
    os.replace(name,a.out/'world.art')
    receipt=dict(schema=1,complete=True,art_mode='reimagined',parts=len(records),
                 world_sha256=hashlib.sha256(payload).hexdigest(),
                 ledger_sha256=hashlib.sha256(a.ledger.read_bytes()).hexdigest(),
                 completion_sha256=hashlib.sha256(a.completion.read_bytes()).hexdigest())
    (a.out/'release.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps(receipt))


if __name__=='__main__':
    main()
