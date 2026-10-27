#!/usr/bin/env python3
"""Send control lines to the running app (TP_CONTROL: E2E_CONTROL, or control.txt beside this file) and wait until they
have run (the app appends the closing ack's tag to <file>.ack)."""
import os, sys, time
CTL = os.environ.get('E2E_CONTROL') or os.path.join(os.path.dirname(os.path.abspath(__file__)), 'control.txt')
tag = f'{time.time():.6f}'
KEYS = {c: (code << 8) | ord(c) for codes, start in (("1234567890", 2), ("QWERTYUIOP", 0x10), ("ASDFGHJKL", 0x1e), ("ZXCVBNM", 0x2c)) for code, c in ((start + i, ch) for i, ch in enumerate(codes))}
KEYS[' '] = 0x3920
lines = []
for a in sys.argv[1:]:
    if a.startswith('type '):
        for ch in a[5:].upper(): lines += [f'key {KEYS[ch]:x}', 'sleep 0.15']
    elif a.startswith('vclick ') or a.startswith('click '):
        op, *xy = a.split(); m = 'vmouse' if op == 'vclick' else 'mouse'; b = xy[2] if len(xy) > 2 else '1'
        lines += [f'{m} {xy[0]} {xy[1]} 0', 'sleep 0.15', f'{m} {xy[0]} {xy[1]} {b}', 'sleep 0.35', f'{m} {xy[0]} {xy[1]} 0', 'sleep 0.2']
    elif a: lines.append(a)
timeout = 120
if lines and lines[0].startswith('--timeout='): timeout = float(lines.pop(0)[10:])
with open(CTL, 'a') as f:
    for l in lines: f.write(l + '\n')
    f.write(f'ack {tag}\n')
t0 = time.time()
while time.time() - t0 < timeout:
    try:
        if tag in open(CTL + '.ack').read(): sys.exit(0)
    except FileNotFoundError: pass
    time.sleep(0.1)
print('timeout waiting for the app', file=sys.stderr); sys.exit(1)
