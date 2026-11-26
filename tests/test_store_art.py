"""tools/store_art.py without the network: GOG's API and files stood in for by made-up pictures. A whole JPEG or PNG
is told from a cut-off one; the park test of build_app.sh picks the shots; a cut keeps a PNG's own rows (every filter
type) exactly; at most four shots, none if one is there; art kept only when it should be; TP_NO_DOWNLOADS=1 fetches
nothing."""
import json
import os
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import ico_image  # noqa: E402
import store_art  # noqa: E402


def chunk(kind, body):
    return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body))


def png(w, h, pixel, filters=(0, 1, 2, 3, 4), extra=b''):
    """An 8-bit RGBA PNG of pixel(x, y) -> (r, g, b, a), its rows filtered in turn with each of `filters`."""
    rows = [bytes(v for x in range(w) for v in pixel(x, y)) for y in range(h)]
    raw, prev = bytearray(), bytes(w * 4)
    for y, row in enumerate(rows):
        f = filters[y % len(filters)]
        out = bytearray([f])
        for i, v in enumerate(row):
            a, b, c = row[i - 4] if i >= 4 else 0, prev[i], prev[i - 4] if i >= 4 else 0
            p = a + b - c
            pred = (0, a, b, (a + b) // 2, a if abs(p - a) <= abs(p - b) and abs(p - a) <= abs(p - c) else b if abs(p - b) <= abs(p - c) else c)[f]
            out.append((v - pred) & 255)
        raw += out
        prev = row
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) + extra
            + chunk(b'IDAT', zlib.compress(bytes(raw))) + chunk(b'IEND', b'')), rows


def lawn(share):
    """A picture whose top 80 % is `share` lawn (in columns, as the test samples every 6th pixel) and grey below."""
    def pixel(x, y):
        if y >= 48:
            return (90, 90, 90, 255)
        return (40, 160, 50, 255) if (x // 6) % 20 < share * 20 else (120, 110, 100, 255)
    return pixel


def jpeg(size=30000, eoi=True, dims=(482, 342)):
    """A JPEG's structure (not a decodable picture): SOI, APP0, a baseline frame header, a scan and EOI."""
    app = b'JFIF\0' + bytes(size)
    sof = struct.pack('>BHHB', 8, *dims, 3) + b'\x01\x22\x00\x02\x11\x01\x03\x11\x01'
    sos = b'\x03\x01\x00\x02\x11\x03\x11\x00\x3f\x00'
    return (b'\xff\xd8' + b'\xff\xe0' + struct.pack('>H', len(app) + 2) + app + b'\xff\xc0' + struct.pack('>H', len(sof) + 2) + sof
            + b'\xff\xda' + struct.pack('>H', len(sos) + 2) + sos + bytes(range(1, 200)) + (b'\xff\xd9' if eoi else b''))


class PictureTest(unittest.TestCase):
    def test_readable(self):
        good, _ = png(9, 7, lambda x, y: (x, y, 0, 255))
        self.assertTrue(store_art.readable(good))
        self.assertFalse(store_art.readable(good[:-20]))
        self.assertFalse(store_art.readable(good[:40] + b'\0' + good[41:]))           # (a damaged chunk)
        self.assertTrue(store_art.readable(jpeg()))
        self.assertFalse(store_art.readable(jpeg(eoi=False)))
        self.assertFalse(store_art.readable(jpeg(dims=(0, 342))))
        self.assertFalse(store_art.readable(jpeg()[:5000]))
        self.assertFalse(store_art.readable(b'<html>' + bytes(30000)))

    def test_is_park(self):
        for share, want in ((0.0, False), (0.25, False), (0.4, True), (0.5, True), (0.6, False), (1.0, False)):
            _, rows = png(120, 60, lawn(share))
            self.assertEqual(store_art.is_park(120, 60, rows), want, share)

    def test_crop(self):
        data, rows = png(37, 40, lambda x, y: ((x * 7 + y) & 255, (y * 13) & 255, (x * y) & 255, (200 + x) & 255),
                         extra=chunk(b'gAMA', struct.pack('>I', 45455)) + chunk(b'tEXt', b'Comment\0x'))
        cut = store_art.crop_png(data, 40 * 85 // 100)
        w, h, got = ico_image._png_rgba(cut)
        self.assertEqual((w, h), (37, 34))
        self.assertEqual(got, rows[:34])
        kinds = [k for k, _ in store_art.png_chunks(cut)]
        self.assertEqual(kinds, [b'IHDR', b'gAMA', b'IDAT', b'IEND'])
        with self.assertRaises(ValueError):
            store_art.crop_png(data, 41)


class FetchTest(unittest.TestCase):
    API_ART = {'_links': {'galaxyBackgroundImage': {'href': 'https://x/keyart.jpg'}, 'logo': {'href': 'https://x/logo.png'},
                          'boxArtImage': {'href': 'https://x/box.jpg'}}}

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.out = Path(self.tmp.name)
        self.said, self.asked = [], []
        big, _ = png(120, 60, lambda x, y: ((x * 31 + y * 17) & 255, (x * y * 7) & 255, (x ^ y) & 255, 255), filters=(0,))
        self.files = {store_art.ART_API: json.dumps(self.API_ART).encode(), 'https://x/keyart.jpg': jpeg(),
                      'https://x/logo.png': big, 'https://x/box.jpg': jpeg(size=100)}
        shots = [lawn(0.4), lawn(1.0), lawn(0.5), lawn(0.0), lawn(0.45), lawn(0.35), lawn(0.4)]
        self.shots = []
        for k, pixel in enumerate(shots):
            data, rows = png(120, 60, pixel)
            self.files['https://s/%d.png' % k] = data
            self.shots.append(rows)
        self.files[store_art.SHOTS_API] = json.dumps({'screenshots': [{'formatter_template_url': 'https://s/%d_{formatter}.png' % k}
                                                                      for k in range(len(shots))]}).encode()

    def get(self, url, timeout):
        self.asked.append(url)
        if url not in self.files:
            raise OSError('no such file')
        return self.files[url]

    def test_art(self):
        with mock.patch.object(store_art, 'get', self.get):
            store_art.fetch_art(self.out, self.said.append)
        self.assertEqual((self.out / 'keyart.jpg').read_bytes(), self.files['https://x/keyart.jpg'])
        self.assertEqual((self.out / 'logo.png').read_bytes(), self.files['https://x/logo.png'])
        self.assertFalse((self.out / 'box.jpg').exists())                    # (20,000 bytes or less: not GOG's picture)
        self.assertEqual(self.said, ['   (box.jpg could not be downloaded from GOG)'])
        self.assertEqual(sorted(p.name for p in self.out.iterdir()), ['keyart.jpg', 'logo.png'])
        self.asked = []
        with mock.patch.object(store_art, 'get', self.get):
            store_art.fetch_art(self.out, self.said.append)
        self.assertEqual(self.asked, [store_art.ART_API, 'https://x/box.jpg'])   # (what is there is never fetched again)

    def test_art_unlisted(self):
        del self.files[store_art.ART_API]
        with mock.patch.object(store_art, 'get', self.get):
            store_art.fetch_art(self.out, self.said.append)
        self.assertEqual(self.said, ["   (GOG's store art could not be listed)"])
        self.assertEqual(list(self.out.iterdir()), [])

    def test_shots(self):
        with mock.patch.object(store_art, 'get', self.get):
            store_art.park_shots(self.out, self.said.append)
        self.assertEqual(self.said, ["   4 of GOG's park pictures for the launch window"])
        self.assertEqual(sorted(p.name for p in self.out.iterdir()), ['park-shot-%d.png' % k for k in (1, 2, 3, 4)])
        for k, shot in zip((1, 2, 3, 4), (0, 2, 4, 5)):                       # (lawn: 40, 100, 50, 0, 45, 35 %: four parks)
            w, h, rows = ico_image._png_rgba((self.out / ('park-shot-%d.png' % k)).read_bytes())
            self.assertEqual((w, h, rows), (120, 51, self.shots[shot][:51]))
        self.assertNotIn('https://s/6.png', self.asked)                      # (it stops at four)
        self.asked = []
        with mock.patch.object(store_art, 'get', self.get):
            store_art.park_shots(self.out, self.said.append)
        self.assertEqual(self.asked, [])

    def test_no_downloads(self):
        out = self.out / 'art'
        r = subprocess.run([sys.executable, str(ROOT / 'tools/store_art.py'), str(out)], capture_output=True, text=True,
                           env={**os.environ, 'TP_NO_DOWNLOADS': '1'})
        self.assertEqual((r.returncode, r.stdout, out.exists()), (0, '', False))


if __name__ == '__main__':
    unittest.main()
