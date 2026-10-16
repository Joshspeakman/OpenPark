#!/usr/bin/env python3
"""Write ADDR..ADDR+SIZE of a C-host checkpoint to a file (a loaded driver image).
The output is game-derived and stays in work/."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from spotcheck import load

ckpt, addr, size, out = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 16), Path(sys.argv[4])
_, _, mem = load(ckpt)
out.write_bytes(mem[addr:addr + size])
print('%s: %d bytes from %#x' % (out, size, addr))
