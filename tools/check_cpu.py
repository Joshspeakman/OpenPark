#!/usr/bin/env python3
"""Synthetic DOS instruction regression corpus, compared with original x86."""
import argparse
import json
import random
from pathlib import Path
from types import SimpleNamespace
from unicorn import UC_HOOK_INTR
from dos import DOS, REGS, SEGMENTS, GDT, descriptor
from le import Object
from trace_startup import Original
from verify_startup import Native, build


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--reference',required=True,type=Path)
    ap.add_argument('--out',type=Path,default=Path('work/cpu-check'))
    args=ap.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    # F2 A5 is intentionally included: Capstone 5 omits its repeat prefix
    # from both mnemonic and detail.prefix, while x86 still repeats it.
    sequences=['f2a5','f3a5','f2a4','66f2a5','b8100034128ec00607','9cfa9d']
    code=bytearray(4096)
    variants={}
    for n,seq in enumerate(sequences):
        data=bytes.fromhex(seq+'cdf1')
        pc=0x110000+n*64;code[n*64:n*64+len(data)]=data
        variants[str(pc)]=[data.hex()]
    callback=bytes.fromhex('a300000700891d04000700890d0800070089150c000700cb')
    code[512:512+len(callback)]=callback
    code[576:578]=bytes.fromhex('cdf1')
    variants[str(0x110200)]=[callback.hex()]
    variants[str(0x110240)]=['cdf1']
    variants[str(0xf010)]=['0fa90fa1071f619dc3']
    image=SimpleNamespace(objects=[Object(len(code),0x110000,4,1,1,code)],entry=0x110000,stack=0x200000,
                          bytes_at=lambda va,n:bytes(code[va-0x110000:va-0x110000+n]))
    library=build(image,args.reference.resolve(),args.out,set(map(int,variants)),variants)
    native,original=Native(library),Original()
    original.uc.hook_add(UC_HOOK_INTR,lambda uc,num,_:uc.emu_stop())
    game=args.out/'empty-game';game.mkdir(exist_ok=True)
    hosts=[DOS(m,game,args.out/name) for m,name in ((native,'native'),(original,'original'))]
    rng=random.Random(0x54504b)
    cases=0
    for n,seq in enumerate(sequences):
        for df in (0,1):
            for count in (0,1,2,17,63):
                for overlap in (False,True):
                    native.lib.tp_reset()
                    pattern=bytes(rng.getrandbits(8) for _ in range(8192))
                    for host in hosts:
                        host.bootstrap(image)
                        host.bases.update({0x100:0x30000,0x108:0x40000})
                        m=host.m
                        for sel,base in host.bases.items():
                            m.write(GDT+sel,descriptor(base,access=0x9a if sel==8 else 0x92))
                        m.write(0x30000,pattern);m.write(0x40000,pattern[::-1])
                        m.set('ds',0x100);m.set('es',0x100 if overlap else 0x108)
                        m.set('eip',0x110000+n*64);m.set('ecx',count)
                        m.set('esi',512);m.set('edi',516);m.set('eax',0x12345678)
                        m.set('eflags',0x203|(df<<10))
                    for sel,base in hosts[0].bases.items():native.lib.tp_descriptor(sel,base)
                    result=native.lib.tp_run(10)
                    if result!=0xf1:raise AssertionError(native.lib.tp_error())
                    original.uc.emu_start(original.get('eip'),0,count=1000)
                    for reg in REGS+SEGMENTS+('eip',):
                        if native.get(reg)!=original.get(reg):raise AssertionError((seq,count,df,overlap,reg))
                    if (native.get('eflags')^original.get('eflags'))&0xed5:raise AssertionError('flags')
                    for at,size in ((0x30000,8192),(0x40000,8192),(image.stack-64,64)):
                        if native.read(at,size)!=original.read(at,size):raise AssertionError((seq,count,df,overlap,'memory'))
                    cases+=1
    for df in (0,1):
        for buttons,event in ((1,3),(0,5)):
            native.lib.tp_reset()
            saved=None
            for host in hosts:
                host.bootstrap(image)
                m=host.m
                for index,reg in enumerate(REGS):m.set(reg,0x12340000+index)
                m.set('esp',image.stack);m.set('eip',0x110240);m.set('eflags',0x203|(df<<10))
                host.mouse.update(callback=(127,8,0x110200),buttons=1-buttons,x=320,y=200)
                host.poll_count=0;host.mouse_script=[(1,564,316,buttons)]
                saved=[m.get(reg) for reg in REGS+SEGMENTS]
                host.poll_keyboard();host.mouse_callback()
            for sel,base in hosts[0].bases.items():native.lib.tp_descriptor(sel,base)
            if native.lib.tp_run(10)!=0xf1:raise AssertionError(native.lib.tp_error())
            original.uc.emu_start(original.get('eip'),0,count=1000)
            for m in (native,original):
                if [m.get(reg) for reg in REGS+SEGMENTS]!=saved:raise AssertionError('callback corrupted caller')
                if (m.get('eflags')^ (0x203|(df<<10)))&0xed5:raise AssertionError('callback corrupted flags')
            if native.read(0x70000,16)!=original.read(0x70000,16):raise AssertionError('callback arguments')
            import struct
            if struct.unpack('<4I',native.read(0x70000,16))!=(event,buttons,564,316):raise AssertionError('wrong mouse event')
            cases+=1
    result={'cases':cases,'mismatches':0,'forms':sequences,'covers':['F2/F3 repeat','zero count','forward/reverse','overlap','non-flat DS/ES','segment load and push/pop','POPF restores IF after CLI','far mouse callback arguments and complete caller restoration']}
    (args.out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
