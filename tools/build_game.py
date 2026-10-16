#!/usr/bin/env python3
"""Statically compile all reachable MAIN.EXE code blocks to one ARM64 library.

Seeds: the entry point, every relocated pointer into the code object (jump and
callback tables are relocated), the survey's call targets, and the blocks of a
verified trace. Recursive descent follows direct branches, call continuations
and interrupt returns. Traced blocks keep their observed extents and byte
variants so verified scheduling stays identical. No decoder or interpreter
exists at runtime: a target that was not compiled stops with its address.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path
from capstone import x86 as X

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dos import load_image, BIAS
from lift_dos import create_lifter, ROOT

# Bytes the DOS host places at bootstrap (dos.py DOS.bootstrap).
SYNTHETIC = {0xf000: bytes.fromhex('cf'), 0xf010: bytes.fromhex('0fa90fa1071f619dc3')}
TERMINAL = ('ret', 'retf', 'iretd', 'hlt', 'ud2')


def successors(i):
    mn, nxt = i.mnemonic, i.address + i.size
    op = i.operands[0] if i.operands else None
    imm = op.imm & 0xffffffff if op is not None and op.type == X.X86_OP_IMM else None
    if mn == 'jmp':
        return [imm] if imm is not None else []
    if mn == 'call':
        return [nxt] + ([imm] if imm is not None else [])
    if mn.startswith('j') or mn.startswith('loop'):
        return [nxt] + ([imm] if imm is not None else [])
    if mn in TERMINAL:
        return []
    return [nxt]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('exe', type=Path)
    ap.add_argument('--reference', type=Path, required=True)
    ap.add_argument('--trace', type=Path, action='append', default=[])
    ap.add_argument('--survey', type=Path)
    ap.add_argument('--extra', default='', help='comma-separated extra block addresses (hex)')
    ap.add_argument('--out', type=Path, default=Path('work/game-build'))
    ap.add_argument('--per-file', type=int, default=1500)
    ap.add_argument('--opt', default='-O1')
    args = ap.parse_args()
    out = args.out
    out.mkdir(parents=True, exist_ok=True)
    image = load_image(args.exe)
    lifter = create_lifter(args.reference.resolve(), image, out)
    lifter.illegal_seh = False
    code, = [o for o in image.objects if o.flags & 4]
    lo, hi = code.base, code.base + code.size

    # Observed extents/variants from traces (address -> set of hex byte strings).
    observed = {}
    for path in args.trace:
        trace = json.loads(path.read_text())
        for k, v in trace['variants'].items():
            observed.setdefault(int(k), set()).update(v)
    seeds = {image.entry} | set(SYNTHETIC) | set(observed)
    for fix in image.fixups:
        target = (image.objects[fix.target_object - 1].base + fix.target_offset) & 0xffffffff
        if fix.kind == 7 and lo <= target < hi:
            seeds.add(target)
    if args.survey:
        seeds.update(a + BIAS for a in json.loads(args.survey.read_text())['funcs'])
    seeds.update(int(a, 16) for a in args.extra.split(',') if a)

    decode_base = lifter.ins

    def collect(address):
        """Block extent following QEMU/Unicorn's x86 translation-block rules, so
        block counts (and the deterministic scheduler) match the oracle: end at
        control transfers, after a REP string instruction, after loading
        DS/ES/SS/CS, after POPF/STI, and at 512 instructions or ~4 KiB."""
        out, pc = {}, address
        while lo <= pc < hi and len(out) < 512 and pc - address < 4064:
            i = lifter.ins(pc)
            if i is None:
                break
            out[pc] = i
            pc += i.size
            mn = i.mnemonic
            if mn.startswith(('j', 'loop')) or mn in ('call', 'ret', 'retf', 'iretd', 'int', 'int1', 'int3', 'into',
                                                      'hlt', 'ud2', 'popfd', 'popf', 'sti', 'lcall', 'ljmp'):
                break
            prefixes = set()
            for b in i.bytes:
                if b not in (0xf0, 0xf2, 0xf3, 0x66, 0x67, 0x26, 0x2e, 0x36, 0x3e, 0x64, 0x65):
                    break
                prefixes.add(b)
            if prefixes & {0xf2, 0xf3} and mn.split()[-1][:4] in ('movs', 'stos', 'lods', 'scas', 'cmps', 'ins', 'outs'):
                break
            if mn in ('lds', 'les', 'lss'):
                break
            if mn in ('mov', 'pop') and i.operands and i.operands[0].type == X.X86_OP_REG and \
                    i.reg_name(i.operands[0].reg) in ('ds', 'es', 'ss', 'cs'):
                break
        return out

    def decode_bytes(address, data):
        return {i.address: i for i in lifter.md.disasm(data, address)}

    def block_at(address):
        """Return the list of (variant bytes or None, decoded dict) for a block."""
        if address in observed:
            return [(bytes.fromhex(v), decode_bytes(address, bytes.fromhex(v))) for v in sorted(observed[address])]
        if address in SYNTHETIC:
            data = SYNTHETIC[address]
            return [(data, decode_bytes(address, data))]
        if not lo <= address < hi:
            return []
        return [(None, collect(address))]

    blocks, todo, outside = {}, sorted(seeds), set()
    while todo:
        a = todo.pop()
        if a in blocks:
            continue
        variants = block_at(a)
        if not variants:
            outside.add(a)
            continue
        blocks[a] = variants
        for _, decoded in variants:
            if not decoded:
                continue
            last = decoded[max(decoded)]
            nexts = successors(last)
            # A traced extent may end before a branch (segment load, POPFD).
            end = max(decoded) + last.size
            if last.mnemonic not in TERMINAL and not last.mnemonic.startswith(('j', 'loop', 'call')):
                nexts.append(end)
            for t in nexts:
                if t not in blocks and (lo <= t < hi or t in SYNTHETIC):
                    todo.append(t)
                elif not (lo <= t < hi or t in SYNTHETIC):
                    outside.add(t)

    # Generate C.
    names, sources = {}, []
    for a in sorted(blocks):
        for index, (data, decoded) in enumerate(blocks[a]):
            lifter.ins = (lambda d, base: (lambda pc: d.get(pc) if pc in d else base(pc)))(decoded, decode_base)
            saved = lifter.collect
            lifter.collect = lambda f, extra=(), d=decoded: d
            source, _ = lifter.lift_function(a)
            lifter.collect = saved
            lifter.ins = decode_base
            name = 'b_%08x_%d' % (a, index)
            sources.append((a, source.replace('f_%08x(' % a, name + '('), name, data))
            names.setdefault(a, []).append((name, data, len(decoded)))

    header = '#include "rt.h"\nextern uint32_t tp_seg[6], tp_base[8192];\n' \
             'void tp_set_segment(unsigned n, unsigned selector);\n' \
             'uint32_t tp_io(Ctx *c, int output, unsigned port, unsigned size, unsigned value);\n' \
             'void tp_interrupt(Ctx *c, uint32_t number);\n'
    files = []
    for k in range(0, len(sources), args.per_file):
        chunk = sources[k:k + args.per_file]
        path = out / ('blocks_%03d.c' % (k // args.per_file))
        text = header + ''.join(src.replace('void %s(' % name, 'void %s(' % name, 1) for _, src, name, _ in chunk)
        if not path.exists() or path.read_text() != text:
            path.write_text(text)
        files.append(path)

    # Dispatch: an O(1) table over the code object plus the synthetic host code.
    decls, entries, guarded = [], [], []
    for a, variants in sorted(names.items()):
        for name, _, _ in variants:
            decls.append('void %s(Ctx *c);' % name)
        count = min(255, max(n for _, _, n in variants))
        if len(variants) == 1:
            entries.append('{0x%xu, %s, %d},' % (a, variants[0][0], count))
        else:
            fn = 'g_%08x' % a
            body = ''.join('if (!memcmp(G_MEM + 0x%xu, "%s", %du)) { %s(c); return; }\n' % (
                a, ''.join('\\x%02x' % b for b in data), len(data), name) for name, data, _ in variants)
            guarded.append('static void %s(Ctx *c) {\n%s rt_unhandled(c, c->tc, "uncompiled self-modified code variant");\n}\n' % (fn, body))
            entries.append('{0x%xu, %s, %d},' % (a, fn, count))
    dispatch = header + '\n'.join(decls) + '\n' + ''.join(guarded) + '''
typedef void (*Block)(Ctx *);
static const struct { uint32_t address; Block fn; uint8_t insns; } blocks[] = {
%s
};
#define CODE_LO 0x%xu
#define CODE_HI 0x%xu
static Block table[CODE_HI - CODE_LO];
static uint8_t counts[CODE_HI - CODE_LO];
static Block synthetic[0x100];
/* Executed x86 instructions (per block, REP counted once): the host's CPU-speed governor. */
extern uint64_t tp_insns;
const uint32_t tp_block_count = %du;
void tp_dispatch_init(void) {
    for (unsigned i = 0; i < sizeof blocks / sizeof blocks[0]; i++) {
        uint32_t a = blocks[i].address;
        if (a >= CODE_LO && a < CODE_HI) table[a - CODE_LO] = blocks[i].fn, counts[a - CODE_LO] = blocks[i].insns;
        else if (a >= 0xf000 && a < 0xf100) synthetic[a - 0xf000] = blocks[i].fn;
    }
}
int tp_dispatch(Ctx *c) {
    uint32_t pc = c->tc;
    Block fn = pc >= CODE_LO && pc < CODE_HI ? table[pc - CODE_LO] : pc >= 0xf000 && pc < 0xf100 ? synthetic[pc - 0xf000] : 0;
    if (!fn) return 0;
    if (pc >= CODE_LO && pc < CODE_HI) tp_insns += counts[pc - CODE_LO];
    fn(c);
    return 1;
}
''' % ('\n'.join(entries), lo, hi, len(blocks))
    dpath = out / 'dispatch.c'
    dpath.write_text(dispatch)
    files.append(dpath)

    inc = str(args.reference.resolve() / 'recomp/runtime')

    def compile_one(path):
        obj = path.with_suffix('.o')
        if obj.exists() and obj.stat().st_mtime >= path.stat().st_mtime:
            return obj
        subprocess.run(['clang', '-arch', 'arm64', args.opt, '-c', '-fPIC', '-w', '-I', inc, str(path), '-o', str(obj)], check=True)
        return obj

    with concurrent.futures.ThreadPoolExecutor(os.cpu_count()) as pool:
        objects = list(pool.map(compile_one, files))
    runtime = out / 'runtime.o'
    subprocess.run(['clang', '-arch', 'arm64', args.opt, '-c', '-fPIC', '-w', '-I', inc, str(ROOT / 'runtime/verify.c'), '-o', str(runtime)], check=True)
    library = out / 'game.dylib'
    subprocess.run(['clang', '-arch', 'arm64', '-dynamiclib', *map(str, objects), str(runtime), '-o', str(library)], check=True)
    staged = out / 'game.a.new'
    subprocess.run(['libtool', '-static', '-o', str(staged), *map(str, objects)], check=True, capture_output=True)
    os.replace(staged, out / 'game.a')
    (out / 'build.json').write_text(json.dumps({'image_sha256': image.sha256,
        'library_sha256': hashlib.sha256(library.read_bytes()).hexdigest(), 'architecture': 'arm64'}, indent=2) + '\n')
    report = {'blocks': len(blocks), 'functions': len(sources), 'seeds': len(seeds),
              'traced_blocks': len(observed), 'guarded': len(guarded),
              'targets_outside_code': sorted(hex(t) for t in outside)[:50], 'outside_count': len(outside),
              'unsupported': dict(lifter.unsup.most_common()), 'internal_errors': lifter.internal[:20]}
    # Block extents (bytes per variant) for tools/spotcheck.py synchronisation.
    (out / 'blocks.json').write_text(json.dumps({str(a): sorted({max(d) + d[max(d)].size - a for _, d in v if d})
                                                 for a, v in blocks.items()}) + '\n')
    (out / 'lift.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k != 'targets_outside_code'}, indent=2))


if __name__ == '__main__':
    main()
