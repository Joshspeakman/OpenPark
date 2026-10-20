"""Synthetic full-colour format tests; no game-derived artwork."""
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from remaster_pack import encode_rgba, palette_expansion, quantize

class RGBAPackTest(unittest.TestCase):
    def setUp(self):
        self.record=dict(bank=0,index=1,width=2,height=1,source_hash='0123456789abcdef',
                         display_bounds=dict(x=-1,y=-1,width=3,height=2))
        self.rows=[[None,1,2],[3,4,5]]
        self.rgba=bytes([13,27,39,255,17,29,41,128])*12
        self.bake=(6,4,self.rgba)

    def test_raw_colour_density_and_preview_are_independent(self):
        raw=encode_rgba([(self.record,self.rows,self.bake)])
        self.assertEqual(raw[:12],b'TPRART03'+struct.pack('<I',1))
        self.assertEqual(struct.unpack_from('<HHHHQhhHHHH',raw,12),
                         (0,1,2,1,0x0123456789abcdef,-1,-1,3,2,2,0))
        self.assertEqual(struct.unpack_from('<6H',raw,40),(256,1,2,3,4,5))
        self.assertEqual(raw[52:],self.rgba)

    def test_invalid_density_and_lengths(self):
        for bake in ((6,3,bytes(72)),(27,18,bytes(1944)),(6,4,bytes(95)),(0,0,b'')):
            with self.subTest(bake=bake[:2]),self.assertRaises(ValueError):
                encode_rgba([(self.record,self.rows,bake)])

    def test_binding_sort_and_preview_validation(self):
        item=(self.record,self.rows,self.bake)
        with self.assertRaises(ValueError):encode_rgba([item,item])
        with self.assertRaises(ValueError):encode_rgba([(self.record,[[257]*3]*2,self.bake)])
        with self.assertRaises(ValueError):encode_rgba([(dict(self.record,bank=2),self.rows,self.bake)])

    def test_unchanged_pixel_expansion_is_detected(self):
        palette=bytes([0,0,0,63,0,0]+[0]*762)
        rgba=bytes([255,0,0,255]*4)
        self.assertTrue(palette_expansion((2,2,rgba),[[1]],palette))
        changed=bytearray(rgba);changed[:4]=bytes([255,127,0,255])
        # The centre sample still quantizes to the same native palette index.
        self.assertEqual(quantize((2,2,changed),1,1,palette),[[1]])
        self.assertFalse(palette_expansion((2,2,changed),[[1]],palette))
        self.assertTrue(palette_expansion((2,2,bytes(16)),[[None]],palette))

if __name__=='__main__':unittest.main()
