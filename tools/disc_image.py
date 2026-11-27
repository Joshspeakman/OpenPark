#!/usr/bin/env python3
"""Files on a disc or floppy image, read in place without mounting it: tools/extras.py off the Mac (where hdiutil mounts
images). Standard library only.

- ISO 9660 CD images: 2048-byte sectors (.iso), or raw 2352-byte ones (.img/.bin: Mode 1, and Mode 2 Form 1), from the
  primary volume descriptor. Names are matched without case, their ;1 versions and trailing dots dropped. Directory
  records never cross a sector (the rest of one is zeros). Not read: Joliet or Rock Ridge names (the 8.3 names are
  there too), multi-extent and interleaved files.
- FAT12 and FAT16 floppy and disk images (a boot sector with its BIOS parameter block at the start; no partition table).
  Long names are not read (the 8.3 names are there too). Not read: FAT32.

  open_image(path): an image with read(path) (a file's bytes, the path /-separated; None if it isn't there) and names()
  (every file's path), or None when it is neither. Close it (or use `with`).
  disc_image.py IMAGE [PATH]: list its files, or write one to stdout.
"""
import struct
import sys

LAYOUTS = ((2048, 0), (2352, 16), (2352, 24))      # ISO 9660: (sector size, user data at): plain, raw Mode 1, Mode 2 Form 1


class _Image:
    """A tree of (name, is_dir, location, size) entries; subclasses give the root, entries() and content()."""
    root = None

    def __init__(self, f):
        self.f = f

    def close(self):
        self.f.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def find(self, path):
        node = self.root
        for part in [p for p in path.replace('\\', '/').split('/') if p]:
            if not node[0]:
                return None
            match = [e for e in self.entries(node) if e[0].lower() == part.lower()]
            if not match:
                return None
            node = match[0][1:]
        return node

    def read(self, path):
        node = self.find(path)
        return None if node is None or node[0] else self.content(node)

    def names(self, node=None, prefix='', seen=None):
        seen = seen if seen is not None else set()
        node = node or self.root
        if node[1] in seen:                              # (a damaged image's loops)
            return
        seen.add(node[1])
        for name, is_dir, at, size in self.entries(node):
            if is_dir:
                yield from self.names((True, at, size), prefix + name + '/', seen)
            else:
                yield prefix + name


class Iso9660(_Image):
    def __init__(self, f, sector, user):
        super().__init__(f)
        self.sector, self.user = sector, user
        pvd = b''
        for block in range(16, 32):                      # (the volume descriptors, to the terminator: the primary's)
            d = self.data(block, 2048)
            if d[1:6] != b'CD001' or d[0] == 255:
                break
            if d[0] == 1:
                pvd = d
                break
        if not pvd or struct.unpack_from('<H', pvd, 128)[0] != 2048:
            raise ValueError('not an ISO 9660 image this reads')
        self.root = (True, struct.unpack_from('<I', pvd, 156 + 2)[0], struct.unpack_from('<I', pvd, 156 + 10)[0])

    def data(self, block, length):
        if self.sector == 2048:
            self.f.seek(block * 2048)
            return self.f.read(length)
        out = bytearray()
        while len(out) < length:
            self.f.seek(block * self.sector + self.user)
            part = self.f.read(2048)
            if not part:
                break
            out += part
            block += 1
        return bytes(out[:length])

    def entries(self, node):
        d, at, out = self.data(node[1], node[2]), 0, []
        while at < len(d):
            n = d[at]
            if n == 0:                                   # (the sector's end: the next record starts the next sector)
                at = (at // 2048 + 1) * 2048
                continue
            if n < 34 or at + n > len(d):
                break
            ident, flags = d[at + 33:at + 33 + d[at + 32]], d[at + 25]
            if ident not in (b'\0', b'\1') and not flags & 4:          # (not itself, its parent, or a Mac resource fork)
                name = ident.decode('latin-1').split(';')[0].rstrip('.')
                out.append((name, bool(flags & 2), struct.unpack_from('<I', d, at + 2)[0], struct.unpack_from('<I', d, at + 10)[0]))
            at += n
        return out

    def content(self, node):
        return self.data(node[1], node[2])


class Fat(_Image):
    def __init__(self, f):
        super().__init__(f)
        f.seek(0)
        b = f.read(512)
        if len(b) < 512 or b[0] not in (0xeb, 0xe9):
            raise ValueError('not a FAT boot sector')
        bps, spc, reserved, fats, root_entries, total, _, spf = struct.unpack_from('<HBHBHHBH', b, 11)
        total = total or struct.unpack_from('<I', b, 32)[0]
        if bps not in (512, 1024, 2048, 4096) or spc not in (1, 2, 4, 8, 16, 32, 64, 128) or fats not in (1, 2) \
                or not reserved or not root_entries or not spf:
            raise ValueError('not a FAT12 or FAT16 boot sector')
        root_sectors = (root_entries * 32 + bps - 1) // bps
        clusters = (total - reserved - fats * spf - root_sectors) // spc
        if not 0 < clusters < 65525:
            raise ValueError('not FAT12 or FAT16')
        self.fat12, self.cluster = clusters < 4085, bps * spc
        f.seek(reserved * bps)
        self.fat = f.read(spf * bps)
        self.root_at = (reserved + fats * spf) * bps
        self.data_at = self.root_at + root_sectors * bps
        self.root = (True, None, root_entries * 32)

    def _next(self, c):
        if self.fat12:
            v = struct.unpack_from('<H', self.fat, c + c // 2)[0]
            v = v >> 4 if c & 1 else v & 0xfff
            return v if 2 <= v < 0xff7 else None
        v = struct.unpack_from('<H', self.fat, c * 2)[0]
        return v if 2 <= v < 0xfff7 else None

    def _chain(self, c, size=None):
        out, seen = bytearray(), set()
        while c is not None and c >= 2 and c not in seen and (size is None or len(out) < size):
            seen.add(c)
            self.f.seek(self.data_at + (c - 2) * self.cluster)
            out += self.f.read(self.cluster)
            try:
                c = self._next(c)
            except struct.error:
                break
        return bytes(out if size is None else out[:size])

    def entries(self, node):
        if node[1] is None:
            self.f.seek(self.root_at)
            d = self.f.read(node[2])
        else:
            d = self._chain(node[1])
        out = []
        for at in range(0, len(d) - 31, 32):
            e = d[at:at + 32]
            if e[0] == 0:
                break
            if e[0] == 0xe5 or e[11] & 0x08:              # (deleted; a volume label or a long name's part)
                continue
            name, ext = e[:8].decode('latin-1').rstrip(), e[8:11].decode('latin-1').rstrip()
            if e[0] == 0x05:
                name = '\xe5' + name[1:]
            if name in ('', '.', '..'):
                continue
            out.append((name + ('.' + ext if ext else ''), bool(e[11] & 0x10), struct.unpack_from('<H', e, 26)[0],
                        struct.unpack_from('<I', e, 28)[0]))
        return out

    def content(self, node):
        return self._chain(node[1], node[2]) if node[1] else b''


def open_image(path):
    f = open(path, 'rb')
    for sector, user in LAYOUTS:
        try:
            return Iso9660(f, sector, user)
        except (ValueError, struct.error):
            pass
    try:
        return Fat(f)
    except (ValueError, struct.error):
        f.close()
        return None


if __name__ == '__main__':
    img = open_image(sys.argv[1])
    if img is None:
        sys.exit('%s: not an ISO 9660 or FAT image' % sys.argv[1])
    with img:
        if len(sys.argv) > 2:
            data = img.read(sys.argv[2])
            if data is None:
                sys.exit('%s: no %s' % (sys.argv[1], sys.argv[2]))
            sys.stdout.buffer.write(data)
        else:
            for name in img.names():
                print(name)
