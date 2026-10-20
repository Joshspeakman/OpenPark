#!/usr/bin/env python3
"""Decode 3DO DataStream video (FILM chunks of Cinepak, as on the 3DO Theme Park disc) to RGB frames.

Cinepak: per strip, V1 and V4 codebooks of 2x2 YUV blocks (6 bytes: Y0..Y3 U V); 4x4 blocks drawn from one
V1 entry (each Y doubled) or four V4 entries; key strips code every block, inter strips flag which changed.
Codebooks carry over between frames and from one strip to the next. Written from the format's public
description. Output is game-derived and stays in work/.

  tools/cinepak.py FILE.STREAM OUT.png [FRAME ...]     contact sheet of the given frames (default: 4 spread)
"""
import struct
import sys
from pathlib import Path


def film_frames(data):
    """The Cinepak frames of a 3DO stream, in order."""
    i, out = 0, []
    while i + 8 <= len(data):
        kind, size = data[i:i + 4], int.from_bytes(data[i + 4:i + 8], 'big')
        if size < 8 or size > 0x100000:              # stream blocks are padded to 32 KB
            i = (i // 32768 + 1) * 32768
            continue
        if kind == b'FILM' and data[i + 16:i + 20] == b'FRME':
            n = int.from_bytes(data[i + 24:i + 28], 'big')
            out.append(data[i + 28:i + 28 + n])
        i += size
    return out


def clamp(v):
    return 0 if v < 0 else 255 if v > 255 else v


def entry(b, k):
    y0, y1, y2, y3 = b[k:k + 4]
    u, v = struct.unpack_from('bb', b, k + 4)
    out = []
    for y in (y0, y1, y2, y3):
        out.append((clamp(y + 2 * v), clamp(y - (u >> 1) - v), clamp(y + 2 * u)))
    return out


class Decoder:
    def __init__(self, w=320, h=200):
        self.w, self.h = w, h
        self.px = bytearray(w * h * 3)
        self.books = []                              # per strip: [v4 list, v1 list]

    def codebook(self, book, b, off, end, partial):
        k = 0
        while off < end:
            if partial:
                flags = int.from_bytes(b[off:off + 4], 'big'); off += 4
                for bit in range(32):
                    if off >= end:
                        break
                    if flags & (0x80000000 >> bit):
                        book[k] = entry(b, off); off += 6
                    k += 1
            else:
                book[k] = entry(b, off); off += 6; k += 1

    def put(self, x, y, c):
        if x < self.w and y < self.h:
            p = (y * self.w + x) * 3
            self.px[p:p + 3] = bytes(c)

    def v1(self, x, y, e):
        for j, c in enumerate(e):
            bx, by = x + (j & 1) * 2, y + (j >> 1) * 2
            for dy in (0, 1):
                for dx in (0, 1):
                    self.put(bx + dx, by + dy, c)

    def v4(self, x, y, es):
        for q, e in enumerate(es):
            bx, by = x + (q & 1) * 2, y + (q >> 1) * 2
            for j, c in enumerate(e):
                self.put(bx + (j & 1), by + (j >> 1), c)

    def frame(self, b):
        flags = b[0]
        nstrips = int.from_bytes(b[8:10], 'big')
        off, ytop = 10, 0
        while len(self.books) < nstrips:
            self.books.append([[None] * 256, [None] * 256])
        for s in range(nstrips):
            while int.from_bytes(b[off:off + 2], 'big') not in (0x1000, 0x1100):   # (3DO's 0xfe00 chunk)
                size = int.from_bytes(b[off + 2:off + 4], 'big')
                if off + 4 > len(b) or size == 0:          # (past the frame, or a chunk of no size: it never advanced)
                    raise ValueError('bad strip at %d in a frame of %d bytes' % (off, len(b)))
                off += size
            sid, ssize = struct.unpack_from('>HH', b, off)
            y1, x1, y2, x2 = struct.unpack_from('>HHHH', b, off + 4)
            if s and not (flags & 1):                # a strip starts from the previous strip's codebooks
                self.books[s] = [list(self.books[s - 1][0]), list(self.books[s - 1][1])]
            v4, v1 = self.books[s]
            top = ytop if y1 == 0 else y1
            height = y2 - y1 if y1 == 0 else y2 - y1
            end, c = off + ssize, off + 12
            while c < end:
                cid, csize = struct.unpack_from('>HH', b, c)
                body, cend = c + 4, c + csize
                if cid in (0x2000, 0x2100):
                    self.codebook(v4, b, body, cend, cid == 0x2100)
                elif cid in (0x2200, 0x2300):
                    self.codebook(v1, b, body, cend, cid == 0x2300)
                elif cid in (0x3000, 0x3100, 0x3200):
                    p, bits, mask = body, 0, 0

                    def bit():
                        nonlocal p, bits, mask
                        if not mask:
                            bits = int.from_bytes(b[p:p + 4], 'big') if p + 4 <= cend else 0
                            p += 4; mask = 0x80000000
                        r = bits & mask; mask >>= 1
                        return r
                    for by in range(top, top + height, 4):
                        for bx in range(0, self.w, 4):
                            if cid == 0x3100 and not bit():
                                continue             # unchanged block (needs no data, even at the chunk's end)
                            if cid == 0x3200 or not bit():
                                if p >= cend:
                                    break
                                self.v1(bx, by, v1[b[p]]); p += 1
                            else:
                                if p + 4 > cend:
                                    break
                                self.v4(bx, by, [v4[b[p + q]] for q in range(4)]); p += 4
                c = cend
            ytop = top + height
            off = end
        return bytes(self.px)


def decode(path, wanted):
    frames = film_frames(Path(path).read_bytes())
    d, out = Decoder(), {}
    for k, f in enumerate(frames):
        img = d.frame(f)
        if k in wanted:
            out[k] = img
        if k >= max(wanted):
            break
    return len(frames), out


if __name__ == '__main__':
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from sprites import png
    src, dst = sys.argv[1], sys.argv[2]
    n = len(film_frames(Path(src).read_bytes()))
    want = [int(a) for a in sys.argv[3:]] or [int(n * f) for f in (0.05, 0.35, 0.65, 0.95)]
    total, got = decode(src, set(want))
    W, H = 320, 200
    sheet = bytearray(W * len(got) * H * 3)
    for c, k in enumerate(sorted(got)):
        for y in range(H):
            sheet[(y * W * len(got) + c * W) * 3:(y * W * len(got) + c * W + W) * 3] = got[k][y * W * 3:(y + 1) * W * 3]
    png(dst, W * len(got), H, sheet)
    print('%s: %d frames; wrote %s' % (src, total, dst))
