"""tools/extras.py off the Mac: the data folder each system's app reads, a 3DO disc's import with its data track kept in
memory (Windows can't reopen an open temporary file by name), and .iso/.img images read in place (Windows and Linux).
Every disc here is made up: synthetic Opera, ISO 9660 and FAT images with made-up hashes, never a game's files."""
import hashlib
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import extras  # noqa: E402
import opera  # noqa: E402
from test_disc_image import fat_image, iso_image, payload  # noqa: E402


def opera_track(files):
    """Raw 2352-byte sectors of a 3DO Opera volume with {name: bytes} in its root, in tools/opera.py's layout."""
    vol = bytearray(2048)
    vol[0], vol[1:6], vol[0x28:0x2c] = 1, b'\x5a' * 5, b'TEST'
    struct.pack_into('>6I', vol, 0x4c, 0, 0, 0, 1, 0, 0)          # (one root block; one copy of it, at block 1)
    struct.pack_into('>I', vol, 0x64, 1)
    entries, blocks, at = bytearray(), [], 2
    for name, data in files.items():
        n = max(1, -(-len(data) // 2048))
        entries += struct.pack('>II4sIIIII', 2, 0, b'    ', 2048, len(data), n, 0, 0) + name.encode().ljust(32, b'\0') \
            + struct.pack('>II', 0, at)
        blocks.append(data.ljust(n * 2048, b'\0'))
        at += n
    root = bytearray(2048)
    struct.pack_into('>iiIII', root, 0, -1, -1, 0, 20 + len(entries), 20)
    root[20:20 + len(entries)] = entries
    user = bytes(vol) + bytes(root) + b''.join(blocks)
    return b''.join(bytes(16) + user[i:i + 2048] + bytes(288) for i in range(0, len(user), 2048))


def sdx2_bytes(n, seed):
    return bytes((i * 37 + seed) & 0xff for i in range(n))


def aifc(samples, rate=22050):
    e, mant = 16383 + rate.bit_length() - 1, rate << (64 - rate.bit_length())
    comm = struct.pack('>HIH', 1, len(samples), 8) + struct.pack('>HQ', e, mant) + b'SDX2'
    ssnd = struct.pack('>II', 0, 0) + samples
    body = b'AIFC' + b'COMM' + struct.pack('>I', len(comm)) + comm + b'SSND' + struct.pack('>I', len(ssnd)) + ssnd
    return b'FORM' + struct.pack('>I', len(body)) + body


def stream(samples, rate=22050):
    head = bytearray(52)
    head[0:4], head[16:20] = b'SNDS', b'SHDR'
    struct.pack_into('>I', head, 4, 52)
    struct.pack_into('>II', head, 44, rate, 1)
    smp = bytearray(24 + len(samples))
    smp[0:4], smp[16:20] = b'SNDS', b'SSMP'
    struct.pack_into('>I', smp, 4, len(smp))
    struct.pack_into('>I', smp, 20, len(samples))
    smp[24:] = samples
    return bytes(head) + bytes(smp)


class DefaultDataTest(unittest.TestCase):
    def test_systems(self):
        with mock.patch.object(extras.Path, 'home', return_value=Path('/Users/p')):
            self.assertEqual(extras.default_data('darwin', {}), Path('/Users/p/Games/ThemeParkNative'))
            self.assertEqual(extras.default_data('darwin', {'TP_DATA': '/x'}), Path('/x'))
        self.assertEqual(extras.default_data('win32', {'LOCALAPPDATA': r'C:\Users\p\AppData\Local'}),
                         Path(r'C:\Users\p\AppData\Local', 'ThemeParkNative'))
        self.assertEqual(extras.default_data('win32', {'LOCALAPPDATA': 'L', 'TP_DATA': 'D'}), Path('D'))
        self.assertEqual(extras.default_data('win32', {'LOCALAPPDATA': 'L', 'TP_DATA': ''}), Path('L', 'ThemeParkNative'))
        self.assertEqual(extras.default_data('linux', {'HOME': '/home/p'}), Path('/home/p/.local/share/ThemeParkNative'))
        self.assertEqual(extras.default_data('linux', {'HOME': '/home/p', 'XDG_DATA_HOME': ''}),
                         Path('/home/p/.local/share/ThemeParkNative'))
        self.assertEqual(extras.default_data('linux', {'HOME': '/home/p', 'XDG_DATA_HOME': '/d'}), Path('/d/ThemeParkNative'))
        self.assertEqual(extras.default_data('linux', {'XDG_DATA_HOME': '/d', 'TP_DATA': '/t'}), Path('/t'))


class Import3doTest(unittest.TestCase):
    """A made-up 3DO disc as a bin/cue: its tunes and ride video imported, read from the data track in memory."""

    def test_cue(self):
        tune, extra, video = sdx2_bytes(3000, 1), sdx2_bytes(1001, 2), sdx2_bytes(5000, 3)
        raw = opera_track({'RIDE1.AIFC': aifc(tune), 'SHARES.AIFC': aifc(extra), 'FLIGHT.STREAM': stream(video)})
        pack = {'title': 'Test 3DO', 'gives': '-', 'key_file': ['Park', '0' * 64],
                'data_track_sha256': hashlib.sha256(extras.data_track_user(raw)).hexdigest(),
                'music': {'1': 'RIDE1.AIFC'}, 'music_extra': ['SHARES.AIFC'], 'ride_videos': {'0': 'FLIGHT', '16': 'FLIGHT'}}
        with tempfile.TemporaryDirectory() as t:
            disc, data = Path(t, 'disc'), Path(t, 'data')
            disc.mkdir()
            (disc / 'Test (Track 1).bin').write_bytes(raw)
            (disc / 'Test.cue').write_text('FILE "Test (Track 1).bin" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n')
            name, what = extras.scan(disc / 'Test.cue', {'3do-usa': pack}, data)
            self.assertEqual((name, what), ('3do-usa', '1 recorded tunes and 1 ride videos imported'))
            out = data / 'extras/3do-usa'
            self.assertEqual((out / 'music/RIDE1.wav').read_bytes(), opera.wav(opera.sdx2(tune)[0], 1, 22050))
            self.assertEqual((out / 'music/SHARES.wav').read_bytes(), opera.wav(opera.sdx2(extra)[0], 1, 22050))
            self.assertEqual((out / 'video/FLIGHT.STREAM').read_bytes(), stream(video))
            self.assertEqual((out / 'video/FLIGHT.wav').read_bytes(), opera.wav(opera.sdx2(video)[0], 1, 22050))
            self.assertEqual(json.loads((out / 'manifest.json').read_text()),
                             {'pack': '3do-usa', 'title': 'Test 3DO', 'tunes': {'1': 'music/RIDE1.wav'},
                              'ride_videos': {'0': 'video/FLIGHT', '16': 'video/FLIGHT'}})
            self.assertEqual(sorted(p.name for p in Path(t).iterdir()), ['data', 'disc'])      # (no temporary files left)


@unittest.skipIf(sys.platform == 'darwin', 'the Mac mounts images with hdiutil')
class ImageTest(unittest.TestCase):
    """.iso and .img images identified by their key files, read in place."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.main, self.demo, self.cd32 = payload(9000, 1), payload(4000, 2), payload(100, 3)
        h = lambda d: hashlib.sha256(d).hexdigest()
        self.packs = {'amiga-cd32': {'title': 'CD32', 'gives': '-', 'key_file': ['main', h(self.cd32)], 'data_track_sha256': '0' * 64},
                      'demo-1994': {'title': 'Demo', 'gives': 'the demo', 'key_file': ['GAME.EXE', h(self.demo)]},
                      'pc-cd': {'title': 'PC CD', 'gives': 'the PC CD', 'key_file': ['GAME/MAIN.EXE', h(self.main)]}}

    def scan(self, name, data):
        p = Path(self.tmp.name, name)
        p.write_bytes(data)
        return extras.scan(p, self.packs, Path(self.tmp.name, 'data'))

    def test_iso(self):
        self.assertEqual(self.scan('pc.iso', iso_image({'GAME/MAIN.EXE;1': self.main, 'SETUP.EXE;1': b'x'})), ('pc-cd', 'the PC CD'))
        self.assertEqual(self.scan('pc.img', iso_image({'game/main.exe;1': self.main}, mode=1)), ('pc-cd', 'the PC CD'))
        self.assertEqual(self.scan('cd32.iso', iso_image({'MAIN.;1': self.cd32})),
                         ('amiga-cd32', 'data track only: the audio tracks are missing (dump the whole disc as bin/cue)'))
        self.assertEqual(self.scan('other.iso', iso_image({'GAME/MAIN.EXE;1': self.demo})), (None, 'not a known disc'))

    def test_floppy(self):
        self.assertEqual(self.scan('demo.img', fat_image({'GAME.EXE': self.demo, 'GAME.DAT': b'x'})), ('demo-1994', 'the demo'))

    def test_unreadable(self):
        self.assertEqual(self.scan('x.img', payload(200000, 4))[0], None)
        self.assertIn('Mac', self.scan('x.dmg', payload(200000, 5))[1])


if __name__ == '__main__':
    unittest.main()
