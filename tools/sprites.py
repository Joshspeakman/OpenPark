#!/usr/bin/env python3
"""Decode a Theme Park sprite bank (NAME.TAB + NAME.DAT) into a contact sheet PNG.

Survey tool for the native renderer (docs/PLAN.md, engine). Bullfrog's format, as in
Theme Hospital: the .TAB is 6-byte entries (uint32 offset into the .DAT, uint8 width,
uint8 height); each sprite in the .DAT is rows of signed run bytes: n > 0 copies n
pixels, n < 0 skips -n transparent pixels, 0 ends the row. Colours are palette
indices into a 768-byte 6-bit VGA palette (DATA/MPALETTE.DAT).

    tools/sprites.py DATA/MBLK-0 --palette DATA/MPALETTE.DAT --out work/sprites/mblk.png

Output stays under work/ (game-derived; never committed).
"""
import argparse
import struct
import zlib
from pathlib import Path


def load_bank(stem):
    tab = Path(str(stem) + '.TAB').read_bytes()
    dat = Path(str(stem) + '.DAT').read_bytes()
    sprites = []
    for k in range(len(tab) // 6):
        off, w, h = struct.unpack_from('<IBB', tab, 6 * k)
        sprites.append(decode(dat, off, w, h))
    return sprites


def decode(dat, off, w, h):
    """Rows of palette indices (None = transparent); raises on a malformed sprite."""
    rows, p = [], off
    for _ in range(h):
        row = []
        while True:
            n = struct.unpack_from('<b', dat, p)[0]; p += 1
            if n == 0:
                break
            if n < 0:
                row += [None] * -n
            else:
                row += list(dat[p:p + n]); p += n
        if len(row) > w:
            raise ValueError('row wider than sprite at %#x' % off)
        rows.append(row + [None] * (w - len(row)))
    return w, h, rows


def png(path, w, h, rgb):
    raw = b''.join(b'\0' + bytes(rgb[y * w * 3:(y + 1) * w * 3]) for y in range(h))
    def chunk(t, c):
        return struct.pack('>I', len(c)) + t + c + struct.pack('>I', zlib.crc32(t + c) & 0xffffffff)
    Path(path).write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                           + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('bank', help='path without extension, e.g. DATA/MBLK-0')
    ap.add_argument('--palette', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--columns', type=int, default=16)
    args = ap.parse_args()
    pal = Path(args.palette).read_bytes()[:768]
    pal = [min(255, v * 255 // 63) for v in pal]
    sprites = load_bank(args.bank)
    cell_w = max([s[0] for s in sprites] + [1]) + 2
    cell_h = max([s[1] for s in sprites] + [1]) + 2
    cols = args.columns
    W, H = cols * cell_w, (len(sprites) + cols - 1) // cols * cell_h
    img = bytearray([40, 0, 40] * W * H)            # magenta-dark background shows transparency
    for k, (w, h, rows) in enumerate(sprites):
        ox, oy = k % cols * cell_w + 1, k // cols * cell_h + 1
        for y, row in enumerate(rows):
            for x, c in enumerate(row):
                if c is not None:
                    i = ((oy + y) * W + ox + x) * 3
                    img[i:i + 3] = bytes(pal[3 * c:3 * c + 3])
    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    png(args.out, W, H, img)
    print('%d sprites, sheet %dx%d -> %s' % (len(sprites), W, H, args.out))


if __name__ == '__main__':
    main()
