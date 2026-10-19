#!/usr/bin/env python3
"""Make the HMI sound driver images (work/drv-ADDR-MODE-NAME.bin + .mask) from the owned package.

The game copies its HMI SOS drivers out of HMIMDRV.386, HMIDET.386 and HMIDRV.386 into DOS memory and calls
them (tools/dump_drivers.md). Three run as they are in those files. The mixer patches its own instructions
and writes a generated routine into itself once sound is mixed, and the build needs those bytes: they are
compiled ahead of time like the rest of the game, with the bytes a driver rewrites guarded on every
execution. Nothing here is copied from anywhere but the owned files and the game's own run:

  1. cut each driver out of its .386 file (offsets below; the cut bytes are checked by hash);
  2. build the game with the drivers fully guarded (only their 32-byte header is identity-checked), run it
     headless with sound on through the scripted start into the first park (tests/check_macos.sh --park's
     input), and stop at the first block whose bytes changed (TP_DUMP_ON_STOP);
  3. take the changed bytes into the images and repeat until a run finishes without a stop;
  4. write each image with a mask: 0 where the image differs from the file (bytes the driver rewrites).

The outputs are game-derived and stay in work/ (ignored). Their hashes are in config/drivers.json; a
mismatch is reported (another package or a changed host would show there).

  tools/make_drivers.py GAME/MAIN.EXE --reference vendor/freedom-force-recomp [--out work]
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# (load address, entered as, name, file, offset, length): where the game loads each driver with GOG's
# sndsetup.inf (SB16 220h/IRQ 7/DMA 1, General MIDI 330h). The detection driver and the digital driver share
# 0x402c0 (one after the other); the digital driver and the mixer are entered flat, then through their own
# code selector, so each has both forms (same bytes).
DRIVERS = [
    (0x40034, 'flat', 'mpu', 'HMIMDRV.386', 626, 647),
    (0x402c0, 'flat', 'det', 'HMIDET.386', 7885, 954),
    (0x402c0, 'flat', 'dig', 'HMIDRV.386', 9309, 1763),
    (0x402c0, 'cs', 'dig', 'HMIDRV.386', 9309, 1763),
    (0x409a8, 'flat', 'mix', 'HMIDRV.386', 109380, 18577),
    (0x409a8, 'cs', 'mix', 'HMIDRV.386', 109380, 18577),
]
HEADER = 32                                  # bytes the build checks to know which driver is loaded
# The mixer writes one generated routine into each of its 32 voice slots (350 bytes from body offset 0x1cd1)
# as voices come into use: the same routine in every slot. The scripted run uses a few voices; the rest of the
# slots are filled with the routine it generated, so a busy park later finds them as the game would write them.
MIX_SLOTS = (0x1cd1, 350, 32)
PARK = ['--quantum', '1000', '--blocks', '8000000',
        '--keys', '60:1c0d,80:1454,82:1245,84:1f53,86:1454,88:1c0d,120:3b00,300:3d00',
        '--clicks', '180:564:316,220:280:90',
        '--block-clicks', '2450000:72:310,2650000:568:316,2800000:604:380']
RUNTIME = ['host.c', 'sb16.c', 'headless.c', 'verify.c', 'view.c', 'native_land.c', 'native_draw.c', 'qol.c',
           'walk.c', 'bigmap.c', 'freemode.c']


def checkpoint_memory(path):
    d = Path(path).read_bytes()
    if d[:8] != b'TPCKPT01':
        sys.exit('%s: not a host checkpoint' % path)
    n, = int.from_bytes(d[72:76], 'little'),
    at = 76 + 8 * n
    zn = int.from_bytes(d[at:at + 8], 'little')
    return zlib.decompress(d[at + 8:at + 8 + zn])


def name(at, mode, kind):
    return 'drv-%05x-%s-%s.bin' % (at, mode, kind)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('exe', type=Path, help='the owned GAME/MAIN.EXE (verified extraction)')
    ap.add_argument('--reference', type=Path, default=ROOT / 'vendor/freedom-force-recomp')
    ap.add_argument('--out', type=Path, default=ROOT / 'work')
    ap.add_argument('--trace', type=Path, default=ROOT / 'work/check-startup/trace.json')
    ap.add_argument('--survey', type=Path, default=ROOT / 'work/main-survey.json')
    ap.add_argument('--rounds', type=int, default=12)
    args = ap.parse_args()
    game = args.exe.resolve().parent
    setup = game.parent / 'THEME.CD' / 'SNDSETUP.INF'
    for p in (args.trace, args.survey, setup):
        if not p.exists():
            sys.exit('missing %s (run tests/check_macos.sh ... --park first)' % p)
    pins = json.loads((ROOT / 'config/drivers.json').read_text())
    work = args.out / 'drivers-make'
    if work.exists():
        shutil.rmtree(work)
    imgdir = work / 'images'
    imgdir.mkdir(parents=True)

    # 1. The drivers as they are in their files.
    original = {}
    for at, mode, kind, fname, off, length in DRIVERS:
        data = (game / fname).read_bytes()[off:off + length]
        if hashlib.sha256(data).hexdigest() != pins['files'][kind]:
            sys.exit('%s in %s is not the expected driver (another package?)' % (kind, fname))
        original[(at, mode, kind)] = data
    image = dict(original)

    def write(final):
        for f in imgdir.glob('drv-*'):
            f.unlink()
        for (at, mode, kind), data in image.items():
            (imgdir / name(at, mode, kind)).write_bytes(data)
            if final:
                mask = bytes(0 if a != b else 1 for a, b in zip(data, original[(at, mode, kind)]))
                mask = bytes([1]) * HEADER + mask[HEADER:]
            else:                                # guard everything: stop at the first changed block
                mask = bytes([1]) * HEADER + bytes(len(data) - HEADER)
            if final and mask.count(0) == 0:
                continue
            (imgdir / (name(at, mode, kind) + '.mask')).write_bytes(mask)

    python = sys.executable
    inc = args.reference / 'recomp' / 'runtime'
    tpimg = work / 'MAIN.TPIMG'
    subprocess.run([python, str(ROOT / 'tools/export_image.py'), str(args.exe), str(tpimg)], check=True, stdout=subprocess.DEVNULL)
    overlay = work / 'overlay'

    # 2.-3. Guarded runs until nothing changes.
    for round_no in range(1, args.rounds + 1):
        write(final=False)
        build = work / 'build'
        subprocess.run([python, str(ROOT / 'tools/build_game.py'), str(args.exe), '--reference', str(args.reference),
                        '--trace', str(args.trace), '--survey', str(args.survey), '--out', str(build), '--opt=-O1',
                        '--drivers', str(imgdir)], check=True, stdout=subprocess.DEVNULL)
        runner = work / 'tp-headless'
        subprocess.run(['clang', '-arch', 'arm64', '-O2', '-w', '-I', str(inc), '-I', str(ROOT / 'runtime')]
                       + [str(ROOT / 'runtime' / f) for f in RUNTIME] + [str(build / 'game.a'), '-lz', '-o', str(runner)], check=True)
        if overlay.exists():
            shutil.rmtree(overlay)
        overlay.mkdir()
        shutil.copy(setup, overlay / 'sndsetup.inf')     # sound on (the overlay is searched first)
        ckpt = work / 'stop.ckpt'
        if ckpt.exists():
            ckpt.unlink()
        env = dict(os.environ, DS_TEST='1', W32_AUDIO_MUTE='1', TP_DUMP_ON_STOP=str(ckpt))
        r = subprocess.run([str(runner), '--game', str(game), '--overlay', str(overlay), '--image', str(tpimg),
                            '--out', str(work / ('run%d' % round_no))] + PARK, env=env, capture_output=True, text=True)
        status = re.search(r'"status": "([^"]*)"', r.stdout)
        status = status.group(1) if status else 'no report'
        if not ckpt.exists():
            print('round %d: %s: converged' % (round_no, status))
            break
        mem = checkpoint_memory(ckpt)
        changed = 0
        for key, data in list(image.items()):
            at = key[0]
            live = mem[at:at + len(data)]
            if live[:HEADER] != original[key][:HEADER]:
                continue                         # another driver is at this address now
            if live != data:
                changed += sum(a != b for a, b in zip(live, data))
                image[key] = bytes(live)
        print('round %d: %s: %d bytes taken into the images' % (round_no, status, changed))
        if not changed:
            sys.exit('stopped without a driver change (%s): not a driver rewrite' % status)
    else:
        sys.exit('no convergence after %d rounds' % args.rounds)

    # The mixer's voice slots: every one holds the routine the run generated in slot 0.
    start, size, count = MIX_SLOTS
    for key in [k for k in image if k[2] == 'mix']:
        data, orig = bytearray(image[key]), original[key]
        slot0 = bytes(data[start:start + size])
        if slot0 == orig[start:start + size]:
            sys.exit('the mixer generated no routine in the run (no sound mixed?)')
        seen = [k for k in range(count) if data[start + k * size:start + (k + 1) * size] != orig[start + k * size:start + (k + 1) * size]]
        if any(data[start + k * size:start + (k + 1) * size] != slot0 for k in seen):
            sys.exit('the mixer wrote different routines into its slots: not the expected driver behaviour')
        for k in range(count):
            data[start + k * size:start + (k + 1) * size] = slot0
        image[key] = bytes(data)
        print('mixer: routine generated in %d of %d slots by the run, copied to all' % (len(seen), count))

    # 4. The images and masks.
    write(final=True)
    report, ok = {}, True
    for f in sorted(imgdir.glob('drv-*')):
        h = hashlib.sha256(f.read_bytes()).hexdigest()
        report[f.name] = h
        want = pins['images'].get(f.name)
        if want and want != h:
            ok = False
            print('note: %s differs from the recorded hash' % f.name)
        shutil.copy(f, args.out / f.name)
    print(json.dumps(report, indent=2))
    print('driver images in %s%s' % (args.out, '' if ok else ' (hash differences above)'))


if __name__ == '__main__':
    main()
