"""tools/ico_image.py against ImageMagick: an icon it writes (PNG and 32-bit DIB entries) reads back to the same
pixels (within 1 where the icon's PNG has 16-bit samples: the two scale them to 8 bits a little differently),
and the PNG and BMP written read back the same in ImageMagick."""
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import ico_image  # noqa: E402


def magick_rgba(path, size):
    return subprocess.run(['convert', str(path), '-depth', '8', 'rgba:-'], check=True, capture_output=True).stdout[:size * size * 4]


@unittest.skipUnless(shutil.which('convert'), 'needs ImageMagick to write the test icon')
class IcoImageTest(unittest.TestCase):
    def test_round_trip(self):
        with tempfile.TemporaryDirectory() as t:
            tmp = Path(t)
            src = tmp / 'src.png'
            subprocess.run(['convert', '-size', '256x256', 'plasma:fractal', '-seed', '3', '-alpha', 'set', '-channel', 'A',
                            '-fx', '(i+j)/512', str(src)], check=True)
            for sizes, want in (('16,32,256', 256), ('16,48', 48)):          # (256: a PNG entry; the others DIBs)
                ico = tmp / f'{want}.ico'
                subprocess.run(['convert', str(src), '-define', f'icon:auto-resize={sizes}', str(ico)], check=True)
                w, h, rows = ico_image.largest(ico)
                self.assertEqual((w, h), (want, want))
                ref = magick_rgba(f'{ico}[{len(sizes.split(",")) - 1}]', want)
                self.assertEqual(len(ref), want * want * 4)
                self.assertLessEqual(max(abs(x - y) for x, y in zip(b''.join(rows), ref)), 1)
                ico_image.write_png(tmp / 'out.png', w, h, rows)
                ico_image.write_bmp(tmp / 'out.bmp', w, h, rows)
                self.assertEqual(magick_rgba(tmp / 'out.png', want), b''.join(rows))
                self.assertEqual(magick_rgba(tmp / 'out.bmp', want), b''.join(rows))


if __name__ == '__main__':
    unittest.main()
