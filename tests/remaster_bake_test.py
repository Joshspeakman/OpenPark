"""Synthetic geometry verifies canonical part extraction, never game artwork."""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

BAKER=Path(__file__).resolve().parents[1]/'build/remaster_bake'
RIG="""TPRIG1 6 2 2 1 0
-.6 0 0 0
-.2 0 0 0
-.4 0 1 0
.2 0 0 1
.6 0 0 1
.4 0 1 1
0 1 2 ff0000
3 4 5 00ff00
10
1 0 0 0 0 1 0 0 0 0 1 0
1 0 0 0 0 1 0 0 0 0 1 0
"""


class BakerTest(unittest.TestCase):
    def test_parts_keep_shared_origin_and_combine_to_full_bake(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);model=root/'model.rig';model.write_text(RIG)
            outputs=[]
            for part in (None,'0','1'):
                output=root/f'{part}.bake'
                args=[str(BAKER),str(model),'10',str(output),'64','64','24','32','48','1.2']
                if part is not None:args.append(part)
                subprocess.run(args,check=True,capture_output=True)
                data=output.read_bytes()
                self.assertEqual(data[:16],b'TPBAKE01'+struct.pack('<II',64,64))
                outputs.append(list(struct.unpack('<4096I',data[16:])))
            all_parts,left,right=outputs
            self.assertIn(0xff0000ff,left)
            self.assertIn(0xff00ff00,right)
            self.assertTrue(all(not(a and b) for a,b in zip(left,right)))
            self.assertEqual(all_parts,[a or b for a,b in zip(left,right)])

    def test_invalid_part_does_not_export_a_bake(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);model=root/'model.rig';model.write_text(RIG);out=root/'invalid.bake'
            for part in ('-1','2','bogus','1suffix'):
                args=[str(BAKER),str(model),'10',str(out),'64','64','24','32','48','1.2',part]
                result=subprocess.run(args,capture_output=True)
                self.assertEqual(result.returncode,2)
                self.assertFalse(out.exists())


if __name__=='__main__':
    unittest.main()
