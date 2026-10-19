"""tools/audio_glitches.py on a made-up recording: a tone, one jump cut into it, one ran-dry gap, and a
pause between two sounds that is not a gap."""
import math
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import audio_glitches  # noqa: E402

RATE = 44100


def tone(n, amp=0.8, f=440.0, phase0=0.0):
    return [(amp * math.sin(phase0 + 2 * math.pi * f * i / RATE),) * 2 for i in range(n)]


class AudioGlitchesTest(unittest.TestCase):
    def test_synthetic(self):
        s = tone(RATE)                                   # 1 s of a clean tone (ends near 0): no jumps
        s += tone(22075, phase0=math.pi / 2)             # at 1.0 s: starts at 0.8, a jump of ~0.85; ends at a zero crossing
        s += [(0.0, 0.0)] * (RATE // 100)                # at 1.5 s: 10 ms of exact zeros inside sound (ran dry)
        s += tone(RATE // 2)                             # (starts and ends near 0)
        s += [(0.0, 0.0)] * RATE                         # a second of silence between two sounds: a pause, not a gap
        s += tone(RATE // 4)
        jumps, gaps = audio_glitches.analyse(s, RATE, 0.5, 3)
        self.assertEqual(len(gaps), 1)
        self.assertAlmostEqual(gaps[0][0], 1.0 + 22075 / RATE, places=4)
        self.assertAlmostEqual(gaps[0][1], 10.0, places=1)
        self.assertEqual([round(t, 3) for t, _ in jumps], [1.0])
        self.assertGreater(jumps[0][1], 0.8)
        with tempfile.TemporaryDirectory() as t:
            p = Path(t) / 'rec.f32'
            p.write_bytes(b''.join(struct.pack('<ff', l, r) for l, r in s))
            back = audio_glitches.read_f32_stereo(p)
            self.assertEqual(len(back), len(s))
            for (l, r), (bl, br) in zip(s[:5000], back[:5000]):
                self.assertAlmostEqual(l, bl, places=6); self.assertAlmostEqual(r, br, places=6)

    def test_clean(self):
        jumps, gaps = audio_glitches.analyse(tone(RATE // 2), RATE, 0.5, 3)
        self.assertEqual((jumps, gaps), ([], []))


if __name__ == '__main__':
    unittest.main()
