"""Synthetic track references, ownership and malformed-table checks."""
import copy
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from remaster_tracks import track_families, attach_track_references


class Tracks(unittest.TestCase):
    def fixture(self):
        data = bytearray(512)
        for family in range(1, 4):
            pointer = 100+family*50
            struct.pack_into('<HIHH', data, family*10, 20*family, pointer, 9, 7)
            for shape in range(2):
                at = pointer+shape*25
                struct.pack_into('<HH', data, at, 3, 4)
                struct.pack_into('<HHHH', data, at+5, 30, 0, 32, 33)
                data[at+17] = 6
                struct.pack_into('<bbb', data, at+18, -48, 24, -16)
        return data

    def test_records_and_signed_fields(self):
        families = track_families(self.fixture(), table=0, counts=(2, 2, 2))
        self.assertEqual([f['family'] for f in families], [1, 2, 3])
        self.assertEqual(families[1]['unknown_header_words'], [9, 7])
        segment = families[1]['segments'][1]
        self.assertEqual(segment['slope_fields'], [-48, 24, -16])
        self.assertEqual(segment['connection_mask'], 6)
        self.assertEqual(len(bytes.fromhex(segment['raw_record'])), 25)
        self.assertEqual({r['field']: r['animation'] for r in segment['references']},
                         dict(base=3, stack=4, cap=41, **{'car_direction:0': 30,
                              'car_direction:2': 32, 'car_direction:3': 33}))

    def test_bounds(self):
        data = self.fixture()
        for table, counts in [(-1, (2, 2, 2)), (500, (2, 2, 2)),
                              (0, (0, 2, 2)), (0, (257, 2, 2)), (0, (2, 2))]:
            with self.subTest(table=table, counts=counts), self.assertRaises(ValueError):
                track_families(data, table, counts)
        for pointer in (0, 490, 0xffffffff):
            bad = bytearray(data);struct.pack_into('<I', bad, 12, pointer)
            with self.subTest(pointer=pointer), self.assertRaises(ValueError):
                track_families(bad, 0, (2, 2, 2))
        struct.pack_into('<H', data, 10, 65535)
        with self.assertRaises(ValueError):
            track_families(data, 0, (2, 2, 2))

    def test_owner_and_ground_family(self):
        families = track_families(self.fixture(), 0, (2, 2, 2))
        memory = bytearray(0x1d0000)
        assets = [dict(id=f'ride:{i}', category='ride', references=[])
                  for i in range(5)]
        assets.append(dict(id='shop:0', category='shop', references=[]))
        for i, family in enumerate((0, 1, 2, 3, -1)):
            struct.pack_into('<b', memory, 0x1cced0+i*200+0xb4, family)
        before = copy.deepcopy(families)
        attach_track_references(assets, memory, families)
        self.assertEqual(families, before)
        for i in (0, 4, 5):
            self.assertEqual(assets[i]['references'], [])
            self.assertNotIn('track_family', assets[i])
        for i in (1, 2, 3):
            self.assertEqual(assets[i]['track_family'], i)
            self.assertTrue(assets[i]['composite_required'])
            self.assertEqual(len(assets[i]['references']), {1: 13, 2: 12, 3: 11}[i])
            self.assertTrue(all(r['field'].startswith(f'track:{i}:')
                                for r in assets[i]['references']))


if __name__ == '__main__':
    unittest.main()
