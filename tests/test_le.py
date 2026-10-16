import struct
import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from le import LE, LEError


def fixture(record=b'\x07\x10\x04\x00\x01\x20\x00\x00\x00'):
    # Synthetic image only: a short final physical page and a zero-filled tail.
    d = bytearray(0x420)
    d[:2] = b'MZ'
    struct.pack_into('<I', d, 0x3c, 0x80)
    d[0x80:0x84] = b'LE\0\0'
    struct.pack_into('<H', d, 0x88, 2)
    values = {0x14: 1, 0x18: 1, 0x1c: 0, 0x20: 1, 0x24: 0x1000,
              0x28: 4096, 0x2c: 32, 0x40: 0xc4, 0x44: 1, 0x48: 0xdc,
              0x68: 0xe0, 0x6c: 0xe8, 0x80: 0x400}
    for off, val in values.items():
        struct.pack_into('<I', d, 0x80 + off, val)
    struct.pack_into('<6I', d, 0x144, 4096, 0x10000, 0x2047, 1, 1, 0)
    d[0x15c:0x160] = b'\x00\x00\x01\x00'
    struct.pack_into('<2I', d, 0x160, 0, len(record))
    d[0x168:0x168 + len(record)] = record
    d[0x400] = 0xc3
    return d


class LEReadTests(unittest.TestCase):
    def test_page_load_bss_and_relocation(self):
        image = LE(fixture())
        self.assertEqual(image.entry, 0x10000)
        self.assertEqual(image.stack, 0x11000)
        self.assertEqual(image.bytes_at(0x10020, 64), bytes(64))
        self.assertEqual(image.apply_offset_fixups(), [])
        self.assertEqual(image.bytes_at(0x10004, 4), struct.pack('<I', 0x10020))

    def test_negative_bias_is_address_arithmetic(self):
        image = LE(fixture(b'\x07\x10\x04\x00\x01\xfc\xff\xff\xff'))
        image.apply_offset_fixups()
        self.assertEqual(image.bytes_at(0x10004, 4), struct.pack('<I', 0xfffc))

    def test_relative_relocation(self):
        image = LE(fixture(b'\x08\x00\x04\x00\x01\x20\x00'))
        image.apply_offset_fixups()
        self.assertEqual(image.bytes_at(0x10004, 4), struct.pack('<I', 0x18))

    def test_source_list(self):
        image = LE(fixture(b'\x27\x00\x02\x01\x20\x00\x04\x00\x08\x00'))
        image.apply_offset_fixups()
        self.assertEqual(image.bytes_at(0x10004, 8), struct.pack('<2I', 0x10020, 0x10020))

    def test_selector_is_reported_pending(self):
        image = LE(fixture(b'\x02\x00\x04\x00\x01'))
        self.assertEqual(len(image.apply_offset_fixups()), 1)

    def test_bad_inputs_fail_closed(self):
        for offset, value in [(0x3c, 0xfffffff0), (0x80 + 0x44, 0xffffffff),
                              (0x80 + 0x80, 0xfffffff0), (0x144, 0x40000001),
                              (0x144 + 12, 2), (0x164, 1)]:
            d = fixture()
            struct.pack_into('<I', d, offset, value)
            with self.subTest(offset=offset), self.assertRaises(LEError):
                LE(d)
        for record in [b'\x07\x00', b'\x07\x01\x00\x00\x01\x00\x00',
                       b'\x07\x00\xff\xff\x01\x00\x00',
                       b'\x07\x00\x04\x00\x00\x00\x00']:
            with self.subTest(record=record), self.assertRaises(LEError):
                LE(fixture(record))

    def test_all_truncated_prefixes_fail(self):
        data = fixture()
        for size in range(len(data)):
            with self.subTest(size=size), self.assertRaises(LEError):
                LE(data[:size])

if __name__ == '__main__':
    unittest.main()
