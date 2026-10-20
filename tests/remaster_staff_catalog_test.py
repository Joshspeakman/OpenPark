"""Source-free staff table fixtures: native IDs and direction groups."""
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from visitor_animation_catalog import staff_assets


class StaffCatalogTests(unittest.TestCase):
    def fixture(self):
        data = bytearray(10*54)
        for i in range(9):
            at = i*54
            name = f'WORKER_{i}'.encode()
            data[at:at+len(name)] = name
            for offset, value in ((0x22, i+1), (0x26, 20+i*12),
                                  (0x28, 24+i*12), (0x2a, 28+i*12)):
                struct.pack_into('<H', data, at+offset, value)
        return data

    def test_first_last_and_directional_references(self):
        records = staff_assets(self.fixture(), base=0)
        self.assertEqual(len(records), 9)
        self.assertEqual(records[0]['name'], 'WORKER_0')
        self.assertEqual(records[-1]['id'], 'staff:8')
        self.assertEqual(records[-1]['name'], 'WORKER_8')
        for i, record in enumerate(records):
            self.assertEqual(record['native_variant'], i)
            refs = {r['field']: r['animation'] for r in record['references']}
            self.assertEqual(refs['+0x2a'], 28+i*12)
            self.assertEqual([refs[f'direction:0x26:{d}'] for d in range(4)],
                             list(range(20+i*12, 24+i*12)))
            self.assertEqual([refs[f'direction:0x28:{d}'] for d in range(4)],
                             list(range(24+i*12, 28+i*12)))

    def test_sentinel_is_definition_field_not_name(self):
        data = self.fixture()
        data[9*54:9*54+7] = b'IGNORED'
        self.assertEqual(len(staff_assets(data, base=0)), 9)
        struct.pack_into('<H', data, 0x22, 0)
        self.assertEqual(staff_assets(data, base=0), [])

    def test_missing_terminator_and_bounds(self):
        with self.assertRaises(ValueError):
            staff_assets(self.fixture()[:9*54], base=0)
        with self.assertRaises(ValueError):
            staff_assets(self.fixture(), base=0, limit=9)
        with self.assertRaises(ValueError):
            staff_assets(self.fixture(), base=-1)
        with self.assertRaises(ValueError):
            staff_assets(self.fixture(), base=0, limit=65)


if __name__ == '__main__':
    unittest.main()
