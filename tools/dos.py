"""Deterministic development DOS host. Unknown services stop, never succeed silently.

The host is shared by the x86 oracle and native verification harness. It is not
an independent DOS implementation oracle and is not the final app platform.
"""
import struct
import zlib
import hashlib
import json
import tempfile
from pathlib import Path
from le import LE

MEMORY = 64 * 1024 * 1024
BIAS = 0x100000
GDT, PSP, ENV = 0x1000, 0x2000, 0x3000
SEGMENTS = ('cs', 'ds', 'es', 'ss', 'fs', 'gs')
REGS = ('eax', 'ecx', 'edx', 'ebx', 'esp', 'ebp', 'esi', 'edi')


def scratch_profile(output, label='run'):
    root=Path(output)/'scratch'
    root.mkdir(parents=True,exist_ok=True)
    return Path(tempfile.mkdtemp(prefix=label+'-',dir=root))


def load_image(path):
    pins = json.loads((Path(__file__).resolve().parents[1]/'config/inputs.json').read_text())
    digest = hashlib.sha256(Path(path).read_bytes()).hexdigest()
    if digest not in {v['sha256'] for v in pins['executables'].values()}:
        raise ValueError('unsupported executable digest')
    image = LE.read(path)
    image.sha256 = digest
    for obj in image.objects:
        obj.base += BIAS
    image.entry += BIAS
    image.stack += BIAS
    if image.apply_offset_fixups():
        raise ValueError('unimplemented selector relocation')
    return image


def descriptor(base, limit=0xfffff, access=0x92, flags=0xc):
    return struct.pack('<HHBBBB', limit & 65535, base & 65535,
                       (base >> 16) & 255, access | 1,
                       ((limit >> 16) & 15) | (flags << 4), base >> 24)


class Stop(RuntimeError):
    pass


class Arena:
    def __init__(self, start, end, alignment):
        self.free = [(start, end-start)]
        self.used = {}
        self.alignment = alignment

    def allocate(self, size):
        if size <= 0:
            return None
        size = (size+self.alignment-1) & -self.alignment
        for index,(ptr,length) in enumerate(self.free):
            if length >= size:
                self.free[index:index+1] = [(ptr+size,length-size)] if length > size else []
                self.used[ptr] = size
                return ptr
        return None

    def release(self, ptr):
        size = self.used.pop(ptr)
        self.free.append((ptr,size))
        merged = []
        for at,length in sorted(self.free):
            if merged and merged[-1][0]+merged[-1][1] == at:
                begin,n = merged.pop(); merged.append((begin,n+length))
            else:
                merged.append((at,length))
        self.free = merged

    def largest(self):
        return max((size for _,size in self.free), default=0)

    def resize(self, ptr, size):
        old = self.used[ptr]
        if size <= 0: return None
        size = (size+self.alignment-1) & -self.alignment
        if size <= old:
            self.used[ptr] = size
            if size < old:
                self.used[ptr+size] = old-size
                self.release(ptr+size)
            return ptr
        for index,(at,length) in enumerate(self.free):
            if at == ptr+old and length >= size-old:
                left = length-(size-old)
                self.free[index:index+1] = [(ptr+size,left)] if left else []
                self.used[ptr] = size
                return ptr
        replacement = self.allocate(size)
        if replacement is not None: self.release(ptr)
        return replacement


class DOS:
    def __init__(self, machine, game, scratch):
        self.m = machine
        self.game, self.scratch = Path(game).resolve(), Path(scratch).resolve()
        if self.scratch.is_relative_to(self.game) or self.game.is_relative_to(self.scratch):
            raise ValueError('game and scratch roots must not overlap')
        self.scratch.mkdir(parents=True, exist_ok=True)
        self.bases = {8: 0, 16: 0, 24: PSP, 32: ENV}
        self.next_selector, self.heap = 0x100, 0x400000
        self.handles, self.vectors = {}, {}
        self.events, self.output = [], bytearray()
        self.exit_code = None
        self.allocations = {}
        self.ticks = 0
        self.linear = Arena(0x400000, MEMORY, 4096)
        self.conventional = Arena(0x40000, 0x90000, 16)
        self.free_selectors = []
        self.mouse = {'x':320,'y':240,'buttons':0,'visible':False,'callback':None,'range_x':(0,639),'range_y':(0,479)}
        self.video_mode = 3
        self.ports = {0x21:0xff,0xa1:0xff,0x3c6:0xff}
        self.palette = bytearray(768)
        self.dac_index = 0
        self.io = []
        self.pit = []
        self.keys = []
        self.irq_count = 0
        self.vectors[8] = (8,0xf000)
        self.poll_count = 0
        self.key_script = []
        self.mouse_script = []
        self.mouse_events = []
        self.pending_mouse = None
        self.click_script=[]
        self.blocks=0
        self.release_due=None
        self.block_clicks=[]

    def saved_files(self):
        return {str(p.relative_to(self.scratch)):hashlib.sha256(p.read_bytes()).hexdigest()
                for p in sorted(self.scratch.rglob('*')) if p.is_file()}

    def close(self):
        for handle in self.handles.values():handle.close()
        self.handles.clear()

    def port_in(self, port, size):
        if size != 1:
            raise Stop('unsupported port input width')
        if port in self.ports:
            value = self.ports[port]
        elif port == 0x3da:
            self.ticks += 1; value = 8 if self.ticks & 1 else 0
        else:
            raise Stop('unsupported IN %04x' % port)
        self.io.append(('in',port,size,value))
        return value

    def port_out(self, port, size, value):
        if size != 1:
            raise Stop('unsupported port output width')
        self.io.append(('out',port,size,value))
        if port in (0x21,0xa1,0x3c6):
            self.ports[port] = value
        elif port in (0x43,0x40):
            self.pit.append((port,value))
        elif port in (0x20,0xa0) and value == 0x20:
            pass # PIC end-of-interrupt acknowledgement
        elif port == 0x3c8:
            self.dac_index = value * 3
        elif port == 0x3c9:
            self.palette[self.dac_index] = value & 63
            self.dac_index = (self.dac_index+1)%768
        else:
            raise Stop('unsupported OUT %04x=%x' % (port,value))

    def frame(self, path):
        if self.video_mode != 0x13:
            return None
        pixels = self.m.read(0xa0000,64000)
        palette = bytes((v << 2) | (v >> 4) for v in self.palette)
        rows = bytearray()
        for y in range(200):
            rows.append(0)
            for index in pixels[y*320:(y+1)*320]:
                index &= self.ports[0x3c6]
                rows.extend(palette[index*3:index*3+3])
        def chunk(kind,data):
            return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
        png = b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>2I5B',320,200,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(bytes(rows)))+chunk(b'IEND',b'')
        Path(path).write_bytes(png)
        return {'mode':0x13,'size':[320,200],'nonzero_pixels':sum(p!=0 for p in pixels),'colors':len(set(pixels))}

    def allocate_dos(self, paragraphs):
        ptr = self.conventional.allocate(paragraphs * 16)
        if ptr is None:
            self.m.set('ebx', self.conventional.largest()//16); self.carry(8); return None
        if self.free_selectors:
            sel = self.free_selectors.pop()
        else:
            sel = self.next_selector; self.next_selector += 8
        if sel >= 4096:
            raise Stop('descriptor table exhausted')
        self.bases[sel] = ptr
        self.allocations[sel] = self.conventional.used[ptr]
        self.m.write(GDT+sel, descriptor(ptr))
        self.m.write(ptr, bytes(self.allocations[sel]))
        self.carry()
        return ptr,sel

    def bootstrap(self, image):
        for obj in image.objects:
            self.m.write(obj.base, bytes(obj.data))
        self.m.write(GDT, bytes(4096))
        self.m.write(0xf000,b'\xcf') # synthetic default timer interrupt return
        # Synthetic far-callback epilogue restores the interrupted guest state.
        self.m.write(0xf010,bytes.fromhex('0fa90fa1071f619dc3'))
        for selector, base in self.bases.items():
            self.m.write(GDT + selector, descriptor(base, access=0x9a if selector == 8 else 0x92))
        self.m.write(PSP + 0x2c, struct.pack('<H', 32))
        self.m.write(PSP + 0x80, b'\0\r')
        self.m.write(ENV, b'PATH=C:\\GAME\0NO87=1\0\0\x01\x00C:\\GAME\\MAIN.EXE\0')
        for name in REGS:
            self.m.set(name, image.stack if name == 'esp' else 0)
        self.m.set('eflags', 0x202)
        self.m.segments({ 'cs': 8, 'ds': 16, 'es': 24, 'ss': 16, 'fs': 16, 'gs': 0 })

    def address(self, segment, offset):
        return self.bases[self.m.get(segment)] + offset

    def timer_irq(self):
        # Development scheduling: one PIT tick at an idle keyboard poll.
        # The final host must schedule against the programmed PIT divisor.
        if not self.m.get('eflags') & 0x200 or self.vectors[8][1] == 0xf000:
            return
        m = self.m
        sel,pc = self.vectors[8]
        sp = m.get('esp')-12
        m.write(sp,struct.pack('<III',m.get('eip'),m.get('cs'),m.get('eflags')))
        m.set('esp',sp); m.set('cs',sel); m.set('eip',pc)
        m.set('eflags',m.get('eflags')&~0x300)
        self.irq_count += 1

    def poll_keyboard(self):
        self.poll_count += 1
        while self.key_script and self.key_script[0][0] <= self.poll_count:
            _,key = self.key_script.pop(0)
            self.keys.append(key)
        while self.mouse_script and self.mouse_script[0][0] <= self.poll_count:
            poll,x,y,buttons = self.mouse_script.pop(0)
            self.queue_mouse(x,y,buttons)
        while self.click_script and self.click_script[0][0]<=self.poll_count:
            poll,x,y=self.click_script.pop(0)
            self.queue_mouse(x,y,1)
            self.release_due=self.blocks+50000

    def queue_mouse(self,x,y,buttons):
        old=self.mouse.copy()
        event=1 if (x,y)!=(old['x'],old['y']) else 0
        for bit,press,release in ((1,2,4),(2,8,16),(4,32,64)):
            if (old['buttons']^buttons)&bit:event |= press if buttons&bit else release
        self.mouse.update(x=x,y=y,buttons=buttons)
        self.pending_mouse=(event,x-old['x'],y-old['y'])
        self.mouse_events.append((self.poll_count,x,y,buttons,event,self.blocks))

    def advance_blocks(self,count):
        self.blocks+=count
        if self.release_due is not None and self.blocks>=self.release_due and self.m.get('eflags')&0x200:
            self.release_due=None
            self.queue_mouse(self.mouse['x'],self.mouse['y'],0)
            self.mouse_callback()
        if self.block_clicks and self.blocks>=self.block_clicks[0][0] and self.m.get('eflags')&0x200:
            _,x,y=self.block_clicks.pop(0)
            self.queue_mouse(x,y,1);self.release_due=self.blocks+50000
            self.mouse_callback()

    def mouse_callback(self):
        if not self.pending_mouse:return
        event,dx,dy = self.pending_mouse; self.pending_mouse=None
        callback = self.mouse['callback']
        if not callback or not event&callback[0]:return
        mask,selector,pc = callback
        if self.bases.get(selector)!=0:
            raise Stop('nonflat mouse callback selector')
        m=self.m
        # Callback ES identifies its far address; use the flat code alias.
        saved=[0xf010,m.get('cs'),m.get('gs'),m.get('fs'),m.get('es'),m.get('ds'),
               m.get('edi'),m.get('esi'),m.get('ebp'),m.get('esp'),m.get('ebx'),m.get('edx'),m.get('ecx'),m.get('eax'),m.get('eflags'),m.get('eip')]
        sp=m.get('esp')-4*len(saved)
        m.write(sp,struct.pack('<'+'I'*len(saved),*saved))
        for name,value in (('eax',event),('ebx',self.mouse['buttons']),('ecx',self.mouse['x']),('edx',self.mouse['y']),('esi',dx&0xffffffff),('edi',dy&0xffffffff),('esp',sp),('cs',8),('eip',pc)):
            m.set(name,value)
        m.set('eflags',m.get('eflags')&~0x400)

    def cstring(self, address):
        data = bytearray()
        for n in range(4096):
            b = self.m.read(address + n, 1)[0]
            if b == 0:
                return data.decode('cp437')
            data.append(b)
        raise Stop('unterminated DOS string')

    def carry(self, error=0):
        self.m.set('eflags', (self.m.get('eflags') & ~1) | bool(error))
        if error:
            self.m.set('eax', error)

    def path(self, name):
        name = name.replace('\\', '/')
        if len(name) > 1 and name[1] == ':':
            name = name[2:]
        parts = [p for p in name.split('/') if p not in ('', '.')]
        if parts and parts[0].upper() == 'GAME':
            parts.pop(0)
        if not parts or '..' in parts:
            raise Stop('unsafe DOS path: ' + name)
        for root in (self.scratch, self.game):
            at = root
            for part in parts:
                matches = [p for p in at.iterdir() if p.name.upper() == part.upper()] if at.is_dir() else []
                at = matches[0] if matches else at / part
            if not at.resolve().is_relative_to(root):
                raise Stop('DOS path escaped root')
            if at.is_file():
                return at
        return self.scratch.joinpath(*parts)

    def writable_path(self,name):
        path = self.path(name)
        if path.is_relative_to(self.game):
            path = self.scratch/path.relative_to(self.game)
        if not path.resolve().is_relative_to(self.scratch):
            raise Stop('DOS write escaped scratch')
        path.parent.mkdir(parents=True,exist_ok=True)
        return path

    def available_handle(self):
        return next((h for h in range(5,20) if h not in self.handles),None)

    def interrupt(self, number):
        m = self.m
        a,b,c,d = (m.get(n) for n in ('eax','ebx','ecx','edx'))
        ax, ah = a & 65535, (a >> 8) & 255
        self.events.append({'int': number, 'eax': a, 'ebx': b, 'ecx': c, 'edx': d, 'pc': m.get('eip')})
        if number == 0x21:
            if ah == 0xb:
                m.set('eax',(a&~255)|(255 if self.keys else 0)); return
            if ah in (7,8):
                if not self.keys: raise Stop('waiting for DOS keyboard input')
                key = self.keys.pop(0)
                m.set('eax',(a&~255)|(key&255)); return
            if ah == 0x30:
                m.set('eax', 0x1606); m.set('ebx', 0); m.set('ecx', 0); return
            if ax == 0xff00 and d & 65535 == 0x78:
                m.set('eax', (a & ~255) | 1); return
            if ah == 0x2c:
                self.ticks += 1
                t = self.ticks
                m.set('ecx', (12 << 8) | ((t // 6000) % 60))
                m.set('edx', (((t // 100) % 60) << 8) | (t % 100)); return
            if ah == 0x2a:
                m.set('eax', (a & ~255) | 5); m.set('ecx', 2026); m.set('edx', 0xa10); return
            if ah == 0x48:
                block = self.allocate_dos(b & 65535)
                if block: m.set('eax', block[1])
                return
            if ah == 0x49:
                sel = m.get('es')
                if sel not in self.allocations:
                    self.carry(9); return
                self.conventional.release(self.bases[sel]); del self.allocations[sel]
                self.free_selectors.append(sel)
                self.carry(); return
            if ah == 0x3d:
                name = self.cstring(self.address('ds', d))
                self.events[-1]['path'] = name
                path = self.path(name)
                if not path.is_file():
                    self.carry(2); return
                if a & 3:
                    if not path.is_relative_to(self.scratch):
                        target = self.scratch / path.relative_to(self.game)
                        target.parent.mkdir(parents=True, exist_ok=True)
                        target.write_bytes(path.read_bytes()); path = target
                # DOS reuses the lowest free handle. Monotonically increasing
                # handles exceed Watcom's descriptor table after a few opens.
                handle = self.available_handle()
                if handle is None:
                    self.carry(4); return
                self.handles[handle] = path.open('r+b' if a & 3 else 'rb')
                m.set('eax', handle); self.carry(); return
            if ah == 0x3c:
                name = self.cstring(self.address('ds',d))
                self.events[-1]['path'] = name
                handle = self.available_handle()
                if handle is None: self.carry(4); return
                path = self.writable_path(name)
                self.handles[handle] = path.open('w+b')
                m.set('eax',handle); self.carry(); return
            if ax == 0x4300:
                name = self.cstring(self.address('ds',d))
                self.events[-1]['path'] = name
                path = self.path(name)
                if not path.exists(): self.carry(2); return
                m.set('ecx',0x10 if path.is_dir() else 0x20); self.carry(); return
            if ah == 0x3e:
                if b not in self.handles:
                    self.carry(6); return
                self.handles.pop(b).close(); self.carry(); return
            if ah == 0x3f:
                if b not in self.handles:
                    self.carry(6); return
                data = self.handles[b].read(c)
                m.write(self.address('ds', d), data)
                m.set('eax', len(data)); self.carry(); return
            if ah == 0x42:
                if b not in self.handles:
                    self.carry(6); return
                offset = struct.unpack('<i', struct.pack('<I', ((c & 65535) << 16) | (d & 65535)))[0]
                pos = self.handles[b].seek(offset, a & 255)
                m.set('eax', pos & 65535); m.set('edx', pos >> 16); self.carry(); return
            if ax == 0x4400:
                if b < 5:
                    m.set('edx', 0x80d3 if b == 0 else 0x80d2)
                    self.carry(); return
                if b in self.handles:
                    m.set('edx', 0); self.carry(); return
                self.carry(6); return
            if ah == 0x35:
                sel, ptr = self.vectors.get(a & 255, (8, 0))
                m.set('es', sel); m.set('ebx', ptr); return
            if ah == 0x25:
                self.vectors[a & 255] = (m.get('ds'), d); return
            if ah == 0x40 and b in (1, 2):
                self.output.extend(m.read(self.address('ds', d), c))
                m.set('eax', c); self.carry(); return
            if ah == 0x40:
                if b not in self.handles: self.carry(6); return
                f = self.handles[b]
                if not f.writable(): self.carry(5); return
                if c: count = f.write(m.read(self.address('ds',d),c))
                else: f.truncate(); count=0
                f.flush(); m.set('eax',count); self.carry(); return
            if ah == 0x4c:
                self.exit_code = a & 255
                raise Stop('DOS exit %d' % self.exit_code)
        if number == 0x33:
            if ax == 0x2f:
                # Optional hardware-reset extension is unavailable, as on
                # CuteMouse's INT33 dispatch. Preserve AX (not success=0).
                self.events[-1]['unsupported_optional'] = 'mouse hardware reset'
                return
            if ax == 0:
                m.set('eax',0xffff); m.set('ebx',3); return
            if ax in (1,2):
                self.mouse['visible'] = ax==1; return
            if ax == 3:
                m.set('ebx',self.mouse['buttons']); m.set('ecx',self.mouse['x']); m.set('edx',self.mouse['y']); return
            if ax == 4:
                self.mouse['x'],self.mouse['y'] = c&65535,d&65535; return
            if ax in (7,8):
                self.mouse['range_x' if ax==7 else 'range_y'] = (c&65535,d&65535); return
            if ax == 0xc:
                self.mouse['callback'] = (c & 65535,m.get('es'),d); return
        if number == 0x10:
            if ah == 0xf:
                m.set('eax',(a&0xffff0000)|0x5000|self.video_mode)
                m.set('ebx',b&~0xff00); return
            if ah == 0:
                self.video_mode = a & 127
                if self.video_mode not in (3,0x13):
                    raise Stop('unsupported VGA mode %x' % self.video_mode)
                if not a & 128: m.write(0xa0000,bytes(65536))
                return
        if number == 0x16:
            if ah in (2,0x12):
                m.set('eax',a&0xffff0000); return
            if ah in (1,0x11):
                self.poll_keyboard()
                m.set('eflags',(m.get('eflags')&~0x40)|(0 if self.keys else 0x40))
                if self.keys: m.set('eax',(a&0xffff0000)|self.keys[0])
                self.timer_irq()
                self.mouse_callback()
                return
            if ah in (0,0x10):
                if not self.keys: raise Stop('waiting for keyboard input')
                m.set('eax',(a&0xffff0000)|self.keys.pop(0)); return
        if number == 0x31:
            if ax == 6:
                if (b & 65535) not in self.bases:
                    self.carry(0x8022); return
                base = self.bases[b & 65535]
                m.set('ecx',base>>16); m.set('edx',base&65535); self.carry(); return
            if ax == 0x101:
                sel = d & 65535
                if sel not in self.allocations:
                    self.carry(0x8022); return
                self.conventional.release(self.bases[sel]); del self.allocations[sel]
                self.free_selectors.append(sel); self.carry(); return
            if ax == 0x100:
                block = self.allocate_dos(b & 65535)
                if block:
                    m.set('eax', block[0] >> 4); m.set('edx',block[1])
                return
            if ax == 0x501:
                size = ((b & 65535) << 16) | (c & 65535)
                if not size:
                    self.carry(0x8021); return
                size = (size + 4095) & ~4095
                ptr = self.linear.allocate(size)
                if ptr is None:
                    self.carry(0x8012); return
                self.heap = max(self.heap,ptr+size)
                self.allocations[ptr] = size
                m.write(ptr, bytes(size))
                for reg, value in (('ebx', ptr >> 16), ('ecx', ptr & 65535),
                                   ('esi', ptr >> 16), ('edi', ptr & 65535)):
                    m.set(reg, value)
                self.carry(); return
            if ax == 0x502:
                handle = ((m.get('esi') & 65535) << 16) | (m.get('edi') & 65535)
                if handle not in self.allocations:
                    self.carry(0x8023); return
                del self.allocations[handle]
                self.linear.release(handle)
                self.carry(); return
            if ax == 0x503:
                handle = ((m.get('esi') & 65535) << 16) | (m.get('edi') & 65535)
                size = (((b & 65535) << 16) | (c & 65535))
                if handle not in self.allocations or not size:
                    self.carry(0x8023 if handle not in self.allocations else 0x8021); return
                size = (size + 4095) & ~4095
                old = self.allocations[handle]
                ptr = self.linear.resize(handle,size)
                if ptr is None:
                    self.carry(0x8012); return
                saved = m.read(handle, min(size,old))
                m.write(ptr, saved + bytes(max(0,size-old)))
                del self.allocations[handle]
                self.allocations[ptr] = size
                self.heap = max(self.heap, ptr + size)
                for reg,value in (('ebx',ptr>>16),('ecx',ptr&65535),('esi',ptr>>16),('edi',ptr&65535)):
                    m.set(reg,value)
                self.carry(); return
        raise Stop('unsupported int %02x ax=%04x bx=%08x cx=%08x dx=%08x at %08x' % (number, ax,b,c,d,m.get('eip')))
