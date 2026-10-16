#!/usr/bin/env python3
"""Lift two bounded game routines with the existing port's lifter and compare ARM64
against the original x86 in Unicorn. Not a game boot or a complete CPU proof.
Generated game code and native artifacts stay under work/native-proof/.
"""
import argparse
import ctypes
import hashlib
import json
import random
import struct
import subprocess
import sys
from pathlib import Path
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn import x86_const as U
from le import LE

ROOT = Path(__file__).resolve().parents[1]
FUNCTIONS = (0x28656, 0x87790)
REGS = ('EAX', 'ECX', 'EDX', 'EBX', 'ESP', 'EBP', 'ESI', 'EDI')
STACK, SCRATCH, RETURN = 0x200000, 0x210000, 0x1ff000


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('exe', type=Path)
    ap.add_argument('--reference', type=Path, required=True, help='existing Freedom Force checkout, read-only')
    ap.add_argument('--iterations', type=int, default=1000)
    args = ap.parse_args()
    if args.iterations < 1:
        ap.error('iterations must be positive')
    digest = hashlib.sha256(args.exe.read_bytes()).hexdigest()
    expected = json.loads((ROOT / 'config/inputs.json').read_text())['executables']['GAME/MAIN.EXE']['sha256']
    if digest != expected:
        raise SystemExit('unsupported game executable digest')
    reference = args.reference.resolve()
    pins = json.loads((ROOT / 'config/reference.json').read_text())
    for name, sha in pins['files'].items():
        if hashlib.sha256((reference / name).read_bytes()).hexdigest() != sha:
            raise SystemExit(f'reference changed: {name}; review before updating its pin')
    sys.path.insert(0, str(reference / 'recomp/tools'))
    sys.dont_write_bytecode = True
    import lift
    image = LE.read(args.exe)
    if image.apply_offset_fixups():
        raise SystemExit('unsupported relocations remain')
    code = [o for o in image.objects if o.flags & 4]
    if len(code) != 1:
        raise SystemExit('proof expects one code object')
    image.imports = {}
    image.u32 = lambda va: int.from_bytes(image.bytes_at(va, 4), 'little')
    lift.PE = lambda _: image
    lift.code_range = lambda _: (code[0].base, code[0].base + code[0].size, bytes(code[0].data))
    out = ROOT / 'work/native-proof'
    out.mkdir(parents=True, exist_ok=True)
    analysis = out / 'analysis.json'
    analysis.write_text(json.dumps({'funcs': list(FUNCTIONS)}))
    lifter = lift.Lifter(str(args.exe), str(analysis))
    lifter.hooks = {}
    lifter.overrides = {}
    sources = []
    for address in FUNCTIONS:
        source, callees = lifter.lift_function(address)
        if callees or lifter.unsup or lifter.internal or lifter.unres_jt:
            raise SystemExit('routine is not a closed supported slice')
        sources.append(source)
    wrapper = '''
#include "rt.h"
#include <stdlib.h>
static uint8_t memory[4 * 1024 * 1024];
uint8_t *G_MEM = memory;
void *tp_memory(void) { return memory; }
void rt_unhandled(Ctx *c, uint32_t pc, const char *why) { abort(); }
'''
    wrapper += '\n'.join(sources)
    wrapper += '''
uint32_t tp_run(uint32_t fn, uint32_t *regs) {
    Ctx c = {0}; memcpy(&c, regs, 32); c.f.eflags = 0x202;
    switch (fn) {
'''
    wrapper += ''.join(f'case {f}u: f_{f:08x}(&c); break;\n' for f in FUNCTIONS)
    wrapper += 'default: abort(); } memcpy(regs, &c, 32); return flags_get(&c.f); }\n'
    source = out / 'proof.c'
    source.write_text(wrapper)
    library = out / 'proof.dylib'
    subprocess.run(['clang', '-arch', 'arm64', '-O2', '-dynamiclib', '-I', str(reference / 'recomp/runtime'), str(source), '-o', str(library)], check=True)
    lib = ctypes.CDLL(str(library))
    lib.tp_memory.restype = ctypes.c_void_p
    lib.tp_run.argtypes = [ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32)]
    lib.tp_run.restype = ctypes.c_uint32
    memory = lib.tp_memory()
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    uc.mem_map(0x10000, 0x300000)
    for obj in image.objects:
        uc.mem_write(obj.base, bytes(obj.data))
        ctypes.memmove(memory + obj.base, bytes(obj.data), obj.size)
    def write(at, data):
        uc.mem_write(at, data)
        ctypes.memmove(memory + at, data, len(data))
    rng = random.Random(0x544850)
    writable = [(o.base, o.size) for o in image.objects if o.flags & 2] + [(STACK, 4096), (SCRATCH, 4096)]
    boundary = [0, 1, 0x7fff, 0x8000, 0xffff, 0xffffffff]
    for fn in FUNCTIONS:
        for iteration in range(args.iterations):
            write(STACK, rng.randbytes(4096) if hasattr(rng, 'randbytes') else bytes(rng.getrandbits(8) for _ in range(4096)))
            write(SCRATCH, bytes(rng.getrandbits(8) for _ in range(4096)))
            values = [rng.getrandbits(32) for _ in range(8)]
            values[4] = STACK + 0x800
            if fn == FUNCTIONS[0]:
                parameters = [SCRATCH + 0x100, boundary[iteration] if iteration < len(boundary) else rng.getrandbits(32)]
            else:
                parameters = [boundary[(iteration + k) % len(boundary)] if iteration < len(boundary) else rng.getrandbits(32) for k in range(4)]
            write(values[4], struct.pack('<' + 'I' * (len(parameters) + 1), RETURN, *parameters))
            for name, value in zip(REGS, values):
                uc.reg_write(getattr(U, 'UC_X86_REG_' + name), value)
            uc.reg_write(U.UC_X86_REG_EFLAGS, 0x202)
            uc.emu_start(fn, RETURN, count=1000)
            if uc.reg_read(U.UC_X86_REG_EIP) != RETURN:
                raise AssertionError('original failed to return within instruction bound')
            registers = (ctypes.c_uint32 * 8)(*values)
            flags = lib.tp_run(fn, registers)
            expected = [uc.reg_read(getattr(U, 'UC_X86_REG_' + name)) for name in REGS]
            if list(registers) != expected:
                raise AssertionError((hex(fn), iteration, 'register mismatch'))
            if (flags ^ uc.reg_read(U.UC_X86_REG_EFLAGS)) & 0xcd5:
                raise AssertionError((hex(fn), iteration, 'defined flags mismatch'))
            for at, size in writable:
                if bytes(uc.mem_read(at, size)) != ctypes.string_at(memory + at, size):
                    raise AssertionError((hex(fn), iteration, 'memory mismatch', hex(at)))
    result = {'architecture': 'arm64', 'functions': list(FUNCTIONS), 'cases': len(FUNCTIONS) * args.iterations,
              'mismatches': 0, 'compared': ['eight integer registers', 'defined arithmetic flags and direction flag', 'all writable LE objects', 'scratch and stack'],
              'limits': 'Two integer/memory slices only; no startup, x87, DOS services, graphics, audio or game loop.'}
    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))

if __name__ == '__main__':
    main()
