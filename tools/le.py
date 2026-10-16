"""Bounded LE reader for DOS/4GW images; no installer or guest code is executed.

Format reference: Open Watcom bld/watcom/h/exeflat.h. LX, iterated pages,
imports, chained/additive fixups and selector assignment deliberately fail closed.
"""
from dataclasses import dataclass
from pathlib import Path
import struct


class LEError(ValueError):
    pass


@dataclass
class Object:
    size: int
    base: int
    flags: int
    page_index: int
    page_count: int
    data: bytearray


@dataclass(frozen=True)
class Fixup:
    object_index: int
    source: int
    kind: int
    target_object: int
    target_offset: int


class LE:
    MAX_IMAGE = 64 * 1024 * 1024

    def __init__(self, data):
        self.data = bytes(data)
        self.take(0, 64)
        if self.data[:2] != b'MZ':
            raise LEError('expected MZ stub')
        self.header = self.u32(0x3c)
        self.take(self.header, 0xc4)
        if self.take(self.header, 4) != b'LE\0\0':
            raise LEError('expected little-endian LE header, not PE/LX')
        h = lambda o: self.u32(self.header + o)
        if h(4) != 0 or self.u16(self.header + 8) not in (2, 3):
            raise LEError('unsupported LE version or CPU')
        self.page_count, self.page_size, self.last_page = h(0x14), h(0x28), h(0x2c)
        if self.page_size != 4096 or not 0 < self.page_count <= self.MAX_IMAGE // 4096:
            raise LEError('unsupported page size or count')
        if self.last_page > self.page_size:
            raise LEError('invalid last-page size')
        if h(0x74):
            raise LEError('imported modules require a separate loader')
        count = h(0x44)
        if not 0 < count <= 256:
            raise LEError('invalid object count')
        table = self.header + h(0x40)
        self.take(table, count * 24)
        page_map = self.header + h(0x48)
        self.take(page_map, self.page_count * 4)
        self.objects = []
        self.page_owner = {}
        for index in range(count):
            size, base, flags, first, pages, reserved = struct.unpack_from('<6I', self.data, table + 24 * index)
            if not size or size > self.MAX_IMAGE or base + size > 0x100000000:
                raise LEError('invalid object extent')
            if pages > (size + 4095) // 4096 or pages and (first < 1 or first + pages - 1 > self.page_count):
                raise LEError('object page range exceeds image')
            if sum(o.size for o in self.objects) + size > self.MAX_IMAGE:
                raise LEError('image allocation limit')
            if any(base < o.base + o.size and o.base < base + size for o in self.objects):
                raise LEError('overlapping objects')
            obj = Object(size, base, flags, first, pages, bytearray(size))
            for local in range(pages):
                logical = first - 1 + local
                if logical in self.page_owner:
                    raise LEError('overlapping page ownership')
                self.page_owner[logical] = (index, local)
                entry = self.take(page_map + 4 * logical, 4)
                number, kind = int.from_bytes(entry[:3], 'big'), entry[3]
                if kind == 3:  # explicit zero page
                    continue
                if kind != 0 or not 1 <= number <= self.page_count:
                    raise LEError('unsupported or invalid page map entry')
                length = (self.last_page or self.page_size) if number == self.page_count else self.page_size
                payload = self.take(h(0x80) + (number - 1) * self.page_size, length)
                amount = min(length, size - local * self.page_size)
                obj.data[local * self.page_size:local * self.page_size + amount] = payload[:amount]
            self.objects.append(obj)
        self.entry_object, self.entry_offset = h(0x18), h(0x1c)
        self.stack_object, self.stack_offset = h(0x20), h(0x24)
        self.entry = self.object_address(self.entry_object, self.entry_offset)
        self.stack = self.object_address(self.stack_object, self.stack_offset, end_ok=True)
        if not self.objects[self.entry_object - 1].flags & 4:
            raise LEError('entry object is not executable')
        self.fixups = []
        fixpage, fixrec = self.header + h(0x68), self.header + h(0x6c)
        self.take(fixpage, (self.page_count + 1) * 4)
        offsets = [self.u32(fixpage + i * 4) for i in range(self.page_count + 1)]
        if offsets[0] != 0 or offsets != sorted(offsets):
            raise LEError('invalid fixup page offsets')
        self.take(fixrec, offsets[-1])
        for page in range(self.page_count):
            pos, end = fixrec + offsets[page], fixrec + offsets[page + 1]
            def read(n, signed=False):
                nonlocal pos
                if n > end - pos:
                    raise LEError('truncated fixup record')
                value = int.from_bytes(self.take(pos, n), 'little', signed=signed)
                pos += n
                return value
            while pos < end:
                source_flags, target_flags = read(1), read(1)
                kind = source_flags & 15
                if source_flags & ~0x2f or target_flags & ~0x50:
                    raise LEError('unsupported alias, external, additive or chained fixup')
                width = {0: 1, 2: 2, 3: 4, 5: 2, 6: 6, 7: 4, 8: 4}.get(kind)
                if width is None:
                    raise LEError('unsupported source fixup type')
                listed = bool(source_flags & 0x20)
                source = read(1) if listed else read(2, signed=True)
                target = read(2 if target_flags & 0x40 else 1)
                offset = 0 if kind == 2 else read(4 if target_flags & 0x10 else 2)
                if not 1 <= target <= len(self.objects):
                    raise LEError("invalid fixup target object")
                sources = [read(2, signed=True) for _ in range(source)] if listed else [source]
                if page not in self.page_owner:
                    raise LEError('fixup on unowned page')
                owner, local = self.page_owner[page]
                for loc in sources:
                    at = local * self.page_size + loc
                    if at < 0 or at + width > self.objects[owner].size:
                        raise LEError('fixup source outside object')
                    self.fixups.append(Fixup(owner, at, kind, target, offset))

    @classmethod
    def read(cls, path):
        return cls(Path(path).read_bytes())

    def take(self, offset, size):
        if offset < 0 or size < 0 or offset > len(self.data) or size > len(self.data) - offset:
            raise LEError('truncated/out-of-range LE data')
        return self.data[offset:offset + size]

    def u16(self, offset):
        return int.from_bytes(self.take(offset, 2), 'little')

    def u32(self, offset):
        return int.from_bytes(self.take(offset, 4), 'little')

    def object_address(self, index, offset, end_ok=False):
        if not 1 <= index <= len(self.objects):
            raise LEError('invalid object reference')
        obj = self.objects[index - 1]
        if offset < 0 or offset > obj.size or (offset == obj.size and not end_ok):
            raise LEError(f'offset {offset:#x} outside target object {index} (size {obj.size:#x})')
        return obj.base + offset

    def apply_offset_fixups(self):
        """Resolve flat 32-bit offsets; return selector/other fixups still pending.

        This is an analysis image, not a complete DOS loader. Callers must not
        silently interpret remaining selectors as Windows segment selectors.
        """
        pending = []
        for f in self.fixups:
            obj = self.objects[f.object_index]
            # Linker offsets may encode a negative bias as uint32, not a dereference.
            target = (self.objects[f.target_object - 1].base + f.target_offset) & 0xffffffff
            if f.kind not in (7, 8):
                pending.append(f)
                continue
            value = target if f.kind == 7 else target - (obj.base + f.source + 4)
            struct.pack_into('<I', obj.data, f.source, value & 0xffffffff)
        return pending

    def bytes_at(self, address, size):
        for obj in self.objects:
            if obj.base <= address < obj.base + obj.size:
                offset = address - obj.base
                return bytes(obj.data[offset:offset + size])
        return b''
