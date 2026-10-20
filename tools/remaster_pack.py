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


def quantize(bake, w, h, palette, native_rle=True):
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
    for row in rows if native_rle else []:
        gap = 0
        for p in row:
            if p is None:
                gap += 1
            else:
                if gap > 128:
                    raise ValueError('Bake has an internal transparent gap wider than the native blitter supports')
                gap = 0
    return rows


def display_bounds(record):
    bounds = record.get('display_bounds', dict(x=0,y=0,width=record['width'],height=record['height']))
    if (not isinstance(bounds,dict) or set(bounds)!={'x','y','width','height'}
        or any(type(v) is not int for v in bounds.values())
        or not all(-2048<=bounds[k]<=2048 for k in ('x','y'))
        or not all(1<=bounds[k]<=2048 for k in ('width','height'))):
        raise ValueError('Invalid replacement display bounds')
    return bounds


def unchanged_source(rows, bounds, record):
    """Transparent padding cannot turn unchanged original art into a remaster."""
    w,h=record['width'],record['height'];x0,y0=bounds['x'],bounds['y']
    crop=[[None]*w for _ in range(h)]
    for y,row in enumerate(rows):
        for x,pixel in enumerate(row):
            xx,yy=x+x0,y+y0
            if 0<=xx<w and 0<=yy<h:crop[yy][xx]=pixel
            elif pixel is not None:return False
    return pixel_hash(crop)==int(record['source_hash'],16)


def encode(records):
    """Synthetic fixtures may call this directly; production CLI audits first."""
    if not 1<=len(records)<=8192:
        raise ValueError('Invalid world part count')
    expanded=any('display_bounds' in record for record,rows in records)
    out = bytearray((b'TPRART02' if expanded else b'TPRART01') + struct.pack('<I',len(records)))
    previous = None
    total=0
    for record, rows in records:
        key = record['bank'],record['index']
        w,h = record['width'],record['height']
        if (previous is not None and key <= previous) or key[0] not in (0,1) or not 0 <= key[1] < 8192:
            raise ValueError('Unsorted or invalid world part ID')
        bounds=display_bounds(record);dw,dh=bounds['width'],bounds['height']
        if not 1 <= w <= 255 or not 1 <= h <= 255 or len(rows)!=dh or any(len(row)!=dw for row in rows):
            raise ValueError('Invalid world part dimensions')
        total+=dw*dh*2 if expanded else dw*dh*3+dh
        if total>64*1024*1024:raise ValueError('World pack exceeds decoded memory limit')
        previous = key
        out += struct.pack('<HHHHQ',*key,w,h,int(record['source_hash'],16))
        if expanded:out+=struct.pack('<hhHH',bounds['x'],bounds['y'],dw,dh)
        for row in rows:
            for pixel in row:
                if pixel is not None and not 0 <= pixel <= 255:
                    raise ValueError('Invalid palette index')
                out += struct.pack('<H',256 if pixel is None else pixel)
    return bytes(out)


def encode_rgba(records):
    """V3 stores the full bake and a separate indexed bookkeeping preview.

    Placement remains in decoded source pixels; texel density never changes
    the native pivot or footprint. The production CLI still requires audit.
    """
    if not 1 <= len(records) <= 8192:
        raise ValueError('Invalid world part count')
    out = bytearray(b'TPRART03' + struct.pack('<I', len(records)))
    previous, preview_total, rgba_total = None, 0, 0
    for record, rows, bake in records:
        key = record['bank'], record['index']
        w, h = record['width'], record['height']
        bounds = display_bounds(record)
        dw, dh = bounds['width'], bounds['height']
        bw, bh, rgba = bake
        if (previous is not None and key <= previous) or key[0] not in (0, 1) or not 0 <= key[1] < 8192:
            raise ValueError('Unsorted or invalid world part ID')
        if not 1 <= w <= 255 or not 1 <= h <= 255 or len(rows) != dh or any(len(row) != dw for row in rows):
            raise ValueError('Invalid world part dimensions')
        if (not 1 <= bw <= 2048 or not 1 <= bh <= 2048 or len(rgba) != bw*bh*4
            or bw % dw or bh % dh or bw//dw != bh//dh or not 1 <= bw//dw <= 8):
            raise ValueError('RGBA bake must have uniform density 1..8 within 2048 texels per side')
        preview_total += dw*dh*2
        rgba_total += len(rgba)
        if preview_total > 64*1024*1024 or rgba_total > 512*1024*1024:
            raise ValueError('World pack exceeds decoded memory limit')
        previous = key
        out += struct.pack('<HHHHQhhHHHH', *key, w, h, int(record['source_hash'], 16),
                           bounds['x'], bounds['y'], dw, dh, bw//dw, 0)
        for row in rows:
            for pixel in row:
                if pixel is not None and not 0 <= pixel <= 255:
                    raise ValueError('Invalid palette index')
                out += struct.pack('<H', 256 if pixel is None else pixel)
        out += rgba
    return bytes(out)


def palette_expansion(bake, rows, palette):
    """True only for nearest-expanded palette artwork with no new detail.

    A faithful new bake may retain the same low-resolution centre samples
    while adding actual geometry/detail between them; do not discard that.
    """
    bw, bh, rgba = bake
    h, w = len(rows), len(rows[0])
    colours = [bytes(v*255//63 for v in palette[i:i+3]) for i in range(0, 768, 3)]
    for y in range(bh):
        for x in range(bw):
            pixel = rows[y*h//bh][x*w//bw]
            p = rgba[(y*bw+x)*4:(y*bw+x)*4+4]
            if pixel is None:
                if p[3] >= 128:
                    return False
            elif p[3] < 128 or p[:3] != colours[pixel]:
                return False
    return True


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
        record=dict(part)
        if 'display_bounds' in done:record['display_bounds']=done['display_bounds']
        bounds=display_bounds(record)
        rows=quantize(bake,bounds['width'],bounds['height'],palette,native_rle=False)
        if unchanged_source(rows,bounds,part) and palette_expansion(bake,rows,palette):
            raise SystemExit(f"{part['id']}: unchanged original pixels are not a remaster")
        # Native draw logs identify a shared RLE pointer, not an alias's table
        # slot. Conflicting replacements for the same source would be ambiguous.
        key=part['bank'],part['source_offset'],part['width'],part['height']
        replacement=(bounds,rows,bake)
        if key in aliases and aliases[key]!=replacement:
            raise SystemExit(f"{part['id']}: conflicting artwork for aliased source data")
        aliases[key]=replacement
        records.append((record,rows,bake))
    payload=encode_rgba(records)
    a.out.mkdir(parents=True,exist_ok=True)
    # Publish the completion receipt last, after the new payload is in place.
    fd,name=tempfile.mkstemp(prefix='world-',suffix='.tmp',dir=a.out)
    with os.fdopen(fd,'wb') as f:
        f.write(payload);f.flush();os.fsync(f.fileno())
    os.replace(name,a.out/'world.art')
    receipt=dict(schema=1,complete=True,art_mode='reimagined',parts=len(records),world_format='TPRART03',
                 world_sha256=hashlib.sha256(payload).hexdigest(),
                 ledger_sha256=hashlib.sha256(a.ledger.read_bytes()).hexdigest(),
                 completion_sha256=hashlib.sha256(a.completion.read_bytes()).hexdigest())
    (a.out/'release.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps(receipt))


if __name__=='__main__':
    main()
