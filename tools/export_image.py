#!/usr/bin/env python3
"""Write the relocated MAIN.EXE memory image the native host loads (TPIMG001).

The output is game-derived and belongs only in work/ or the user's local data
folder, never in git."""
import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dos import load_image


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('exe', type=Path)
    ap.add_argument('out', type=Path)
    args = ap.parse_args()
    image = load_image(args.exe)
    data = bytearray(b'TPIMG001' + struct.pack('<III', image.entry, image.stack, len(image.objects)))
    for obj in image.objects:
        data += struct.pack('<II', obj.base, len(obj.data)) + bytes(obj.data)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(data)
    print('%s: entry %#x stack %#x objects %d' % (args.out, image.entry, image.stack, len(image.objects)))


if __name__ == '__main__':
    main()
