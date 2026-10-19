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
import re
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
    if mn in ('lcall', 'ljmp') and len(i.operands) == 2:
        return ([nxt] if mn == 'lcall' else []) + [i.operands[1].imm & 0xffffffff]
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
    ap.add_argument('--loaded-code', action='append', default=[],
                    help='ADDR:image.bin — code the game loads at run time (HMI drivers), dumped after loading')
    ap.add_argument('--drivers', type=Path, help='directory of drv-ADDR-MODE-NAME.bin driver images (see tools/dump_drivers.md)')
    ap.add_argument('--out', type=Path, default=Path('work/game-build'))
    ap.add_argument('--per-file', type=int, default=1500)
    ap.add_argument('--opt', default='-O1')
    ap.add_argument('--prefix', default='', help='symbol prefix for a second image in one binary (e.g. intro_)')
    args = ap.parse_args()
    out = args.out
    out.mkdir(parents=True, exist_ok=True)
    image = load_image(args.exe)
    lifter = create_lifter(args.reference.resolve(), image, out)
    lifter.illegal_seh = False
    code, = [o for o in image.objects if o.flags & 4]
    lo, hi = code.base, code.base + code.size
    # Run-time loaded code (drivers) at their deterministic load addresses.
    # (decode base, bytes); region_info[k] = (linear load address, entered
    # through its own code selector?). Selector-entered drivers see EIP as an
    # offset: they are decoded at a private virtual base and their address
    # literals are rewritten to offsets after lifting.
    regions, region_info = [], []
    if args.drivers:
        for f in sorted(args.drivers.glob('drv-*-*-*.bin')):
            _, at, mode, _ = f.stem.split('-', 3)
            args.loaded_code.append('%s:%s%s' % (at, f, ':cs' if mode == 'cs' else ''))
    for spec in args.loaded_code:
        at, path, *mode = spec.split(':')
        offset_space = mode == ['cs']
        base = 0x80000000 + len(regions) * 0x100000      # private decode space
        regions.append((base, Path(path).read_bytes()))
        # Optional <image>.mask: 0 marks bytes the driver rewrites at run time
        # (its own variables), excluded from the identity check.
        mask = Path(path + '.mask')
        region_info.append((int(at, 16), offset_space, mask.read_bytes() if mask.exists() else None))

    def region_of(a):
        for base, data in regions:
            if base <= a < base + len(data):
                return base, data
        return None

    def to_offsets(source):
        for k, (base, data) in enumerate(regions):
            # Selector-entered drivers see offsets; flat-entered ones linear addresses.
            origin = 0 if region_info[k][1] else region_info[k][0]
            source = re.sub(r'0x([0-9a-f]{8})u', lambda m, b=base, n=len(data), o=origin: '0x%xu' % (int(m.group(1), 16) - b + o)
                            if b <= int(m.group(1), 16) < b + n else m.group(0), source)
        return source
    lifter.patchable = lambda a: region_of(a) is not None

    def code_linear(a):
        r = region_of(a)
        if r:
            k = regions.index(r)
            return region_info[k][0] + a - r[0]
        return a
    lifter.code_linear = code_linear

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
    for base, data in regions:
        # Driver images are a few KiB with entry tables in unknown formats:
        # compile a block at every byte offset. Junk decodes trap only if run.
        seeds.update(base + k for k in range(len(data)) if data[k:k + 2] != b'\0\0')

    decode_base = lifter.ins

    region_cache = {}

    def region_ins(pc):
        r = region_of(pc)
        if not r:
            return None
        if pc not in region_cache:
            base, data = r
            region_cache[pc] = next(lifter.md.disasm(data[pc - base:pc - base + 16], pc, 1), None)
        return region_cache[pc]

    def collect(address):
        """Block extent following QEMU/Unicorn's x86 translation-block rules, so
        block counts (and the deterministic scheduler) match the oracle: end at
        control transfers, after a REP string instruction, after loading
        DS/ES/SS/CS, after POPF/STI, and at 512 instructions or ~4 KiB."""
        out, pc = {}, address
        inside = region_of(address)
        limit = 64 if inside else 512        # driver blocks: every offset is a seed; keep them short
        while (inside and region_of(pc) == inside or not inside and lo <= pc < hi) and len(out) < limit and pc - address < 4064:
            i = region_ins(pc) if inside else lifter.ins(pc)
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
        if not (lo <= address < hi or region_of(address)):
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
                inside = lo <= t < hi or t in SYNTHETIC or region_of(t)
                if t not in blocks and inside:
                    todo.append(t)
                elif not inside:
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
            # Every block must hand control on (set the next PC) or trap.
            if decoded and 'c->tc' not in source and 'rt_unhandled' not in source:
                if region_of(a):
                    continue        # a junk decode inside a driver image: left uncompiled (stops if reached)
                raise SystemExit('block %08x does not reach a successor' % a)
            name = '%sb_%08x_%d' % (args.prefix, a, index)
            sources.append((a, source.replace('f_%08x(' % a, name + '('), name, data))
            names.setdefault(a, []).append((name, data, len(decoded)))

    # Tunables: immediates the host may change at run time (runtime/qol.c, the free-mode levers).
    # Applied only where the instruction is exactly as expected; each becomes a variable defined in
    # dispatch.c with the game's value.
    TUNABLES = {0x167ff6: ('cmp eax, 0x320', 'b = 0x320u', 'tp_tune_max_people', 0x320)}   # visitors in the park (0x167cd4)
    applied = {}
    for i, (a, src, name, data) in enumerate(sources):
        for at, (asm, old, var, value) in TUNABLES.items():
            tag = '/* %08x: %s */' % (at, asm)
            if tag in src:
                lines = src.split('\n')
                lines = [l.replace(old, 'b = ' + var, 1) if tag in l else l for l in lines]
                src = '\n'.join(lines); applied[var] = value
        sources[i] = (a, src, name, data)
    # Map geometry (runtime/bigmap.c, docs/PLAN.md "Larger maps"): the instructions that encode the 64x64
    # park map (config/mapgeo.json) read variables instead: tp_map_w (row stride), tp_map_shift (its log2)
    # and tp_map_ext (the playable size). Each site is an immediate, a shift count or an appended statement.
    geo_path = Path(__file__).resolve().parent.parent / 'config' / 'mapgeo.json'
    if not args.prefix and geo_path.exists():
        geo = {int(a, 16): v for a, v in json.loads(geo_path.read_text())['sites'].items()}
        tag_re = re.compile(r'^\s*/\* ([0-9a-f]{8}): ')
        done, failed = set(), set()
        for i, (a, src, name, data) in enumerate(sources):
            if '/* 00' not in src:
                continue
            lines = src.split('\n'); changed = False
            for j, line in enumerate(lines):
                m = tag_re.match(line)
                if not m or int(m.group(1), 16) not in geo:
                    continue
                at = int(m.group(1), 16); site = geo[at]; new_line = line
                if 'append' in site:
                    new_line = line + site['append']
                elif 'disp' in site:            # a memory displacement: rows of 64 cells of 4 bytes
                    asm = line.split('*/')[0].split(': ', 1)[1]
                    d = int(re.search(r'\+ 0x([0-9a-f]+)\]', asm).group(1), 16)
                    if site['disp'] == 'abs':   # a cell off the map base: kept near its edge or the middle column
                        x, y, b = (d >> 2) % 64, (d >> 2) // 64, d & 3
                        ex = '%du' % x if x < 16 else '(tp_map_ext-%du)' % (64 - x) if x >= 48 else '(tp_map_ext/2u+%d)' % (x - 32)
                        ey = '%du' % y if y < 32 else '(tp_map_ext-%du)' % (64 - y)
                        expr = '((%s+%s*tp_map_w)*4u+%du)' % (ex, ey, b)
                    elif site['disp'] == 'col':  # x = 63 of a row
                        expr = '((tp_map_ext-1u)*4u+%du)' % (d - 0xfc)
                    elif d < 0x100:              # the next row, x - 1
                        expr = '(tp_map_w*4u-%du)' % (0x100 - d)
                    else:                        # a row near the bottom edge, from a row's start
                        expr = '((tp_map_ext-%du)*tp_map_w*4u+%du)' % (64 - (d >> 8), d & 0xff)
                    new_line = line.replace('+ 0x%xu' % d, '+ ' + expr, 1)
                elif site.get('old') == 'n = 6u':
                    new_line = line.replace('uint32_t n = 6u', 'uint32_t n = tp_map_shift', 1)
                else:
                    asm = line.split('*/')[0].split(': ', 1)[1]
                    hexv = site['old'][:-1] if 'old' in site else re.findall(r'0x[0-9a-f]+', asm)[-1]
                    for form in ('b = %su', '(uint32_t)(%su)', ', %su)', '(int32_t)%su', 'v = %su'):
                        if form % hexv in line:
                            new_line = line.replace(form % hexv, (form % hexv).replace(hexv + 'u', site['new']), 1); break
                if new_line != line:
                    lines[j] = new_line; done.add(at); changed = True
                else:
                    failed.add(at)
            if changed:
                sources[i] = (a, '\n'.join(lines), name, data)
        for v, val in (('tp_map_w', 64), ('tp_map_shift', 6), ('tp_map_ext', 64), ('tp_map_bytes', 0x4000),
                       ('tp_things_alloc', 1200), ('tp_things_n', 1200), ('tp_things_bytes', 0x3a980), ('tp_things_spare', 0x4e20),
                       ('tp_stay_ticks', 0xfa0), ('tp_stay_tired', 0x1f40), ('tp_menu_bar', 0x1b53b6)):
            applied[v] = val
        print('map geometry: %d of %d sites applied, %d not matched %s, %d not in the build' % (
            len(done), len(geo), len(failed - done), ' '.join('%x' % x for x in sorted(failed - done))[:300],
            len(set(geo) - done - failed)), file=sys.stderr)
    tunable_decls = ''.join('extern uint32_t %s;\n' % v for v in applied)
    header = '#include "rt.h"\nextern uint32_t tp_seg[6], tp_base[8192];\nextern uint64_t tp_insns, tp_rep;\n' + tunable_decls + \
             'void tp_set_segment(unsigned n, unsigned selector);\n' \
             'uint32_t tp_io(Ctx *c, int output, unsigned port, unsigned size, unsigned value);\n' \
             'void tp_interrupt(Ctx *c, uint32_t number);\n'
    files = []
    loaded = [x for x in sources if region_of(x[0])]
    for k in range(0, len(loaded), args.per_file):
        path = out / ('blocks_loaded_%03d.c' % (k // args.per_file))
        text = header + ''.join(to_offsets(src) for _, src, _, _ in loaded[k:k + args.per_file])
        if not path.exists() or path.read_text() != text:
            path.write_text(text)
        files.append(path)
    sources = [x for x in sources if not region_of(x[0])]
    for k in range(0, len(sources), args.per_file):
        chunk = sources[k:k + args.per_file]
        path = out / ('blocks_%03d.c' % (k // args.per_file))
        text = header + ''.join(src.replace('void %s(' % name, 'void %s(' % name, 1) for _, src, name, _ in chunk)
        if not path.exists() or path.read_text() != text:
            path.write_text(text)
        files.append(path)

    def region_code():
        """Blocks of run-time loaded code: a table per region, entered only
        while the bytes in memory match the image compiled here (checked on
        every entry from outside the region: drivers share load addresses)."""
        out = ['typedef void (*RBlock)(Ctx *);', 'static int current_region = -1;']
        for k, (base, data) in enumerate(regions):
            out.append('static const uint8_t region%d_image[%d] = {%s};' % (k, len(data), ','.join(str(b) for b in data)))
            out.append('static const struct { uint32_t off; RBlock fn; uint8_t insns; uint8_t guard; } region%d_blocks[] = {%s{0,0,0,0}};' % (k, ''.join(region_entries[k])))
            out.append('static RBlock region%d_table[%d]; static uint8_t region%d_counts[%d], region%d_guard[%d]; static int region%d_state;' % (k, len(data), k, len(data), k, len(data), k))
        out.append('static Block region_block(Ctx *c, uint32_t pc, uint32_t csbase) {')
        for k, (base, data) in enumerate(regions):
            linear, offset_space, _ = region_info[k]
            if offset_space:
                out.append('    if (csbase == 0x%xu && pc < %du) { uint32_t off = pc;' % (linear, len(data)))
            else:
                out.append('    if (!csbase && pc >= 0x%xu && pc < 0x%xu) { uint32_t off = pc - 0x%xu;' % (linear, linear + len(data), linear))
            # Identity: the driver header (the package and its driver files
            # are pinned by hash). Blocks over bytes the driver rewrites are
            # checked against the compiled bytes on every execution.
            out.append('        if (current_region != %d) {' % k)
            m = region_info[k][2] or bytes([1]) * len(data)
            out.append('            { static const uint8_t mask[%d] = {%s}; int n = %d;' % (len(data), ','.join(str(b) for b in m), min(32, len(data))))
            out.append('              for (int i = 0; i < n; i++) if (mask[i] && G_MEM[0x%xu + i] != region%d_image[i]) goto next%d; }' % (linear, k, k))
            out.append('            if (!region%d_state) { for (int i = 0; region%d_blocks[i].fn; i++) region%d_table[region%d_blocks[i].off] = region%d_blocks[i].fn, region%d_counts[region%d_blocks[i].off] = region%d_blocks[i].insns, region%d_guard[region%d_blocks[i].off] = region%d_blocks[i].guard; region%d_state = 1; }' % ((k,) * 12))
            out.append('            current_region = %d;' % k)
            out.append('        }')
            out.append('        if (region%d_guard[off] && memcmp(G_MEM + 0x%xu + off, region%d_image + off, region%d_guard[off])) return 0;' % (k, linear, k, k))
            out.append('        tp_insns += region%d_counts[off];' % k)
            out.append('        return region%d_table[off];' % k)
            out.append('    }')
            out.append('    next%d:;' % k)
        out.append('    return 0;\n}')
        out.append('static void region_reset(void) { current_region = -1;%s}' % ''.join(' region%d_state = 0;' % k for k in range(len(regions))))
        return '\n'.join(out)

    # Dispatch: an O(1) table over the code object plus the synthetic host code.
    decls, entries, guarded = [], [], []
    region_entries = [[] for _ in regions]
    for a, variants in sorted(names.items()):
        r = region_of(a)
        if r:
            k = regions.index(r)
            off = a - r[0]
            dec = blocks[a][0][1]
            length = max(dec) + dec[max(dec)].size - a if dec else 0
            m = region_info[k][2]
            # Blocks over bytes the driver rewrites (generated/patched code)
            # are checked against the compiled bytes on every execution.
            guard = length if m and any(m[off + j] == 0 for j in range(length) if off + j < len(m)) else 0
            region_entries[k].append('{0x%xu, %s, %d, %d},' % (off, variants[0][0], min(255, variants[0][2]), guard))
            decls.append('void %s(Ctx *c);' % variants[0][0])
            continue
        for name, _, _ in variants:
            decls.append('void %s(Ctx *c);' % name)
        count = min(255, max(n for _, _, n in variants))
        if len(variants) == 1:
            entries.append('{0x%xu, %s, %d},' % (a, variants[0][0], count))
        else:
            fn = '%sg_%08x' % (args.prefix, a)
            body = ''.join('if (!memcmp(G_MEM + 0x%xu, "%s", %du)) { %s(c); return; }\n' % (
                a, ''.join('\\x%02x' % b for b in data), len(data), name) for name, data, _ in variants)
            guarded.append('static void %s(Ctx *c) {\n%s rt_unhandled(c, c->tc, "uncompiled self-modified code variant");\n}\n' % (fn, body))
            entries.append('{0x%xu, %s, %d},' % (a, fn, count))
    dispatch = header + ''.join('uint32_t %s = 0x%xu;\n' % (v, val) for v, val in applied.items()) + '\n'.join(decls) + '\n' + ''.join(guarded) + '''
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
static void region_reset(void);
void tp_dispatch_init(void) {
    region_reset();
    for (unsigned i = 0; i < sizeof blocks / sizeof blocks[0]; i++) {
        uint32_t a = blocks[i].address;
        if (a >= CODE_LO && a < CODE_HI) table[a - CODE_LO] = blocks[i].fn, counts[a - CODE_LO] = blocks[i].insns;
        else if (a >= 0xf000 && a < 0xf100) synthetic[a - 0xf000] = blocks[i].fn;
    }
}
%s
int tp_dispatch(Ctx *c) {
    uint32_t pc = c->tc;
    Block fn = tp_base[tp_seg[0] >> 3] ? 0 : pc >= CODE_LO && pc < CODE_HI ? table[pc - CODE_LO] : pc >= 0xf000 && pc < 0xf100 ? synthetic[pc - 0xf000] : 0;
    if (fn) current_region = -1;
    else fn = region_block(c, pc, tp_base[tp_seg[0] >> 3]);
    if (!fn) return 0;
    if (current_region < 0 && pc >= CODE_LO && pc < CODE_HI) tp_insns += counts[pc - CODE_LO];
    fn(c);
    return 1;
}
''' % ('\n'.join(entries), lo, hi, len(blocks), region_code())
    if args.prefix:
        for sym in ('tp_dispatch_init', 'tp_dispatch', 'tp_block_count'):
            dispatch = re.sub(r'\b%s\b' % sym, args.prefix + sym[3:], dispatch)
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
    if not args.prefix:    # a prefixed image is linked into the app beside the main one
        subprocess.run(['clang', '-arch', 'arm64', '-dynamiclib', *map(str, objects), str(runtime), '-o', str(library)], check=True)
    staged = out / 'game.a.new'
    subprocess.run(['libtool', '-static', '-o', str(staged), *map(str, objects)], check=True, capture_output=True)
    os.replace(staged, out / 'game.a')
    if not args.prefix:
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
