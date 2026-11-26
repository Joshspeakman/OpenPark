"""runtime/sha256.c against hashlib, and runtime/platform_pool.c's parallel loop (Linux and Windows) visiting every
index once."""
import ctypes
import hashlib
import random
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import toolchain  # noqa: E402


@unittest.skipUnless(shutil.which(toolchain.cc()[0]), 'no C compiler')
class PlatformTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        # (any name: ctypes loads it by path. Windows can't delete a DLL its process still holds, and the pool's threads
        # live in it, so there it goes in the ignored build/ and the next run replaces it.)
        where = ROOT / 'build/test-platform' if toolchain.SYSTEM == 'windows' else Path(cls.tmp.name)
        where.mkdir(parents=True, exist_ok=True)
        library = where / 'platform.dylib'
        threads = ['-lpthread'] if toolchain.SYSTEM != 'macos' else []
        subprocess.run([*toolchain.cc(), '-O2', *toolchain.shared(), '-I', str(ROOT / 'runtime'), str(ROOT / 'runtime/sha256.c'),
                        str(ROOT / 'runtime/platform_pool.c'), '-o', str(library), *threads], check=True)
        cls.lib = ctypes.CDLL(str(library))

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_sha256(self):
        rng = random.Random(7)
        for n in (0, 1, 3, 55, 56, 57, 63, 64, 65, 119, 120, 128, 1000, 65537):
            data = bytes(rng.getrandbits(8) for _ in range(n))
            out = ctypes.create_string_buffer(32)
            self.lib.tp_sha256(data, ctypes.c_size_t(n), out)
            self.assertEqual(out.raw.hex(), hashlib.sha256(data).hexdigest(), n)

    def test_sha256_streamed(self):
        data = bytes(range(256)) * 41
        state = ctypes.create_string_buffer(256)
        self.lib.tp_sha256_init(state)
        for at in range(0, len(data), 77):
            chunk = data[at:at + 77]
            self.lib.tp_sha256_update(state, chunk, ctypes.c_size_t(len(chunk)))
        out = ctypes.create_string_buffer(32)
        self.lib.tp_sha256_final(state, out)
        self.assertEqual(out.raw.hex(), hashlib.sha256(data).hexdigest())

    def test_parallel_for(self):
        seen = (ctypes.c_int * 1000)()
        callback = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_size_t)(lambda ctx, i: seen.__setitem__(i, seen[i] + 1))
        for n in (0, 1, 7, 1000):
            for i in range(1000):
                seen[i] = 0
            self.lib.tp_pool_parallel_for(ctypes.c_size_t(n), None, callback)
            self.assertEqual(list(seen[:n]), [1] * n)
            self.assertEqual(sum(seen[n:]), 0)


if __name__ == '__main__':
    unittest.main()
