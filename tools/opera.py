#!/usr/bin/env python3
"""List or extract a 3DO disc's Opera filesystem from a bin/cue data track (2352-byte sectors).

The volume header (sector 0: type 1, five 0x5a sync bytes) gives the root directory's blocks; each directory
block lists entries (flags, id, type, block size, byte count, block count, burst, gap, name[32], avatars:
copies of the file's first block). Written from the format's public description. Output is game-derived
and stays in work/.

  tools/opera.py DISC.bin [OUTDIR]
"""
import struct, sys, os
BIN=sys.argv[1]; OUT=sys.argv[2] if len(sys.argv)>2 else None
f=open(BIN,'rb'); RAW=2352
def sector(n): f.seek(n*RAW+16); return f.read(2048)
def blocks(start, count, bs=2048):
    return b''.join(sector(start+i) for i in range(count))
v=sector(0)
assert v[0]==1 and v[1:6]==b'\x5a'*5, v[:8]
label=v[0x28:0x48].split(b'\0')[0]; bsz,bcnt,rootid,rootblocks,rootbs,lastcopy=struct.unpack_from('>IIIIII',v,0x4c)
copies=struct.unpack_from('>%dI'%(lastcopy+1),v,0x64)
print('label',label,'blocksize',bsz,'blocks',bcnt,'root at',copies[0],'blocks',rootblocks)
files=[]
def walk(start, nblocks, path):
    for b in range(nblocks):
        blk=sector(start+b)
        nxt,prev,flags,first_free,first_entry=struct.unpack_from('>iiIII',blk,0)
        off=first_entry
        while off < first_free:
            eflags,eid,etype,ebs,ebytes,eblocks,burst,gap=struct.unpack_from('>II4sIIIII',blk,off)
            name=blk[off+32:off+64].split(b'\0')[0].decode('latin1')
            navat,=struct.unpack_from('>I',blk,off+64)
            avat=struct.unpack_from('>%dI'%(navat+1),blk,off+68)
            off+=68+4*(navat+1)
            p=path+'/'+name
            if eflags & 0xff == 7: walk(avat[0], eblocks, p)
            else: files.append((p, avat[0], ebytes, etype))
        if eflags & 0x80000000: pass
walk(copies[0], rootblocks, '')
tot=0
for p,a,n,t in files:
    tot+=n
print(len(files),'files, %.1f MB'%(tot/1e6))
for p,a,n,t in sorted(files): print('%10d %s %s'%(n, t.decode('latin1'), p))
if OUT:
    for p,a,n,t in files:
        dst=os.path.join(OUT,p.lstrip('/')); os.makedirs(os.path.dirname(dst),exist_ok=True)
        data=blocks(a,(n+2047)//2048)[:n]; open(dst,'wb').write(data)
