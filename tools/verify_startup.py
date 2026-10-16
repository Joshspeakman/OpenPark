#!/usr/bin/env python3
"""Compile traced blocks as ARM64 and compare startup at DOS service boundaries."""
import argparse
import ctypes
import hashlib
import json
import subprocess
import time
from pathlib import Path
from unicorn import UC_HOOK_INTR, UC_HOOK_BLOCK, UC_HOOK_CODE, UC_HOOK_MEM_WRITE, UC_HOOK_INSN, UcError
from unicorn import x86_const as U
from dos import DOS, Stop, REGS, SEGMENTS, MEMORY, load_image, scratch_profile
from trace_startup import Original
from lift_dos import create_lifter, ROOT
from native_host import Native


def build(image, reference, out, blocks, variants):
    lifter = create_lifter(reference, image, out)
    sources = []
    # Collect direct fall-through entries too, but do not speculate recursively
    # into branches absent from the trace. Missing targets stop explicitly.
    dispatch = 'void tp_dispatch_init(void) {}\nconst uint32_t tp_block_count = %du;\nint tp_dispatch(Ctx *c) { switch(c->tc) {\n' % len(blocks)
    decoder = lifter.ins
    for address in sorted(blocks):
        dispatch += 'case 0x%xu:\n' % address
        for index,encoded in enumerate(variants[str(address)]):
            data = bytes.fromhex(encoded)
            decoded = {i.address:i for i in lifter.md.disasm(data,address)}
            lifter.ins = lambda pc: decoded.get(pc) if address <= pc < address+len(data) else decoder(pc)
            # End at the observed block extent: it may end at a call/return,
            # REP boundary, or a self-modifying branch's new target.
            collect = lifter.collect
            lifter.collect = lambda f,extra=(): decoded
            source,_ = lifter.lift_function(address)
            lifter.collect = collect
            name = 'f_%08x_v%d' % (address,index)
            source = source.replace('f_%08x(' % address,name+'(')
            sources.append(source)
            literal = ''.join('\\x%02x' % b for b in data)
            dispatch += 'if (!memcmp(G_MEM+0x%xu,"%s",%du)) { %s(c); return 1; }\n' % (address,literal,len(data),name)
        dispatch += 'rt_unhandled(c,c->tc,"uncompiled code variant"); return 0;\n'
    lifter.ins = decoder
    header = (ROOT / 'runtime/verify.c').read_text()
    dispatch += 'default: return 0; }}\n'
    path = out / 'startup.c'
    path.write_text(header + '\n'.join(sources) + dispatch)
    library = out / 'startup.dylib'
    subprocess.run(['clang', '-arch', 'arm64', '-O1', '-dynamiclib', '-I', str(reference / 'recomp/runtime'), str(path), '-o', str(library)], check=True)
    (out/'build.json').write_text(json.dumps({'image_sha256':getattr(image,'sha256',None),
        'library_sha256':hashlib.sha256(library.read_bytes()).hexdigest(),'architecture':'arm64'},indent=2)+'\n')
    (out / 'lift.json').write_text(json.dumps({'blocks':len(blocks), 'unsupported':lifter.unsup}, indent=2))
    return library


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('exe', type=Path)
    ap.add_argument('--reference', type=Path, required=True)
    ap.add_argument('--trace', type=Path, default=Path('work/startup/trace.json'))
    ap.add_argument('--out', type=Path, default=Path('work/startup-native'))
    ap.add_argument('--debug-from',type=int,default=1000000)
    ap.add_argument('--services',type=int,default=20000)
    ap.add_argument('--seconds',type=float,default=90)
    ap.add_argument('--quantum',type=int,default=0)
    ap.add_argument('--library',type=Path,help='prebuilt whole-program library (tools/build_game.py)')
    args = ap.parse_args()
    if args.services<=0 or args.seconds<=0 or args.quantum<0:ap.error('invalid verification bound')
    args.out.mkdir(parents=True, exist_ok=True)
    image = load_image(args.exe)
    trace = json.loads(args.trace.read_text())
    blocks = set(trace['blocks'])
    library = args.library or build(image, args.reference.resolve(), args.out, blocks,trace['variants'])
    native, original = Native(library), Original()
    hosts = [DOS(m, args.exe.parent, scratch_profile(args.out,name)) for m,name in ((native,'native'),(original,'original'))]
    for host in hosts:
        host.bootstrap(image)
        host.m.set('eip', image.entry)
        host.key_script = [(int(p),int(k,16)) for p,k in (s.split(':') for s in trace.get('keys','').split(',') if s)]
        host.mouse_script = [tuple(map(int,s.split(':'))) for s in trace.get('mouse','').split(',') if s]
        host.click_script = [tuple(map(int,s.split(':'))) for s in trace.get('clicks','').split(',') if s]
        host.block_clicks = [tuple(map(int,s.split(':'))) for s in trace.get('block_clicks','').split(',') if s]
    for sel, base in hosts[0].bases.items():
        native.lib.tp_descriptor(sel, base)
    io_error = []
    @ctypes.CFUNCTYPE(ctypes.c_int,ctypes.c_int,ctypes.c_uint,ctypes.c_uint,ctypes.c_uint)
    def native_io(output,port,size,value):
        try:
            if output: hosts[0].port_out(port,size,value); return 0
            return hosts[0].port_in(port,size)
        except Stop as e:
            io_error.append(str(e)); return -1
    native.lib.tp_io_callback(native_io)
    original.uc.hook_add(UC_HOOK_INSN,lambda uc,p,s,_: hosts[1].port_in(p,s),None,1,0,U.UC_X86_INS_IN)
    original.uc.hook_add(UC_HOOK_INSN,lambda uc,p,s,v,_: hosts[1].port_out(p,s,v),None,1,0,U.UC_X86_INS_OUT)
    boundary = []
    dirty = set()
    def written(uc, access, address, size, value, _):
        dirty.update(range(address//4096, (address+size-1)//4096+1))
    def interrupt(uc, number, _):
        boundary.append(number); uc.emu_stop()
    original.uc.hook_add(UC_HOOK_INTR, interrupt)
    original.uc.hook_add(UC_HOOK_MEM_WRITE, written)
    comparisons = 0
    status = 'service bound'
    deadline = time.monotonic()+args.seconds
    limit = min(args.services,len(trace.get('services',[])))
    quantum=args.quantum or trace.get('quantum',0)
    yields=0
    target_blocks=trace.get('executed_blocks',0) if quantum and args.services>=len(trace.get('services',[])) else 0
    while comparisons<limit or hosts[0].blocks<target_blocks:
        if time.monotonic() >= deadline:
            status = 'wall-clock bound'; break
        debug = comparisons >= args.debug_from
        if debug:
            for step in range(1000000):
                pc = native.get('eip')
                boundary.clear()
                number = native.lib.tp_run(1)
                if number < 0: break
                seen_block = [False]
                def next_block(uc,address,size,_):
                    if seen_block[0] and address == native.get('eip'): uc.emu_stop()
                    seen_block[0] = True
                hook = original.uc.hook_add(UC_HOOK_CODE,next_block)
                original.uc.ctl_remove_cache(0,MEMORY)
                original.uc.emu_start(original.get('eip'),0,timeout=1000000,count=1000000)
                original.uc.hook_del(hook)
                for reg in REGS+SEGMENTS+('eip',):
                    if native.get(reg)!=original.get(reg):
                        raise AssertionError(('block',hex(pc),reg,hex(native.get(reg)),hex(original.get(reg)), 'pc',hex(native.get('eip')),hex(original.get('eip'))))
                for page in sorted(dirty):
                    x,y=native.read(page*4096,4096),original.read(page*4096,4096)
                    if x!=y:
                        at=next(k for k,(a,b) in enumerate(zip(x,y)) if a!=b)
                        raise AssertionError(('block',hex(pc),'memory',hex(page*4096+at),x[at:at+8].hex(),y[at:at+8].hex()))
                dirty.clear()
                if number: break
        else:
            budget=min(quantum,target_blocks-hosts[0].blocks) if target_blocks else quantum or 1000000
            if budget<=0:raise AssertionError('trace block bound precedes its service count')
            number = native.lib.tp_run(budget)
        if number < 0 or number==0 and not quantum:
            status = 'native %d at %08x: %s' % (number,native.get('eip'),native.lib.tp_error().decode())
            break
        if not debug:
            boundary.clear()
            if quantum:
                original_number=original.run_blocks(budget)
                if original_number!=number or original.blocks_run!=native.lib.tp_blocks():
                    raise AssertionError(('quantum',comparisons,original_number,number,original.blocks_run,native.lib.tp_blocks(),hex(original.get('eip')),hex(native.get('eip'))))
            else:
                original.uc.emu_start(original.get('eip'), 0, timeout=10000000, count=10000000)
        if boundary != ([number] if number else []):
            raise AssertionError(('interrupt divergence',number,boundary))
        if hosts[0].io != hosts[1].io:
            raise AssertionError('port I/O divergence')
        for reg in REGS + SEGMENTS + ('eip',):
            if native.get(reg) != original.get(reg):
                raise AssertionError((comparisons,reg,hex(native.get(reg)),hex(original.get(reg))))
        # Undefined x86 arithmetic flags are not compared at arbitrary services.
        if (native.get('eflags') ^ original.get('eflags')) & 0x600:
            raise AssertionError('IF/DF divergence')
        # Arithmetic flags may be architecturally undefined here (after MUL,
        # shifts, ...). They are not compared, so they must not leak into
        # memory through interrupt or callback frames either: continue both
        # sides from the original's flags.
        native.set('eflags', original.get('eflags'))
        # Include relocated code: startup writes its saved data selector there.
        # Compare CPU-written pages at each boundary and the complete mapped
        # region periodically and at the final stop. The full comparisons catch
        # unexpected native writes to pages the original did not touch.
        spans = [(0,MEMORY)] if (comparisons+yields) % 100 == 0 else [(p*4096,4096) for p in sorted(dirty)]
        for address,size in spans:
            x,y = native.read(address,size),original.read(address,size)
            if x != y:
                at = next(k for k,(a,b) in enumerate(zip(x,y)) if a != b)
                raise AssertionError((comparisons,'memory',hex(address+at),x[at:at+16].hex(),y[at:at+16].hex()))
        dirty.clear()
        if number:comparisons += 1
        else:yields+=1
        stopped = []
        for host in hosts:
            try:
                if number:host.interrupt(number)
                if quantum:host.advance_blocks(native.lib.tp_blocks())
            except Stop as e:
                stopped.append(str(e))
        for sel,base in hosts[0].bases.items():
            native.lib.tp_descriptor(sel,base)
        if stopped:
            if len(stopped) != 2 or stopped[0] != stopped[1]:
                raise AssertionError(('host divergence',stopped))
            status = stopped[0]; break
    if number >= 0 and native.read(0,MEMORY) != original.read(0,MEMORY):
        raise AssertionError('final full-memory mismatch')
    for host in hosts:host.close()
    if number>=0 and hosts[0].saved_files()!=hosts[1].saved_files():
        raise AssertionError('scratch file output mismatch')
    result = {'status':status,'service_boundaries_compared':comparisons,'compiled_blocks':ctypes.c_uint32.in_dll(native.lib,'tp_block_count').value if args.library else len(blocks),'pc':native.get('eip'),
              'output':hosts[0].output.decode('cp437'),'limits':'Shared DOS host, synthetic DOS4G launch contract; no independent DOS compatibility claim.'}
    result['frame'] = hosts[0].frame(args.out/'native-frame.png')
    result['scratch']=[str(host.scratch) for host in hosts]
    result['saved_files']=hosts[0].saved_files()
    result['scheduling_boundaries_compared']=yields
    result['executed_blocks']=hosts[0].blocks
    hosts[1].frame(args.out/'original-frame.png')
    if number >= 0 and result['frame']:
        if (args.out/'native-frame.png').read_bytes() != (args.out/'original-frame.png').read_bytes():
            raise AssertionError('framebuffer/palette mismatch')
        result['frame_sha256'] = hashlib.sha256((args.out/'native-frame.png').read_bytes()).hexdigest()
    (args.out / 'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__ == '__main__':
    main()
