#!/usr/bin/env python3
"""Bounded, headless development oracle. Does not launch an emulator UI."""
import argparse
import json
import time
from pathlib import Path
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_BLOCK, UC_HOOK_INTR, UC_HOOK_INSN, UC_HOOK_MEM_WRITE, UcError
from unicorn import x86_const as U
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from dos import DOS, Stop, MEMORY, GDT, load_image, scratch_profile


class Original:
    def __init__(self):
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self.uc.mem_map(0, MEMORY)
        self.uc.reg_write(U.UC_X86_REG_GDTR, (0, GDT, 4095, 0))
        self.budget=None
        self.blocks_run=0
        self.interrupt_number=0
        self.repeat_pc=None
        self.block_cache={}
        self.decoder=Cs(CS_ARCH_X86,CS_MODE_32)
        self.uc.hook_add(UC_HOOK_BLOCK,self.on_block)
        self.uc.hook_add(UC_HOOK_INTR,self.on_interrupt)

    def on_interrupt(self,uc,number,_):
        if self.budget is not None:
            self.interrupt_number=number
            uc.emu_stop()

    def on_block(self,uc,pc,size,_):
        if self.budget is None:return
        # Unicorn re-enters a REP block for individual iterations; the compiled
        # block executes the complete string operation before yielding.
        if pc==self.repeat_pc:return
        if self.blocks_run>=self.budget:
            uc.emu_stop();return
        self.blocks_run+=1
        key=(pc,size)
        if key not in self.block_cache:
            ins=list(self.decoder.disasm(bytes(uc.mem_read(pc,size)),pc))
            last=ins[-1] if ins else None
            self.block_cache[key]=(last.address if last and last.bytes[-1] in (0xa4,0xa5,0xa6,0xa7,0xaa,0xab,0xac,0xad,0xae,0xaf) and any(b in (0xf2,0xf3) for b in last.bytes[:-1]) else None)
        self.repeat_pc=self.block_cache[key]

    def run_blocks(self,budget):
        self.budget=budget;self.blocks_run=0;self.interrupt_number=0
        self.uc.emu_start(self.get('eip'),0,timeout=10000000,count=10000000)
        self.budget=None
        return self.interrupt_number

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
    ap.add_argument('--mouse',default='',help='poll:x:y:buttons events in DOS coordinates')
    ap.add_argument('--clicks',default='',help='poll:x:y clicks, released after 50000 compiled blocks')
    ap.add_argument('--block-clicks',default='',help='block:x:y clicks independent of keyboard polling')
    ap.add_argument('--quantum',type=int,default=0)
    ap.add_argument('--blocks',type=int,default=2000000)
    ap.add_argument('--seconds',type=float,default=20)
    args = ap.parse_args()
    if args.instructions<=0 or args.blocks<=0 or args.seconds<=0 or args.quantum<0:ap.error('invalid run bound')
    if args.block_clicks and not args.quantum:ap.error('--block-clicks requires --quantum')
    args.out.mkdir(parents=True, exist_ok=True)
    image = load_image(args.exe)
    m = Original()
    host = DOS(m, args.exe.parent, scratch_profile(args.out))
    host.bootstrap(image)
    host.key_script = [(int(p),int(k,16)) for p,k in (s.split(':') for s in args.keys.split(',') if s)]
    host.mouse_script = [tuple(map(int,s.split(':'))) for s in args.mouse.split(',') if s]
    host.click_script = [tuple(map(int,s.split(':'))) for s in args.clicks.split(',') if s]
    host.block_clicks = [tuple(map(int,s.split(':'))) for s in args.block_clicks.split(',') if s]
    if args.clicks and not args.quantum:ap.error('--clicks requires --quantum')
    blocks = set()
    variants = {}
    seen = set()
    epoch = [0]
    code = [o for o in image.objects if o.flags&4][0]
    def code_write(uc,access,address,size,value,_):
        epoch[0] += 1
        m.block_cache.clear()
    m.uc.hook_add(UC_HOOK_MEM_WRITE,code_write,None,code.base,code.base+code.size-1)
    def block(uc,pc,size,_):
        blocks.add(pc)
        key = (pc,size,epoch[0])
        if key in seen: return
        seen.add(key)
        variants.setdefault(pc,set()).add(bytes(uc.mem_read(pc,size)).hex())
    m.uc.hook_add(UC_HOOK_BLOCK, block)
    if not args.quantum:
        m.uc.hook_add(UC_HOOK_INTR, lambda uc, number, _: host.interrupt(number))
    m.uc.hook_add(UC_HOOK_INSN, lambda uc,port,size,_: host.port_in(port,size), None, 1, 0, U.UC_X86_INS_IN)
    m.uc.hook_add(UC_HOOK_INSN, lambda uc,port,size,value,_: host.port_out(port,size,value), None, 1, 0, U.UC_X86_INS_OUT)
    status = 'instruction or time bound'
    try:
        if args.quantum:
            status='block or time bound'
            m.set('eip',image.entry)
            start=time.monotonic()
            while host.blocks<args.blocks and time.monotonic()-start<args.seconds:
                number=m.run_blocks(min(args.quantum,args.blocks-host.blocks))
                if number:host.interrupt(number)
                host.advance_blocks(m.blocks_run)
        else:
            m.uc.emu_start(image.entry, 0, timeout=int(args.seconds*1000000), count=args.instructions)
    except (Stop, UcError) as e:
        status = str(e)
    report = {'status': status, 'pc': m.get('eip'), 'blocks': sorted(blocks), 'services': host.events,
              'output': host.output.decode('cp437'), 'exit_code': host.exit_code, 'io':host.io,
              'frame':host.frame(args.out/'frame.png'),
              'variants':{str(pc):sorted(v) for pc,v in variants.items()}}
    report['keys'] = args.keys
    report['mouse'] = args.mouse
    report['clicks'],report['quantum'],report['executed_blocks'] = args.clicks,args.quantum,host.blocks
    report['block_clicks']=args.block_clicks
    report['mouse_events'] = host.mouse_events
    report['polls'],report['timer_irqs'] = host.poll_count,host.irq_count
    host.close()
    report['scratch'],report['saved_files'] = str(host.scratch),host.saved_files()
    (args.out / 'trace.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({**report, 'blocks': len(blocks), 'services': host.events[-8:], 'io':host.io[-8:],
                     'variants':sum(len(v)>1 for v in variants.values())}, indent=2))


if __name__ == '__main__':
    main()
