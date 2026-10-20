"""Synthetic table edge cases. No owned game material in tests."""
import struct,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from visitor_animation_catalog import graph
class GraphTest(unittest.TestCase):
 def test_chains_and_seat_markers(self):
  starts=struct.pack('<HHH',1,3,9)
  frames=b''.join(struct.pack('<HBBBBH',1,20,30,0,0,n) for n in [0,2,1,0,9])
  elems=b'\0'*10+struct.pack('<HhhHH',18,-4,-9,128,0)
  g=graph(starts,frames,elems)
  self.assertEqual(g['chains'][0]['frames'],[1,2]);self.assertEqual(g['chains'][0]['loop_start'],0)
  self.assertEqual(g['chains'][1]['termination'],'end');self.assertEqual(g['chains'][2]['termination'],'invalid')
  self.assertTrue(g['frames'][1]['parts'][0]['rider_marker']);self.assertEqual(g['frames'][1]['parts'][0]['sprite_index'],3)
  self.assertEqual(g['unreachable_frames'],[4])
 def test_invalid_alignment(self):
  with self.assertRaises(ValueError):graph(b'1',b'',b'')
 def test_element_cycle(self):
  g=graph(struct.pack('<H',1),b'\0'*8+struct.pack('<HBBBBH',1,0,0,0,0,0),b'\0'*10+struct.pack('<HhhHH',6,0,0,0,1))
  self.assertEqual(g['frames'][1]['element_termination'],'cycle')
if __name__=='__main__':unittest.main()
