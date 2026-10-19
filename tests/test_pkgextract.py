"""tools/pkgextract.py against archives written by an independent tool (bsdtar/libarchive): a package-shaped
XAR holding package.pkg/Scripts, a gzip-compressed cpio with the game folder below ./payload."""
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import pkgextract  # noqa: E402

FILES = {'payload/Contents/Resources/game/GAME/MAIN.EXE': os.urandom(70001),
         'payload/Contents/Resources/game/GAME/DATA/LANG.DAT': b'x' * 513,
         'payload/Contents/Resources/game/goggame-1.ico': b'',
         'payload/Contents/Resources/other.txt': b'not wanted'}


@unittest.skipUnless(shutil.which('bsdtar'), 'needs bsdtar to write the test archives')
class PkgExtractTest(unittest.TestCase):
    def build(self, tmp, cpio_format, xar_compression):
        src = tmp / 'src'
        for name, data in FILES.items():
            (src / name).parent.mkdir(parents=True, exist_ok=True)
            (src / name).write_bytes(data)
        pkg = tmp / 'pkg/package.pkg'
        pkg.mkdir(parents=True)
        subprocess.run(['bsdtar', '--format', cpio_format, '-czf', str(pkg / 'Scripts'), '-C', str(src), './payload'], check=True)
        (pkg / 'PackageInfo').write_text('<pkg-info/>')
        out = tmp / 'test.pkg'
        subprocess.run(['bsdtar', '--format', 'xar', '--options', f'xar:compression={xar_compression}', '-cf', str(out),
                        '-C', str(tmp / 'pkg'), 'package.pkg'], check=True)
        return out, (pkg / 'Scripts').read_bytes()

    def check(self, cpio_format, xar_compression):
        with tempfile.TemporaryDirectory() as t:
            tmp = Path(t)
            pkg, scripts = self.build(tmp, cpio_format, xar_compression)
            got = pkgextract.xar_extract(pkg, 'package.pkg/Scripts', tmp / 'archive')
            self.assertEqual(hashlib.sha256(got.read_bytes()).digest(), hashlib.sha256(scripts).digest())
            n = pkgextract.cpio_extract(got, tmp / 'extracted', './payload/Contents/Resources/game')
            self.assertEqual(n, 3)
            for name, data in FILES.items():
                path = tmp / 'extracted' / name
                if 'game/' in name:
                    self.assertEqual(path.read_bytes(), data, name)
                else:
                    self.assertFalse(path.exists(), name)
            with self.assertRaises(KeyError):
                pkgextract.xar_extract(pkg, 'package.pkg/Payload', tmp / 'archive')

    def test_odc_gzip_xar(self):
        self.check('odc', 'gzip')

    def test_newc_plain_xar(self):
        self.check('newc', 'none')

    def test_unsafe_path(self):
        with self.assertRaises(ValueError):
            pkgextract._safe('/tmp/x', '../escape')


if __name__ == '__main__':
    unittest.main()
