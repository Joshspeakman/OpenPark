#!/usr/bin/env python3
"""Bounded, headless development oracle. Does not launch an emulator UI."""
import argparse
import json
from pathlib import Path
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_BLOCK, UC_HOOK_INTR, UC_HOOK_INSN, UC_HOOK_MEM_WRITE, UcError
from unicorn import x86_const as U
from dos import DOS, Stop, MEMORY, GDT, load_image


class Original:
    def __init__(self):
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self.uc.mem_map(0, MEMORY)
        self.uc.reg_write(U.UC_X86_REG_GDTR, (0, GDT, 65535, 0))

    def get(self, name):
        return self.uc.reg_read(getattr(U, 'UC_X86_REG_' + name.upper()))

    def set(self, name, value):
        self.uc.reg_write(getattr(U, 'UC_X86_REG_' + name.upper()), value)

    def read(self, address, size):
        return bytes(self.uc.mem_read(address, size))

    def write(self, address, data):
        self.uc.mem_write(address, data)

    def segments(self, values):
        for name, value in values.items():
            self.set(name, value)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('exe', type=Path)
    ap.add_argument('--out', type=Path, default=Path('work/startup'))
    ap.add_argument('--instructions', type=int, default=5000000)
    ap.add_argument('--keys',default='',help='deterministic poll:hex-scan/ASCII pairs, e.g. 60:1c0d')
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    image = load_image(args.exe)
    m = Original()
    host = DOS(m, args.exe.parent, args.out / 'scratch')
    host.bootstrap(image)
    host.key_script = [(int(p),int(k,16)) for p,k in (s.split(':') for s in args.keys.split(',') if s)]
    blocks = set()
    variants = {}
    seen = set()
    epoch = [0]
    code = [o for o in image.objects if o.flags&4][0]
    def code_write(uc,access,address,size,value,_):
        epoch[0] += 1
    m.uc.hook_add(UC_HOOK_MEM_WRITE,code_write,None,code.base,code.base+code.size-1)
    def block(uc,pc,size,_):
        blocks.add(pc)
        key = (pc,size,epoch[0])
        if key in seen: return
        seen.add(key)
        variants.setdefault(pc,set()).add(bytes(uc.mem_read(pc,size)).hex())
    m.uc.hook_add(UC_HOOK_BLOCK, block)
    m.uc.hook_add(UC_HOOK_INTR, lambda uc, number, _: host.interrupt(number))
    m.uc.hook_add(UC_HOOK_INSN, lambda uc,port,size,_: host.port_in(port,size), None, 1, 0, U.UC_X86_INS_IN)
    m.uc.hook_add(UC_HOOK_INSN, lambda uc,port,size,value,_: host.port_out(port,size,value), None, 1, 0, U.UC_X86_INS_OUT)
    status = 'instruction or time bound'
    try:
        m.uc.emu_start(image.entry, 0, timeout=20000000, count=args.instructions)
    except (Stop, UcError) as e:
        status = str(e)
    report = {'status': status, 'pc': m.get('eip'), 'blocks': sorted(blocks), 'services': host.events,
              'output': host.output.decode('cp437'), 'exit_code': host.exit_code, 'io':host.io,
              'frame':host.frame(args.out/'frame.png'),
              'variants':{str(pc):sorted(v) for pc,v in variants.items()}}
    report['keys'] = args.keys
    report['polls'],report['timer_irqs'] = host.poll_count,host.irq_count
    (args.out / 'trace.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({**report, 'blocks': len(blocks), 'services': host.events[-8:], 'io':host.io[-8:],
                     'variants':sum(len(v)>1 for v in variants.values())}, indent=2))


if __name__ == '__main__':
    main()
