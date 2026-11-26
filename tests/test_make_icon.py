"""tools/make_icon.c, built with this machine's C compiler and platform/sdl/draw.c, on made-up art (a red card with a
green planet where GOG's box art has its own): tp_icon.m's geometry probed pixel by pixel (the planet cut from the card,
the sky, the band and its rule, the outline and keyline, the rounded corner), and its outputs read back: the .ico (32-bit
bitmaps and a 256 PNG), the icon theme's PNGs, the BMP. With a TrueType font (the launcher's Poppins Bold, else one of
this machine's), the band's word too."""
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import ico_image  # noqa: E402
import toolchain  # noqa: E402

DRAW = ROOT / 'platform/sdl/draw.c'
STB = ROOT / 'vendor/stb/stb_image_write.h'
FONTS = [ROOT / 'platform/sdl/fonts/Poppins-Bold.ttf', Path(os.environ.get('SystemRoot', r'C:\Windows'), 'Fonts/arialbd.ttf'),
         Path('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'), Path('/System/Library/Fonts/Supplemental/Arial Bold.ttf')]
FONT = next((f for f in FONTS if f.exists()), None)


def card(w=342, h=482, scale=1):
    """GOG's box art's stand-in: red, with a green disc where the planet is (centre (171, 284), radius 150 at 342 wide)."""
    def pixel(x, y):
        inside = (x + 0.5 - 171 * scale) ** 2 + (y + 0.5 - 284 * scale) ** 2 < (148 * scale) ** 2
        return b'\x20\xc0\x40\xff' if inside else b'\xff\x00\x00\xff'
    return w, h, [b''.join(pixel(x, y) for x in range(w)) for y in range(h)]


@unittest.skipUnless(DRAW.exists() and STB.exists() and shutil.which(toolchain.cc()[0]), 'needs platform/sdl/draw.c, vendor/stb and a C compiler')
class MakeIconTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.dir = Path(cls.tmp.name)
        cls.tool = cls.dir / ('make_icon' + toolchain.EXE)
        subprocess.run([*toolchain.cc(), '-O2', '-I', str(ROOT / 'platform/sdl'), '-I', str(ROOT / 'vendor/stb'),
                        str(ROOT / 'tools/make_icon.c'), str(DRAW), '-lm', '-o', str(cls.tool)], check=True)
        cls.art = cls.dir / 'box.png'
        ico_image.write_png(cls.art, *card())

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def make(self, *args, art=None):
        return subprocess.run([str(self.tool), str(art or self.art), *map(str, args)], capture_output=True, text=True)

    def picture(self, path):
        w, h, rows = ico_image._png_rgba(Path(path).read_bytes())
        return w, h, lambda x, y: tuple(rows[y][4 * x:4 * x + 4])

    def test_nightly(self):
        out = self.dir / 'nightly'
        out.mkdir()
        r = self.make('--box', '--nightly', '--png', out / 'icon.png', '--ico', out / 'icon.ico', '--hicolor', out / 'hicolor',
                      '--bmp', out / 'icon.bmp')
        self.assertEqual(r.returncode, 0, r.stderr)
        w, h, px = self.picture(out / 'icon.png')
        self.assertEqual((w, h), (1024, 1024))
        self.assertEqual(px(5, 5)[3], 0)                                   # (beyond the tile and its shadow)
        self.assertEqual(px(512, 945)[:3], (0, 0, 0))                      # (the shadow, below the tile)
        self.assertTrue(0 < px(512, 945)[3] < 0.45 * 255)
        self.assertEqual(px(512, 1000)[3], 0)
        self.assertLess(px(110, 110)[3], 0.45 * 255)                       # (outside the rounded corner: only shadow)
        r, g, b, a = px(512, 421)                                          # (the planet's middle: the card's green)
        self.assertEqual(a, 255)
        self.assertTrue(g > 150 and r < 80 and b < 100, (r, g, b))
        for x, y in ((234, 140), (790, 140), (150, 400)):                  # (the card outside the planet is cut: sky)
            r, g, b, a = px(x, y)
            self.assertTrue(a == 255 and b > 200 and b > g > r, (x, y, r, g, b))
        sky_top, sky_low = px(150, 300), px(150, 700)                      # (lighter at the top)
        self.assertGreater(sum(sky_top[:3]), sum(sky_low[:3]) + 100)
        self.assertEqual(px(512, 735)[:3], (8, 8, 14))                     # (the rule above the band)
        r, g, b, _ = px(160, 745)                                          # (the band's top: (0.36, 0.20, 0.78))
        self.assertTrue(abs(r - 92) < 6 and abs(g - 51) < 6 and abs(b - 199) < 6, (r, g, b))
        r, g, b, _ = px(512, 900)                                          # (its foot: toward (0.20, 0.10, 0.52))
        self.assertTrue(r < 70 and b < 150 and b > 2 * r, (r, g, b))
        self.assertEqual(px(96, 512)[:3], (8, 8, 14))                      # (the outline on the tile's edge)
        r, g, b, a = px(114, 512)                                          # (the yellow keyline)
        self.assertTrue(r > 200 and 150 < g < 225 and b < 120, (r, g, b))
        names = sorted(str(p.relative_to(out / 'hicolor')).replace(os.sep, '/') for p in (out / 'hicolor').rglob('*.png'))
        self.assertEqual(names, sorted('%dx%d/apps/theme-park-native.png' % (n, n) for n in (16, 32, 48, 64, 128, 256, 512)))
        for n in (16, 32, 48, 64, 128, 256, 512):
            self.assertEqual(self.picture(out / ('hicolor/%dx%d/apps/theme-park-native.png' % (n, n)))[:2], (n, n))
        ico = (out / 'icon.ico').read_bytes()
        self.assertEqual(struct.unpack('<HHH', ico[:6]), (0, 1, 7))
        sizes = []
        for i in range(7):
            w, h, _, _, planes, bits, size, at = struct.unpack('<BBBBHHII', ico[6 + 16 * i:22 + 16 * i])
            body = ico[at:at + size]
            n = w or 256
            sizes.append(n)
            self.assertEqual((w, h, planes, bits), (n % 256, n % 256, 1, 32))
            if n == 256:
                self.assertEqual(body[:8], b'\x89PNG\r\n\x1a\n')
                pic = ico_image._png_rgba(body)
            else:
                self.assertEqual(struct.unpack('<IiiHH', body[:16]), (40, n, 2 * n, 1, 32))
                self.assertEqual(len(body), 40 + n * n * 4 + (n + 31) // 32 * 4 * n)
                pic = ico_image._dib_rgba(body)
                mask = body[40 + n * n * 4:]
                row = (n + 31) // 32 * 4
                self.assertEqual(mask[(n - 1) * row] & 0x80, 0x80)          # (its top-left corner: see-through)
            if n in (16, 32, 48, 64, 128, 256):                            # (the same pixels as the icon theme's)
                self.assertEqual(pic, ico_image._png_rgba((out / ('hicolor/%dx%d/apps/theme-park-native.png' % (n, n))).read_bytes()))
        self.assertEqual(sizes, [16, 24, 32, 48, 64, 128, 256])
        self.assertEqual(ico_image.largest(out / 'icon.ico')[:2], (256, 256))
        bmp = (out / 'icon.bmp').read_bytes()
        self.assertEqual((bmp[:2], struct.unpack_from('<IiiHHI', bmp, 14)), (b'BM', (108, 256, 256, 1, 32, 3)))
        self.assertEqual(len(bmp), 14 + 108 + 256 * 256 * 4)

    def test_plain(self):
        """Without the band the planet is 680 across, in the middle of the tile."""
        out = self.dir / 'plain.png'
        r = self.make('--box', '--png', out)
        self.assertEqual(r.returncode, 0, r.stderr)
        _, _, px = self.picture(out)
        r, g, b, _ = px(512, 512)
        self.assertTrue(g > 150 and r < 80, (r, g, b))
        r, g, b, _ = px(512, 870)                                          # (no band: sky, then the planet's foot)
        self.assertTrue(b > 150 and b > r, (r, g, b))
        self.assertTrue(px(512, 215)[1] > 150 and px(512, 150)[2] > 200)  # (its top at 172, its rim soft for 23)

    def test_icon_art(self):
        """GOG's 256-pixel icon instead of the box art (its planet at (128, 125), radius 82 to 91)."""
        art = self.dir / 'icon-256.png'
        ico_image.write_png(art, 256, 256, [b''.join(b'\x20\xc0\x40\xff' if (x - 128) ** 2 + (y - 125) ** 2 < 80 ** 2 else b'\x00\x00\xff\x80'
                                                     for x in range(256)) for y in range(256)])
        out = self.dir / 'ico-art.png'
        r = self.make('--nightly', '--png', out, art=art)
        self.assertEqual(r.returncode, 0, r.stderr)
        _, _, px = self.picture(out)
        self.assertTrue(px(512, 421)[1] > 150)
        r, g, b, a = px(215, 125)
        self.assertTrue(a == 255 and b > 200 and b > g > r, (r, g, b))

    def test_refuses(self):
        short = self.dir / 'short.png'
        ico_image.write_png(short, *card(342, 300))
        self.assertEqual(self.make('--box', '--png', self.dir / 'x.png', art=short).returncode, 1)
        junk = self.dir / 'junk.jpg'
        junk.write_bytes(b'not a picture' * 100)
        self.assertEqual(self.make('--png', self.dir / 'x.png', art=junk).returncode, 1)
        self.assertEqual(self.make('--frobnicate').returncode, 2)
        self.assertFalse((self.dir / 'x.png').exists())

    @unittest.skipUnless(FONT, 'no TrueType font found on this machine')
    def test_word(self):
        """The band's word: white capitals in the band, none without a font."""
        with_word, without = self.dir / 'word.png', self.dir / 'no-word.png'
        self.assertEqual(self.make('--box', '--nightly', '--font', FONT, '--png', with_word).returncode, 0)
        self.assertEqual(self.make('--box', '--nightly', '--png', without).returncode, 0)

        def white(path):
            _, _, px = self.picture(path)
            rows = [y for y in range(740, 924) if any(min(px(x, y)[:3]) > 230 for x in range(200, 824, 2))]
            return (min(rows), max(rows)) if rows else None
        self.assertIsNone(white(without))
        top, bottom = white(with_word)
        self.assertLess(abs((top + bottom) / 2 - 819.8), 6)                # (the capitals' middle, as the Mac's)
        self.assertLess(abs(bottom - top - 73.3), 6)                       # (and their height)


if __name__ == '__main__':
    unittest.main()
