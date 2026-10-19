#!/usr/bin/env python3
"""Unpack Rob Northen (RNC ProPack) method-1 blocks, as Bullfrog used for packed data and demo archives.

Method 1 is a Huffman/LZ format: per chunk three Huffman tables (literal run lengths, match distances, match
lengths), literal bytes stored inline in the bit stream. Written from the format's public description
(Simon Tatham's dernc). Used by tools/extras.py to read owned demo disks; the output is game-derived and
stays in work/.

  tools/rnc.py FILE OUTDIR      unpack every RNC block found in FILE into OUTDIR/block-NN.bin
"""
import struct
import sys
from pathlib import Path


class Bits:
    def __init__(self, data, pos):
        self.d, self.p = data, pos
        self.buf, self.count = self.lword(), 16

    def lword(self):
        b = self.d[self.p:self.p + 4] + b'\0\0\0\0'
        return b[0] | b[1] << 8 | b[2] << 16 | b[3] << 24

    def advance(self, n):
        self.buf >>= n
        self.count -= n
        if self.count < 16:
            self.p += 2
            self.buf |= self.lword() << self.count
            self.count += 16

    def read(self, n):
        v = self.buf & ((1 << n) - 1)
        self.advance(n)
        return v

    def fix(self):                               # after inline literal bytes
        self.count -= 16
        self.buf &= (1 << self.count) - 1
        self.buf |= self.lword() << self.count
        self.count += 16


def table(bits):
    n = bits.read(5)
    if not n:
        return []
    lens = [bits.read(4) for _ in range(n)]
    out, code = [], 0
    for length in range(1, max(lens) + 1):
        for value, l in enumerate(lens):
            if l == length:
                out.append((int(format(code, '0%db' % length)[::-1], 2), length, value))
                code += 1
        code <<= 1
    return out


def decode(bits, t):
    for code, length, value in t:
        if bits.buf & ((1 << length) - 1) == code:
            bits.advance(length)
            if value >= 2:
                v = 1 << (value - 1)
                return v | bits.read(value - 1)
            return value
    raise ValueError('bad Huffman code')


def unpack(data, at=0):
    """Unpack the RNC block at data[at:]; returns (bytes, packed length including the header)."""
    if data[at:at + 3] != b'RNC' or data[at + 3] != 1:
        raise ValueError('not an RNC method-1 block')
    size, packed = struct.unpack_from('>II', data, at + 4)
    bits = Bits(data, at + 18)
    bits.advance(2)
    out = bytearray()
    while len(out) < size:
        raw, dist, lens = table(bits), table(bits), table(bits)
        count = bits.read(16)
        while True:
            n = decode(bits, raw)
            if n:
                out += data[bits.p:bits.p + n]
                bits.p += n
                bits.fix()
            count -= 1
            if count <= 0:
                break
            back = decode(bits, dist) + 1
            n = decode(bits, lens) + 2
            for _ in range(n):
                out.append(out[-back])
    return bytes(out[:size]), packed + 18


def blocks(data):
    at = data.find(b'RNC\x01')
    while at >= 0:
        try:
            out, used = unpack(data, at)
            yield at, out
            at = data.find(b'RNC\x01', at + used)
        except (ValueError, IndexError):
            at = data.find(b'RNC\x01', at + 4)


if __name__ == '__main__':
    src, dst = Path(sys.argv[1]), Path(sys.argv[2])
    dst.mkdir(parents=True, exist_ok=True)
    for k, (at, out) in enumerate(blocks(src.read_bytes())):
        (dst / ('block-%02d.bin' % k)).write_bytes(out)
        print('%02d at %#x: %d bytes' % (k, at, len(out)))
