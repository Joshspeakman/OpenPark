#!/usr/bin/env python3
"""Run already-compiled game blocks without importing Unicorn or Capstone.

This is a bounded development host, not the final Cocoa platform layer.
"""
import argparse
import ctypes
import hashlib
import json
import sys
import time
from pathlib import Path
from dos import DOS, Stop, load_image, scratch_profile
from native_host import Native
import toolchain


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('exe',type=Path)
    ap.add_argument('--library',type=Path,required=True)
    ap.add_argument('--out',type=Path,default=Path('work/native-run'))
    ap.add_argument('--keys',default='')
    ap.add_argument('--mouse',default='')
    ap.add_argument('--clicks',default='')
    ap.add_argument('--block-clicks',default='')
    ap.add_argument('--blocks',type=int,default=0)
    ap.add_argument('--quantum',type=int,default=0)
    ap.add_argument('--services',type=int,default=13000)
    ap.add_argument('--seconds',type=float,default=30)
    args=ap.parse_args()
    if args.seconds<=0 or args.services<=0:ap.error('bounds must be positive')
    if args.blocks<0 or args.quantum<0:ap.error('block bounds cannot be negative')
    if (args.blocks or args.block_clicks) and not args.quantum:ap.error('block scheduling requires --quantum')
    if args.clicks and not args.quantum:ap.error('--clicks requires --quantum')
    image=load_image(args.exe)
    manifest=json.loads((args.library.parent/'build.json').read_text())
    if manifest['image_sha256']!=image.sha256 or manifest['library_sha256']!=hashlib.sha256(args.library.read_bytes()).hexdigest():
        raise SystemExit('compiled library does not match its manifest/input')
    args.out.mkdir(parents=True,exist_ok=True)
    m=Native(args.library)
    host=DOS(m,args.exe.parent,scratch_profile(args.out))
    host.bootstrap(image);m.set('eip',image.entry)
    host.key_script=[(int(p),int(k,16)) for p,k in (s.split(':') for s in args.keys.split(',') if s)]
    host.mouse_script=[tuple(map(int,s.split(':'))) for s in args.mouse.split(',') if s]
    host.click_script=[tuple(map(int,s.split(':'))) for s in args.clicks.split(',') if s]
    host.block_clicks=[tuple(map(int,s.split(':'))) for s in args.block_clicks.split(',') if s]
    errors=[]
    @ctypes.CFUNCTYPE(ctypes.c_int,ctypes.c_int,ctypes.c_uint,ctypes.c_uint,ctypes.c_uint)
    def io(output,port,size,value):
        try:
            if output:host.port_out(port,size,value);return 0
            return host.port_in(port,size)
        except Stop as e:errors.append(str(e));return -1
    m.lib.tp_io_callback(io)
    for sel,base in host.bases.items():m.lib.tp_descriptor(sel,base)
    status='block bound' if args.blocks else 'service bound';count=0;start=time.monotonic()
    while (host.blocks<args.blocks if args.blocks else count<args.services):
        if time.monotonic()-start>=args.seconds:status='wall-clock bound';break
        budget=min(args.quantum,args.blocks-host.blocks) if args.blocks else args.quantum or 1000000
        number=m.lib.tp_run(budget)
        if number<0 or number==0 and not args.quantum:
            status='native %d at %08x: %s' % (number,m.get('eip'),m.lib.tp_error().decode());break
        if number:count+=1
        try:
            if number:host.interrupt(number)
            if args.quantum:host.advance_blocks(m.lib.tp_blocks())
        except Stop as e:status=str(e);break
        for sel,base in host.bases.items():m.lib.tp_descriptor(sel,base)
    frame=host.frame(args.out/'frame.png')
    host.close()
    result={'status':status,'services':count,'elapsed_seconds':round(time.monotonic()-start,3),
            'pc':m.get('eip'),'frame':frame,'polls':host.poll_count,'timer_irqs':host.irq_count,
            'cpu':'statically compiled '+toolchain.architecture(),'oracle_imported':'unicorn' in sys.modules,
            'disassembler_imported':'capstone' in sys.modules,'io_errors':errors}
    result['scratch'],result['saved_files']=str(host.scratch),host.saved_files()
    result['executed_blocks'],result['mouse_events']=host.blocks,host.mouse_events
    if frame:result['frame_sha256']=hashlib.sha256((args.out/'frame.png').read_bytes()).hexdigest()
    (args.out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))
    if status not in ('service bound','block bound'):raise SystemExit(1)


if __name__=='__main__':main()
