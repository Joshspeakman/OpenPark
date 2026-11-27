"""tools/disc_image.py on images built here (ISO 9660 plain and raw, FAT12 and FAT16): every file read back by its
path, names matched without case or ISO 9660's versions, directories that take more than a sector, and anything else
refused. No game files: the images hold made-up bytes."""
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import disc_image  # noqa: E402


def iso_image(files, mode=0):
    """An ISO 9660 image of {path: bytes}, names as recorded ('GAME/MAIN.EXE;1'), in 2048-byte sectors, or raw
    2352-byte ones (mode 1: Mode 1, user data at 16; mode 2: Mode 2 Form 1, at 24)."""
    dirs = {'': []}
    for path in sorted(files):
        parts = path.split('/')
        for i in range(1, len(parts)):
            d, parent = '/'.join(parts[:i]), '/'.join(parts[:i - 1])
            if d not in dirs:
                dirs[d] = []
                dirs[parent].append((parts[i - 1], d, True))
        dirs['/'.join(parts[:-1])].append((parts[-1], path, False))

    def size_of(name):
        return 33 + len(name) + (len(name) % 2 == 0)            # (records are an even length)

    def layout(entries):                                         # (records never cross a sector)
        at, offsets = 0, []
        for name in [b'\0', b'\1'] + [e[0].encode() for e in entries]:
            if at % 2048 + size_of(name) > 2048:
                at = -(-at // 2048) * 2048
            offsets.append((at, name))
            at += size_of(name)
        return offsets, -(-at // 2048) * 2048

    def record(name, lba, size, is_dir):
        r = bytearray(size_of(name))
        r[0] = len(r)
        r[2:18] = struct.pack('<I', lba) + struct.pack('>I', lba) + struct.pack('<I', size) + struct.pack('>I', size)
        r[25], r[28:32], r[32] = 2 if is_dir else 0, b'\1\0\0\1', len(name)
        r[33:33 + len(name)] = name
        return bytes(r)

    where, nxt = {}, 18
    for d, entries in dirs.items():
        size = layout(entries)[1]
        where[d] = (nxt, size)
        nxt += size // 2048
    for p in sorted(files):
        where[p] = (nxt, len(files[p]))
        nxt += max(1, -(-len(files[p]) // 2048))
    user = bytearray(nxt * 2048)
    for d, entries in dirs.items():
        targets = [(d, True), (d.rpartition('/')[0], True)] + [(e[1], e[2]) for e in entries]
        base = where[d][0] * 2048
        for (at, name), (target, is_dir) in zip(layout(entries)[0], targets):
            r = record(name, *where[target], is_dir)
            user[base + at:base + at + len(r)] = r
    for p, data in files.items():
        user[where[p][0] * 2048:where[p][0] * 2048 + len(data)] = data
    pvd = bytearray(2048)
    pvd[0:7] = b'\1CD001\1'
    pvd[128:132] = struct.pack('<H', 2048) + struct.pack('>H', 2048)
    pvd[156:190] = record(b'\0', *where[''], True)
    user[16 * 2048:17 * 2048] = pvd
    user[17 * 2048:17 * 2048 + 7] = b'\xffCD001\1'
    if not mode:
        return bytes(user)
    sub = b'' if mode == 1 else bytes(8)
    head = b'\0' + b'\xff' * 10 + b'\0' + bytes(3) + bytes([mode])
    return b''.join(head + sub + user[s * 2048:(s + 1) * 2048] + bytes(2352 - 16 - len(sub) - 2048) for s in range(nxt))


def fat_image(files, fat16=False, extras=True):
    """A FAT12 1.44 MB floppy (or a FAT16 disk) of {path: bytes}, 8.3 names; with a volume label, a long name's entry
    and a deleted file in the root (which a reader skips) unless extras is False."""
    bps, spc, reserved, fats = 512, 1, 1, 2
    sectors, root_entries, spf = (8400, 512, 33) if fat16 else (2880, 224, 9)
    root_at = (reserved + fats * spf) * bps
    data_at = root_at + root_entries * 32
    img = bytearray(sectors * bps)
    img[0:11] = b'\xeb\x3c\x90MSDOS5.0'
    struct.pack_into('<HBHBHHBH', img, 11, bps, spc, reserved, fats, root_entries, sectors, 0xf0, spf)
    img[510:512] = b'\x55\xaa'
    fat, nxt = bytearray(spf * bps), [2]

    def link(c, v):
        if fat16:
            struct.pack_into('<H', fat, c * 2, v)
        else:
            o = c + c // 2
            if c & 1:
                fat[o] = (fat[o] & 0x0f) | (v << 4 & 0xf0)
                fat[o + 1] = v >> 4 & 0xff
            else:
                fat[o] = v & 0xff
                fat[o + 1] = (fat[o + 1] & 0xf0) | (v >> 8 & 0x0f)

    def store(data):
        if not data:
            return 0
        n = -(-len(data) // bps)
        first = nxt[0]
        for k in range(n):
            link(first + k, first + k + 1 if k + 1 < n else (0xffff if fat16 else 0xfff))
            img[data_at + (first + k - 2) * bps:data_at + (first + k - 2) * bps + bps] = data[k * bps:(k + 1) * bps].ljust(bps, b'\0')
        nxt[0] += n
        return first

    def entry(name, attr, cluster, size):
        stem, _, ext = name.partition('.') if name not in ('.', '..') else (name, '', '')
        return stem.ljust(8).encode('latin-1') + ext.ljust(3).encode('latin-1') + bytes([attr]) + bytes(14) \
            + struct.pack('<HI', cluster, size)

    tree = {}
    for path, data in files.items():
        node = tree
        for part in path.split('/')[:-1]:
            node = node.setdefault(part, {})
        node[path.split('/')[-1]] = data

    def directory(node, own=None, parent=0):
        out = bytearray()
        if own is not None:
            out += entry('.', 0x10, own, 0) + entry('..', 0x10, parent, 0)
        for name, v in node.items():
            if isinstance(v, dict):
                n = -(-(len(v) + 2) * 32 // bps)
                first = store(bytes(n * bps))                    # (its clusters first: its own entries point to them)
                body = directory(v, first, own or 0)
                for k in range(n):
                    img[data_at + (first + k - 2) * bps:data_at + (first + k - 1) * bps] = body[k * bps:(k + 1) * bps].ljust(bps, b'\0')
                out += entry(name, 0x10, first, 0)
            else:
                out += entry(name, 0x20, store(v), len(v))
        return out

    root = bytearray()
    if extras:
        root += entry('TESTDISK', 0x08, 0, 0) + b'\x41' + bytes(10) + b'\x0f' + bytes(20) + b'\xe5' + entry('GONE.TXT', 0x20, 0, 5)[1:]
    root += directory(tree)
    img[root_at:root_at + len(root)] = root
    if fat16:
        fat[0:4] = b'\xf0\xff\xff\xff'                                   # (the media byte and the end marks)
    else:
        fat[0:3] = b'\xf0\xff\xff'
    for k in range(fats):
        img[(reserved + k * spf) * bps:(reserved + (k + 1) * spf) * bps] = fat
    return bytes(img)


def payload(n, seed):
    return bytes((i * 7 + seed * 13 + (i >> 8)) & 0xff for i in range(n))


class DiscImageTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def image(self, data, name='disc.iso'):
        p = Path(self.tmp.name) / name
        p.write_bytes(data)
        img = disc_image.open_image(p)
        self.assertIsNotNone(img)
        self.addCleanup(img.close)
        return img

    def check_iso(self, mode):
        files = {'GAME/MAIN.EXE;1': payload(5000, 1), 'MAIN.;1': payload(10, 2), 'Park;1': payload(2048, 3),
                 'SETUP.BAT;1': b''}
        files.update({'DATA/F%02d.DAT;1' % i: payload(i + 1, i) for i in range(80)})       # (two sectors of records)
        img = self.image(iso_image(files, mode))
        self.assertIsInstance(img, disc_image.Iso9660)
        self.assertEqual(img.read('GAME/MAIN.EXE'), files['GAME/MAIN.EXE;1'])
        self.assertEqual(img.read('game/main.exe'), files['GAME/MAIN.EXE;1'])
        self.assertEqual(img.read('main'), files['MAIN.;1'])                 # (the CD32's key file, as some discs name it)
        self.assertEqual(img.read('PARK'), files['Park;1'])
        self.assertEqual(img.read('setup.bat'), b'')
        self.assertEqual(img.read('DATA/F79.DAT'), files['DATA/F79.DAT;1'])
        self.assertIsNone(img.read('GAME/NOPE.EXE'))
        self.assertIsNone(img.read('GAME'))                                   # (a directory isn't a file)
        self.assertIsNone(img.read('MAIN/X'))
        self.assertEqual(sorted(img.names()), sorted(p.split(';')[0].rstrip('.') for p in files))

    def test_iso(self):
        self.check_iso(0)

    def test_raw_mode1(self):
        self.check_iso(1)

    def test_raw_mode2_form1(self):
        self.check_iso(2)

    def check_fat(self, fat16):
        files = {'GAME.EXE': payload(1500, 4), 'README.TXT': b'hello', 'GAME/MAIN.EXE': payload(3000, 5),
                 'GAME/DATA/X.DAT': payload(512, 6), 'EMPTY': b''}
        img = self.image(fat_image(files, fat16), 'floppy.img')
        self.assertIsInstance(img, disc_image.Fat)
        self.assertEqual(img.fat12, not fat16)
        for path, data in files.items():
            self.assertEqual(img.read(path), data, path)
            self.assertEqual(img.read(path.lower()), data, path)
        self.assertIsNone(img.read('GONE.TXT'))                               # (deleted)
        self.assertIsNone(img.read('TESTDISK'))                               # (the volume label)
        self.assertEqual(sorted(img.names()), sorted(files))

    def test_fat12(self):
        self.check_fat(False)

    def test_fat16(self):
        self.check_fat(True)

    def test_not_an_image(self):
        for data in (b'', bytes(100000), payload(400000, 9)):
            p = Path(self.tmp.name) / 'x.img'
            p.write_bytes(data)
            self.assertIsNone(disc_image.open_image(p))

    def test_closes(self):
        p = Path(self.tmp.name) / 'disc.iso'
        p.write_bytes(iso_image({'A.TXT;1': b'a'}))
        with disc_image.open_image(p) as img:
            self.assertEqual(img.read('a.txt'), b'a')
        os.remove(p)                                                          # (Windows: only once it is closed)
        p.write_bytes(b'x' * 50000)
        self.assertIsNone(disc_image.open_image(p))
        os.remove(p)


if __name__ == '__main__':
    unittest.main()
