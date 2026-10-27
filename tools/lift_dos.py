"""DOS adaptation of the pinned shared static lifter; no runtime decoding."""
import hashlib
import importlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SEGS = ('cs', 'ds', 'es', 'ss', 'fs', 'gs')


def create_lifter(reference, image, output):
    pins = json.loads((ROOT / 'config/reference.json').read_text())
    for name, sha in pins['files'].items():
        if hashlib.sha256((reference / name).read_bytes()).hexdigest() != sha:
            raise ValueError('reference changed: ' + name)
    sys.dont_write_bytecode = True
    sys.path.insert(0, str(reference / 'recomp/tools'))
    lift = importlib.import_module('lift')
    image.imports = {}
    image.u32 = lambda va: int.from_bytes(image.bytes_at(va, 4), 'little')
    code, = [o for o in image.objects if o.flags & 4]
    lift.PE = lambda _: image
    lift.code_range = lambda _: (code.base, code.base + code.size, bytes(code.data))
    analysis = output / 'analysis.json'
    analysis.write_text('{"funcs": []}')

    class DOSLifter(lift.Lifter):
        guest_flow = True
        patchable = staticmethod(lambda address: False)
        code_linear = staticmethod(lambda address: address)
        hooks, overrides = {}, {}

        def collect(self, f, extra=()):
            result = {}
            pc = f
            for _ in range(512):
                i = self.ins(pc)
                if i is None:
                    break
                result[pc] = i
                pc += i.size
                if i.mnemonic.startswith(('j', 'loop')) or i.mnemonic in ('call', 'ret', 'retf', 'int', 'iretd', 'hlt', 'ud2'):
                    break
            return result

        def rreg(self, n):
            return 'tp_seg[%d]' % SEGS.index(n) if n in SEGS else super().rreg(n)

        def wreg(self, n, v):
            if n in SEGS:
                return 'tp_set_segment(%du, %s);' % (SEGS.index(n), v)
            return super().wreg(n, v)

        def addr(self, i, op, lea=False):
            # The generic emitter's flat FS special case is replaced below.
            m = op.mem
            parts = []
            names = [i.reg_name(r) for r in (m.base, m.index) if r]
            if names and all(n in lift.R16 for n in names):
                # 16-bit effective address (67h prefix): wraps at 64 KiB.
                terms = ['(%s & 0xffffu)' % lift.R16[n] for n in names] + ['0x%xu' % (m.disp & 0xffff)]
                offset = '((%s) & 0xffffu)' % ' + '.join(terms)
                if lea:
                    return offset
                seg = i.reg_name(m.segment) if m.segment else ('ss' if 'bp' in names else 'ds')
                return '(tp_base[tp_seg[%d] >> 3] + %s)' % (SEGS.index(seg), offset)
            for reg, scale in ((m.base, 1), (m.index, m.scale)):
                if reg:
                    name = i.reg_name(reg)
                    if name not in lift.R32:
                        raise lift.Unsupported('16-bit address')
                    parts.append('%s * %du' % (name, scale))
            parts.append('0x%xu' % (m.disp & 0xffffffff))
            offset = '(uint32_t)(%s)' % ' + '.join(parts)
            if lea:
                return offset
            seg = i.reg_name(m.segment) if m.segment else ('ss' if i.reg_name(m.base) in ('ebp', 'esp') else 'ds')
            return '(tp_base[tp_seg[%d] >> 3] + %s)' % (SEGS.index(seg), offset)

        def goto(self, target, f, insns, callees):
            return 'c->tc = 0x%xu; SPILL; return;' % target

        def falls_through(self, i):
            if i.mnemonic in ('int', 'retf', 'iretd'):
                return False
            if i.mnemonic in ('cli', 'sti', 'in', 'out', 'lds', 'les', 'lfs', 'lgs', 'lss'):
                return True
            return super().falls_through(i)

        def emit(self, i, f, insns, callees):
            mn, ops = i.mnemonic, i.operands
            strings = ('movsb','movsw','movsd','stosb','stosw','stosd','lodsb','lodsw','lodsd','scasb','scasw','scasd','cmpsb','cmpsw','cmpsd')
            prefixes = []
            for byte in i.bytes:
                if byte not in (0xf0,0xf2,0xf3,0x66,0x67,0x26,0x2e,0x36,0x3e,0x64,0x65): break
                prefixes.append(byte)
            # INS/OUTS (port strings, REP or one): the game reads the whole VGA palette back with REP INSB from
            # 3C9h (0x187ed4, before a screen's fade: the year-end screens); unlifted, it ended the game.
            if mn.split()[-1] in ('insb', 'insw', 'insd', 'outsb', 'outsw', 'outsd'):
                base = mn.split()[-1]; k = {'b': 1, 'w': 2, 'd': 4}[base[-1]]
                if i.addr_size != 4 or any(p in prefixes for p in (0x26, 0x2e, 0x36, 0x64, 0x65)):
                    raise lift.Unsupported('unimplemented port string address/segment override')
                if base.startswith('ins'):
                    one = 'SPILL; { uint32_t v = tp_io(c, 0, edx & 0xffffu, %du, 0); RELOAD; W%d(edi, v); } edi += d;' % (k, k * 8)
                else:
                    one = '{ uint32_t v = R%d(esi); SPILL; tp_io(c, 1, edx & 0xffffu, %du, v); RELOAD; } esi += d;' % (k * 8, k)
                body = 'while (ecx) { %s ecx--; }' % one if any(p in prefixes for p in (0xf2, 0xf3)) else one
                return '{ uint32_t d = (f.df ? (uint32_t)-%d : %du); %s }' % (k, k, body)
            # LSL (the C runtime's heap growth, 0x190138/0x19030b: the limit of DS): every descriptor the host makes is
            # 4 GiB, page granular (host.c), so the limit is all ones and ZF is set (a selector of 0: ZF clear).
            if mn == 'lsl' and len(ops) == 2 and ops[0].size == 4:
                return '{ uint32_t s = %s & 0xfffcu; if (s) { %s } flags_set(&f, s ? 0x40u : 0u, 0x40u); }' % (
                    self.rd(i, ops[1]), self.wr(i, ops[0], '0xffffffffu'))
            if mn.split()[-1] in strings:
                if i.addr_size!=4 or any(p in prefixes for p in (0x26,0x2e,0x36,0x64,0x65)):
                    raise lift.Unsupported('unimplemented string address/segment override')
            if mn in strings and any(p in prefixes for p in (0xf2,0xf3)):
                # Capstone omits REPNE in the mnemonic for F2 MOVS/STOS,
                # although the target CPU repeats these Watcom instructions.
                return self.string(mn,'repne' if 0xf2 in prefixes else 'rep')
            # Far CALL/JMP through memory (FF /3, FF /5): Capstone reports a
            # near 4-byte operand. Load selector:offset; CALL pushes CS then EIP.
            k = len(prefixes)
            if mn in ('call', 'jmp') and i.bytes[k] == 0xff and len(i.bytes) > k + 1 and (i.bytes[k + 1] >> 3) & 7 in (3, 5):
                if 0x66 in prefixes:
                    raise lift.Unsupported('16-bit far transfer')
                p = self.addr(i, ops[0])
                push = 'esp -= 4; W32(esp, tp_seg[0]); esp -= 4; W32(esp, 0x%xu); ' % (i.address + i.size) if mn == 'call' else ''
                return '{ uint32_t p = %s; uint32_t off = R32(p), sel = R16(p + 4); %stp_set_segment(0, sel); c->tc = off; SPILL; return; }' % (p, push)
            if mn in ('lcall', 'ljmp') and len(ops) == 2:
                push = 'esp -= 4; W32(esp, tp_seg[0]); esp -= 4; W32(esp, 0x%xu); ' % (i.address + i.size) if mn == 'lcall' else ''
                return '{ %stp_set_segment(0, 0x%xu); c->tc = 0x%xu; SPILL; return; }' % (push, ops[0].imm & 0xffff, ops[1].imm & 0xffffffff)
            # HMI drivers patch their own selector placeholders (mov r16,
            # 0FFFFh) at run time: read such immediates from memory.
            if mn == 'mov' and len(ops) == 2 and ops[1].type == lift.X.X86_OP_IMM and ops[0].size == 2 \
                    and ops[1].imm & 0xffff == 0xffff and self.patchable(i.address):
                return self.wr(i, ops[0], 'R16(0x%xu)' % self.code_linear(i.address + i.size - 2))
            if mn == 'pushaw':
                regs = ('eax', 'ecx', 'edx', 'ebx', 'esp', 'ebp', 'esi', 'edi')
                body = ' '.join('W16(t - %d, %s);' % (2 * (k + 1), r) for k, r in enumerate(regs))
                return '{ uint32_t t = esp; %s esp -= 16; }' % body
            if mn == 'popaw':
                regs = ('edi', 'esi', 'ebp', None, 'ebx', 'edx', 'ecx', 'eax')
                body = ' '.join('%s = (%s & 0xffff0000u) | R16(esp + %d);' % (r, r, 2 * k) for k, r in enumerate(regs) if r)
                return '{ %s esp += 16; }' % body
            if mn == 'int':
                return 'c->tc = 0x%xu; SPILL; tp_interrupt(c, %du); return;' % (i.address + i.size, ops[0].imm)
            if mn in ('sti', 'cli'):
                return 'flags_set(&f, %s, F_IF);' % ('F_IF' if mn == 'sti' else '0')
            if mn=='popfd':
                return '{ uint32_t v = R32S(esp); esp += 4; flags_set(&f,v,0xed5u); }'
            if mn == 'out':
                return 'SPILL; tp_io(c, 1, %s, %du, %s); RELOAD;' % (self.rd(i,ops[0]),ops[1].size,self.rd(i,ops[1]))
            if mn == 'in':
                return '{ SPILL; uint32_t v = tp_io(c, 0, %s, %du, 0); RELOAD; %s }' % (self.rd(i,ops[1]),ops[0].size,self.wr(i,ops[0],'v'))
            if mn in ('iretd','retf'):
                if 0x66 in i.prefix:
                    raise lift.Unsupported('16-bit far return')
                adjust = ops[0].imm if ops else 0
                tail = 'flags_set(&f,R32S(esp+8),0xffffffffu);' if mn=='iretd' else ''
                return 'c->tc = R32S(esp); tp_set_segment(0,R32S(esp+4)); %s esp += %du; SPILL; return;' % (tail,(12 if mn=='iretd' else 8)+adjust)
            if mn in ('push', 'pop') and ops[0].type == lift.X.X86_OP_REG and i.reg_name(ops[0].reg) in SEGS:
                name = i.reg_name(ops[0].reg)
                size = 2 if 0x66 in i.prefix else 4
                if mn == 'push':
                    return 'esp -= %du; W%d(esp, %s);' % (size, size * 8, self.rreg(name))
                return '{ uint32_t v = R16(esp); esp += %du; %s }' % (size, self.wreg(name, 'v'))
            if mn in ('lds', 'les', 'lfs', 'lgs', 'lss'):
                return '{ uint32_t p = %s; uint32_t v = R32(p); uint32_t s = R16(p + 4); %s %s }' % (self.addr(i, ops[1]), self.wr(i, ops[0], 'v'), self.wreg(mn[1:], 's'))
            return super().emit(i, f, insns, callees)

        def string(self, mn, rep):
            size = {'b': 1, 'w': 2, 'd': 4}[mn[-1]]
            bits = size * 8
            read, write = 'R%d' % bits, 'W%d' % bits
            src = '(tp_base[tp_seg[1] >> 3] + esi)'
            dst = '(tp_base[tp_seg[2] >> 3] + edi)'
            acc = self.rreg({1: 'al', 2: 'ax', 4: 'eax'}[size])
            op = mn[:-1]
            if op == 'movs':
                body = '%s(%s, %s(%s)); esi += d; edi += d;' % (write,dst,read,src)
            elif op == 'stos':
                body = '%s(%s, %s); edi += d;' % (write,dst,acc)
            elif op == 'lods':
                body = self.wreg({1:'al',2:'ax',4:'eax'}[size], '%s(%s)' % (read,src)) + ' esi += d;'
            else:
                left = acc if op == 'scas' else '%s(%s)' % (read,src)
                body = '{ uint32_t a = %s, b = %s(%s); LF(LF_SUB, %d, a, b, (a - b) & %s); } edi += d;' % (left,read,dst,lift.SZ[size],lift.MASK[size])
                if op == 'cmps':
                    body += ' esi += d;'
            if rep:
                # DOSBox's normal core (GOG's cycles=fixed 10000) charges a cycle per
                # repetition of MOVS/STOS/LODS (not SCAS/CMPS: core_normal/string.h);
                # the governor counts them the same way (tp_insns, tp_rep).
                if op in ('scas','cmps'):
                    stop = 'if (%s) break;' % ('CC_E' if rep == 'repne' else '!CC_E')
                    body = 'while (ecx) { %s ecx--; %s }' % (body,stop)
                else:
                    body = 'tp_insns += ecx; tp_rep += ecx; while (ecx) { %s ecx--; }' % body
            return '{ uint32_t d = f.df ? (uint32_t)-%d : %du; %s }' % (size,size,body)

    result = DOSLifter('', str(analysis))
    result.jt_cache = {}
    return result
