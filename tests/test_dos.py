import sys
import tempfile
import unittest
import uuid
from pathlib import Path

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from dos import Arena, DOS, Stop, REGS, SEGMENTS


class Machine:
    def __init__(self):
        self.mem = bytearray(1024*1024)
        self.regs = dict.fromkeys(REGS+SEGMENTS+('eip','eflags'),0)
        self.regs['ds'] = 16
    def get(self,n): return self.regs[n]
    def set(self,n,v): self.regs[n] = v
    def read(self,a,n): return bytes(self.mem[a:a+n])
    def write(self,a,d): self.mem[a:a+len(d)] = d


class ArenaTests(unittest.TestCase):
    def test_alignment_exhaustion_and_coalescing(self):
        a = Arena(0x1000,0x2000,16)
        x,y = a.allocate(17),a.allocate(3000)
        self.assertEqual((x,y),(0x1000,0x1020))
        self.assertIsNone(a.allocate(4096))
        a.release(x); a.release(y)
        self.assertEqual(a.free,[(0x1000,4096)])

    def test_resize_move_shrink_and_failed_transaction(self):
        a = Arena(0x1000,0x2000,16)
        x,y = a.allocate(64),a.allocate(64)
        moved = a.resize(x,256)
        self.assertEqual(moved,0x1080)
        snapshot = (a.free[:],a.used.copy())
        self.assertIsNone(a.resize(y,8192))
        self.assertEqual((a.free,a.used),snapshot)
        self.assertEqual(a.resize(moved,32),moved)
        a.release(moved); a.release(y)
        self.assertEqual(a.largest(),4096)

    def test_grow_in_place_does_not_take_earlier_hole(self):
        a = Arena(0x1000,0x2000,16)
        early = a.allocate(512); middle = a.allocate(16); last = a.allocate(64)
        a.release(early)
        self.assertEqual(a.resize(last,128),last)
        self.assertEqual(a.used[last],128)


class DOSTests(unittest.TestCase):
    def setUp(self):
        root=Path(tempfile.mkdtemp(prefix='theme-park-tests-'))
        trash=Path.home()/'.Trash'
        trash.mkdir(exist_ok=True)
        self.addCleanup(lambda:root.rename(trash/(root.name+'-'+uuid.uuid4().hex)))
        self.game=root/'game';self.game.mkdir()
        (self.game/'Data').mkdir()
        (self.game/'Data'/'Mixed.DAT').write_bytes(b'abcdef')
        self.m=Machine();self.dos=DOS(self.m,self.game,root/'scratch')
        self.addCleanup(lambda: [f.close() for f in self.dos.handles.values()])

    def call(self,ax,**regs):
        self.m.set('eax',ax)
        for n,v in regs.items(): self.m.set(n,v)
        self.dos.interrupt(0x21)

    def open(self,mode=0):
        self.m.write(0x10000,b'data\\mixed.dat\0')
        self.call(0x3d00|mode,edx=0x10000)
        return self.m.get('eax')

    def test_closed_handles_are_reused_beyond_crt_table_limit(self):
        for _ in range(100):
            self.assertEqual(self.open(),5)
            self.call(0x3e00,ebx=5)
        self.assertEqual(self.open(),5)
        self.assertEqual(self.open(),6)

    def test_seek_read_eof_and_bad_handle(self):
        h=self.open()
        self.call(0x4202,ebx=h,ecx=0,edx=0)
        self.assertEqual(self.m.get('eax'),6)
        self.call(0x4200,ebx=h,ecx=0,edx=2)
        self.call(0x3f00,ebx=h,ecx=8,edx=0x20000)
        self.assertEqual(self.m.get('eax'),4)
        self.assertEqual(self.m.read(0x20000,4),b'cdef')
        self.call(0x3f00,ebx=h,ecx=8,edx=0x20000)
        self.assertEqual(self.m.get('eax'),0)
        self.call(0x3f00,ebx=99,ecx=8,edx=0x20000)
        self.assertEqual(self.m.get('eax'),6)
        self.assertTrue(self.m.get('eflags')&1)

    def test_handle_uses_bx_only(self):
        # Watcom's stdio passes garbage in the upper half of EBX (seen while
        # reading sndsetup.inf); DOS reads the handle from BX.
        h=self.open()
        self.call(0x4400,ebx=0x1c0000|h)
        self.assertFalse(self.m.get('eflags')&1)
        self.call(0x3f00,ebx=0x1c0000|h,ecx=3,edx=0x20000)
        self.assertEqual((self.m.get('eax'),self.m.read(0x20000,3)),(3,b'abc'))
        self.call(0x3e00,ebx=0x1c0000|h)
        self.assertFalse(self.m.get('eflags')&1)

    def test_writable_open_copies_owned_file_to_scratch(self):
        h=self.open(2)
        self.dos.handles[h].write(b'XYZ');self.dos.handles[h].flush()
        self.assertEqual((self.game/'Data'/'Mixed.DAT').read_bytes(),b'abcdef')
        self.assertEqual(self.dos.path('DATA/MIXED.DAT').read_bytes(),b'XYZdef')

    def test_traversal_and_symlink_escape_are_rejected(self):
        for name in ('../outside','C:/GAME/../outside','data/../../outside'):
            with self.assertRaises(Stop):self.dos.path(name)
        (self.game/'escape').symlink_to(self.game.parent,target_is_directory=True)
        with self.assertRaises(Stop):self.dos.path('escape/outside')

    def test_scratch_cannot_overlap_owned_game_root(self):
        for scratch in (self.game,self.game/'scratch',self.game.parent):
            with self.assertRaises(ValueError):DOS(self.m,self.game,scratch)

    def test_unknown_service_is_explicit_failure(self):
        with self.assertRaisesRegex(Stop,'unsupported int'):
            self.call(0xdead)

    def test_create_write_truncate_stays_in_overlay(self):
        self.m.write(0x10000,b'DATA/Mixed.DAT\0')
        self.call(0x3c00,ecx=0,edx=0x10000)
        h=self.m.get('eax')
        self.m.write(0x20000,b'new data')
        self.call(0x4000,ebx=h,ecx=8,edx=0x20000)
        self.call(0x4200,ebx=h,ecx=0,edx=3)
        self.call(0x4000,ebx=h,ecx=0,edx=0x20000)
        self.call(0x3e00,ebx=h)
        self.assertEqual(self.dos.path('data/mixed.dat').read_bytes(),b'new')
        self.assertEqual((self.game/'Data'/'Mixed.DAT').read_bytes(),b'abcdef')


if __name__=='__main__':unittest.main()
