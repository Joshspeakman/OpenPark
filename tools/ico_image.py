#!/usr/bin/env python3
"""The largest picture of a Windows .ico (GOG's icon from the package) as PNG and as a 32-bit BMP (SDL's window
icon), without image libraries: tools/build_linux.sh and tools/build_windows.sh. PNG entries (8- or 16-bit RGB/RGBA,
not interlaced) and 32-bit DIB entries are read; anything else is skipped.
  ico_image.py ICON.ico --png icon.png --bmp icon.bmp"""
import argparse
import struct
import zlib
from pathlib import Path


def _png_rgba(data):
    """RGBA rows (bytes per row, top down) of an 8- or 16-bit RGB or RGBA PNG (16 bits: rounded to 8)."""
    at, idat, w = 8, b'', 0
    while at < len(data):
        n, kind = struct.unpack('>I4s', data[at:at + 8])
        body = data[at + 8:at + 8 + n]
        if kind == b'IHDR':
            w, h, depth, ctype, _, _, interlace = struct.unpack('>IIBBBBB', body)
            if depth not in (8, 16) or ctype not in (2, 6) or interlace:
                raise ValueError('unsupported PNG')
        elif kind == b'IDAT':
            idat += body
        at += 12 + n
    channels, size = (4 if ctype == 6 else 3), depth // 8
    bpp = channels * size
    raw, stride, rows, prev = zlib.decompress(idat), w * bpp, [], bytearray(w * bpp)
    for y in range(h):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b, c = prev[i], prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c; pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line
        line8 = bytes(line) if size == 1 else bytes((line[i] * 256 + line[i + 1] + 128) // 257 for i in range(0, stride, 2))
        rows.append(line8 if channels == 4 else b''.join(line8[i:i + 3] + b'\xff' for i in range(0, w * 3, 3)))
    return w, h, rows


def _dib_rgba(data):
    size, w, h2, _, bits = struct.unpack('<IiiHH', data[:16])
    if bits != 32:
        raise ValueError('unsupported DIB')
    h = abs(h2) // 2
    px = data[size:size + w * h * 4]
    rows = [b''.join(bytes((px[o + 2], px[o + 1], px[o], px[o + 3])) for o in range(r * w * 4, (r + 1) * w * 4, 4)) for r in range(h)]
    return w, h, rows[::-1]                     # (bottom-up)


def largest(ico):
    data = Path(ico).read_bytes()
    _, kind, count = struct.unpack('<HHH', data[:6])
    if kind != 1:
        raise ValueError('not an icon')
    best = None
    for i in range(count):
        w, h, _, _, _, _, n, at = struct.unpack('<BBBBHHII', data[6 + 16 * i:22 + 16 * i])
        body = data[at:at + n]
        try:
            pic = _png_rgba(body) if body[:8] == b'\x89PNG\r\n\x1a\n' else _dib_rgba(body)
        except (ValueError, struct.error, zlib.error):
            continue
        if not best or pic[0] * pic[1] > best[0] * best[1]:
            best = pic
    if not best:
        raise ValueError('no picture this tool reads')
    return best


def write_png(path, w, h, rows):
    chunk = lambda k, b: struct.pack('>I', len(b)) + k + b + struct.pack('>I', zlib.crc32(k + b))
    raw = b''.join(b'\0' + r for r in rows)
    Path(path).write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
                           + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def write_bmp(path, w, h, rows):
    """BITMAPV4HEADER with an alpha mask, bottom-up BGRA."""
    px = b''.join(b''.join(bytes((r[o + 2], r[o + 1], r[o], r[o + 3])) for o in range(0, w * 4, 4)) for r in rows[::-1])
    head = struct.pack('<IiiHHIIiiII', 108, w, h, 1, 32, 3, len(px), 2835, 2835, 0, 0)
    head += struct.pack('<IIII', 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000) + b'BGRs' + bytes(36) + bytes(12)
    Path(path).write_bytes(b'BM' + struct.pack('<IHHI', 14 + len(head) + len(px), 0, 0, 14 + len(head)) + head + px)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('ico', type=Path)
    ap.add_argument('--png', type=Path)
    ap.add_argument('--bmp', type=Path)
    a = ap.parse_args()
    w, h, rows = largest(a.ico)
    if a.png:
        write_png(a.png, w, h, rows)
    if a.bmp:
        write_bmp(a.bmp, w, h, rows)
    print(f'{w}x{h}')


if __name__ == '__main__':
    main()
