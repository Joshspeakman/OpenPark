#!/usr/bin/env python3
"""Replay C-host checkpoints through the compiled ARM64 blocks and the x86
oracle (Unicorn) in lockstep, from any point of any run (fuzzing included).

Each checkpoint runs until the first DOS/BIOS interrupt or --blocks, in quanta,
comparing registers, selectors, IF/DF and port traffic after every quantum and
all memory at the end. Ports use a small shared deterministic model, so this
checks CPU translation between service boundaries, not the DOS host."""
import argparse
import ctypes
import json
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from unicorn import UC_HOOK_INSN, UC_HOOK_INTR, UC_HOOK_CODE
from unicorn import x86_const as U
from dos import REGS, SEGMENTS, MEMORY
from trace_startup import Original
from native_host import Native

NAMES = REGS + ('eflags', 'eip') + SEGMENTS


def load(path):
    d = Path(path).read_bytes()
    assert d[:8] == b'TPCKPT01', path
    regs = struct.unpack_from('<16I', d, 8)
    n, = struct.unpack_from('<I', d, 72)
    bases = dict(struct.unpack_from('<%dI' % (2 * n), d, 76)[k:k + 2] for k in range(0, 2 * n, 2))
    at = 76 + 8 * n
    zn, = struct.unpack_from('<Q', d, at)
    mem = zlib.decompress(d[at + 8:at + 8 + zn])
    return dict(zip(NAMES, regs)), bases, mem


class Ports:
    def __init__(self):
        self.log, self.n = [], 0

    def io(self, out, port, size, value):
        self.log.append((out, port, size, value))
        if out:
            return 0
        if port == 0x3da:
            self.n += 1
            return 8 if self.n & 1 else 0
        return 0xff


EXTENTS = {}
DECODER = None


def boundaries(native, start):
    import capstone
    global DECODER
    DECODER = DECODER or capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    lengths = EXTENTS.get(start, [16])
    data = native.read(start, max(lengths))
    return {i.address for i in DECODER.disasm(data, start)}, max(lengths)


def executes(native, block, target):
    """Times running `block` executes the instruction at `target` (0 or 1)."""
    if block == target:
        return 1
    if not block < target < block + max(EXTENTS.get(block, [0])):
        return 0
    return int(target in boundaries(native, block)[0])


def is_rep(native, pc):
    data = native.read(pc, 4)
    k = 0
    while k < 3 and data[k] in (0x66, 0x67, 0x26, 0x2e, 0x36, 0x3e, 0x64, 0x65):
        k += 1
    return data[k] in (0xf2, 0xf3)


def check(path, native, blocks, quantum):
    regs, bases, mem = load(path)
    original = Original()
    pn, po = Ports(), Ports()
    original.uc.hook_add(UC_HOOK_INSN, lambda uc, p, s, _: po.io(0, p, s, 0), None, 1, 0, U.UC_X86_INS_IN)
    original.uc.hook_add(UC_HOOK_INSN, lambda uc, p, s, v, _: po.io(1, p, s, v), None, 1, 0, U.UC_X86_INS_OUT)
    hit = {'int': 0}
    def on_int(uc, number, _):
        hit['int'] = number
        uc.emu_stop()
    original.uc.hook_add(UC_HOOK_INTR, on_int)
    native.lib.tp_reset()
    for m in (native, original):
        m.write(0, mem)
    for sel, base in bases.items():
        native.lib.tp_descriptor(sel, base)
    for name in NAMES:
        native.set(name, regs[name])
        original.set(name, regs[name])
    cb = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_int, ctypes.c_uint, ctypes.c_uint, ctypes.c_uint)(pn.io)
    native.lib.tp_io_callback(cb)
    buf = (ctypes.c_uint32 * (quantum + 64))()
    ctypes.c_void_p.in_dll(native.lib, 'tp_trace_buf').value = ctypes.addressof(buf)
    ctypes.c_uint32.in_dll(native.lib, 'tp_trace_cap').value = quantum + 1
    done, n, segments = 0, 0, 0
    while done < blocks:
        before = native.get('eip')
        ctypes.c_uint32.in_dll(native.lib, 'tp_trace_n').value = 0
        n = native.lib.tp_run(quantum)
        if n < 0:
            # Does the original fault at the same place? (A division by zero
            # raises interrupt 0 in Unicorn.)
            err = native.lib.tp_error().decode()
            hit['int'] = None
            try:
                original.uc.emu_start(before, 0, timeout=20000000, count=50000000)
            except Exception as e:
                hit['int'] = str(e)
            return {'checkpoint': str(path), 'result': 'native stop', 'pc': hex(native.get('eip')), 'error': err,
                    'original_interrupt': hit['int'], 'original_pc': hex(original.get('eip')),
                    'same_registers': all(native.get(r) == original.get(r) for r in REGS)}
        ran = ctypes.c_uint32.in_dll(native.lib, 'tp_trace_n').value
        done += ran
        hit['int'] = 0
        if n:
            original.uc.emu_start(before, 0, timeout=20000000, count=50000000)
        else:
            # Synchronise by arrival count at native's stopping address; the two
            # sides may split blocks differently (REP, segment loads, POPFD).
            target = native.get('eip')
            # REP string instructions re-enter their address once per iteration
            # under Unicorn: extend the segment past them.
            while is_rep(native, target):
                if native.lib.tp_run(1) != 0:
                    raise SystemExit('interrupt while extending past REP at %x' % target)
                buf[ran] = target; ran += 1; done += 1
                target = native.get('eip')
            k = sum(executes(native, buf[i], target) for i in range(ran))
            seen = {'n': 0}
            def at_target(uc, address, size, _):
                seen['n'] += 1
                if seen['n'] > k:
                    uc.emu_stop()
            h = original.uc.hook_add(UC_HOOK_CODE, at_target, None, target, target)
            original.uc.emu_start(before, 0, timeout=20000000, count=50000000)
            original.uc.hook_del(h)
        segments += 1
        bad = [r for r in REGS + SEGMENTS + ('eip',) if native.get(r) != original.get(r)]
        if (native.get('eflags') ^ original.get('eflags')) & 0x600:
            bad.append('IF/DF')
        if n != hit['int']:
            bad.append('interrupt %s/%s' % (n, hit['int']))
        if pn.log != po.log:
            bad.append('ports')
        if bad:
            return {'checkpoint': str(path), 'result': 'MISMATCH', 'after_blocks': done, 'what': bad, 'segment_from': hex(before),
                    'native': {r: hex(native.get(r)) for r in NAMES}, 'original': {r: hex(original.get(r)) for r in NAMES}}
        if n:
            break
    a, b = native.read(0, MEMORY), original.read(0, MEMORY)
    if a != b:
        at = next(k for k in range(0, MEMORY, 4096) if a[k:k + 4096] != b[k:k + 4096])
        at += next(k for k in range(4096) if a[at + k] != b[at + k])
        return {'checkpoint': str(path), 'result': 'MISMATCH', 'what': ['memory'], 'address': hex(at),
                'native': a[at:at + 16].hex(), 'original': b[at:at + 16].hex()}
    return {'checkpoint': str(path), 'result': 'match', 'blocks': done, 'segments': segments,
            'stopped_at_interrupt': n, 'pc': hex(native.get('eip'))}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('checkpoints', nargs='+', type=Path)
    ap.add_argument('--library', type=Path, required=True)
    ap.add_argument('--blocks', type=int, default=200000)
    ap.add_argument('--quantum', type=int, default=1000)
    args = ap.parse_args()
    native = Native(args.library)
    extents = args.library.parent / 'blocks.json'
    EXTENTS.update({int(k): v for k, v in json.loads(extents.read_text()).items()})
    results = [check(p, native, args.blocks, args.quantum) for p in args.checkpoints]
    for r in results:
        print(json.dumps(r))
    if any(r['result'] != 'match' for r in results):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
