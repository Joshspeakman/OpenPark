#!/usr/bin/env python3
"""List or extract a 3DO disc's Opera filesystem from a bin/cue data track (2352-byte sectors).

The volume header (sector 0: type 1, five 0x5a sync bytes) gives the root directory's blocks; each directory
block lists entries (flags, id, type, block size, byte count, block count, burst, gap, name[32], avatars:
copies of the file's first block). Written from the format's public description. Output is game-derived
and stays in work/.

  tools/opera.py DISC.bin [OUTDIR]
"""
import os
import struct
import sys

RAW = 2352


class Disc:
    def __init__(self, path):
        self.f = open(path, 'rb')
        v = self.sector(0)
        if v[0] != 1 or v[1:6] != b'\x5a' * 5:
            raise ValueError('not a 3DO Opera volume')
        self.label = v[0x28:0x48].split(b'\0')[0].decode('latin1')
        _, _, _, root_blocks, _, last = struct.unpack_from('>IIIIII', v, 0x4c)
        copies = struct.unpack_from('>%dI' % (last + 1), v, 0x64)
        self.files = {}
        self._walk(copies[0], root_blocks, '')

    def sector(self, n):
        self.f.seek(n * RAW + 16)
        return self.f.read(2048)

    def _walk(self, start, nblocks, path):
        for b in range(nblocks):
            blk = self.sector(start + b)
            _, _, _, first_free, first_entry = struct.unpack_from('>iiIII', blk, 0)
            off = first_entry
            while off < first_free:
                flags, _, kind, _, nbytes, blocks, _, _ = struct.unpack_from('>II4sIIIII', blk, off)
                name = blk[off + 32:off + 64].split(b'\0')[0].decode('latin1')
                navat, = struct.unpack_from('>I', blk, off + 64)
                avat = struct.unpack_from('>%dI' % (navat + 1), blk, off + 68)
                off += 68 + 4 * (navat + 1)
                p = path + '/' + name
                if flags & 0xff == 7:
                    self._walk(avat[0], blocks, p)
                else:
                    self.files[p.lstrip('/')] = (avat[0], nbytes, kind)

    def read(self, name):
        start, n, _ = self.files[name]
        return b''.join(self.sector(start + i) for i in range((n + 2047) // 2048))[:n]


def sdx2(data, channels=1, state=None):
    """3DO SDX2 (square-root-delta-exact) bytes -> 16-bit little-endian PCM."""
    prev = state if state is not None else [0] * channels
    out = bytearray()
    for k, b in enumerate(data):
        c = k % channels
        s = b - 256 if b > 127 else b
        sq = s * abs(s) * 2
        x = prev[c] + sq if b & 1 else sq
        x = -32768 if x < -32768 else 32767 if x > 32767 else x
        prev[c] = x
        out += struct.pack('<h', x)
    return bytes(out), prev


def aifc_pcm(data):
    """(PCM bytes, channels, rate) from a 3DO AIFC (SDX2) file."""
    i, ch, rate, ssnd = 12, 1, 44100, b''
    while i + 8 <= len(data):
        cid, n = data[i:i + 4], int.from_bytes(data[i + 4:i + 8], 'big')
        if cid == b'COMM':
            ch = struct.unpack_from('>H', data, i + 8)[0]
            e = ((data[i + 16] & 0x7f) << 8) | data[i + 17]
            rate = round(int.from_bytes(data[i + 18:i + 26], 'big') * 2.0 ** (e - 16383 - 63))
        elif cid == b'SSND':
            off = int.from_bytes(data[i + 8:i + 12], 'big')
            ssnd = data[i + 16 + off:i + 8 + n]
        i += 8 + n + (n & 1)
    return sdx2(ssnd, ch)[0], ch, rate


def stream_audio(data):
    """(PCM bytes, channels, rate) of a 3DO DataStream's SNDS chunks (SDX2)."""
    i, ch, rate, parts, state = 0, 1, 44100, [], None
    while i + 8 <= len(data):
        kind, size = data[i:i + 4], int.from_bytes(data[i + 4:i + 8], 'big')
        if size < 8 or size > 0x100000:
            i = (i // 32768 + 1) * 32768
            continue
        if kind == b'SNDS':
            sub = data[i + 16:i + 20]
            if sub == b'SHDR':
                rate = int.from_bytes(data[i + 44:i + 48], 'big')
                ch = int.from_bytes(data[i + 48:i + 52], 'big')
            elif sub == b'SSMP':
                n = int.from_bytes(data[i + 20:i + 24], 'big')
                pcm, state = sdx2(data[i + 24:i + 24 + n], ch, state)
                parts.append(pcm)
        i += size
    return b''.join(parts), ch, rate


def wav(pcm, channels, rate):
    return (b'RIFF' + struct.pack('<I', 36 + len(pcm)) + b'WAVEfmt ' + struct.pack('<IHHIIHH', 16, 1, channels, rate, rate * 2 * channels, 2 * channels, 16)
            + b'data' + struct.pack('<I', len(pcm)) + pcm)


if __name__ == '__main__':
    d = Disc(sys.argv[1])
    print('label %s, %d files' % (d.label, len(d.files)))
    for name, (start, n, kind) in sorted(d.files.items()):
        print('%10d %-4s %s' % (n, kind.decode('latin1'), name))
    if len(sys.argv) > 2:
        for name in d.files:
            dst = os.path.join(sys.argv[2], name)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            open(dst, 'wb').write(d.read(name))
