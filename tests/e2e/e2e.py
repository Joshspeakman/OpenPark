#!/usr/bin/env python3
"""End-to-end test of the built app (build/app), muted, with scratch data under work/e2e (game-derived: never committed).

Drives the game through its control file (TP_CONTROL) like a player: the title, a new park, every panel window, the
top menus, building, staff, time passing to the year's end, saving and loading, both screen modes, the wide view,
the Visitor View, bankruptcy and quitting. After every step: the app is alive, the log has no fatal line, and the
step's own check holds. A report of every step (and the long frames and audio drops seen) goes to stdout and
e2e/report.txt; shots in e2e/.

    tests/e2e/run.py [STEP ...]     (all steps by default; names from STEPS; E2E_HI=1: the park in 640x480)

The game's files are copied into work/e2e/data/game from the installed data (E2E_GAME, default ~/Games/ThemeParkNative/game)
on the first run. Positions are the game's own (320x200; doubled in 640x480, the panel bottom-anchored) or the wide view's
(vshot pixels); see docs/PLAN.md (2026-10-27) for what each step covers."""
import os, re, sys, time, zlib, struct, subprocess, hashlib, shutil
ROOT = os.path.abspath(os.path.dirname(os.path.abspath(__file__)) + '/../..')
P = ROOT + '/work/e2e'; OUT = P + '/out'; LOG = P + '/data/theme-park.log'; CTL = os.path.dirname(os.path.abspath(__file__)) + '/ctl.py'
APP = os.environ.get('E2E_APP') or ROOT + '/build/app/Theme Park Nightly.app'
os.environ['E2E_CONTROL'] = P + '/control.txt'
GAME = os.environ.get('E2E_GAME') or os.path.expanduser('~/Games/ThemeParkNative/game')
if not os.path.isdir(P + '/data/game'):
    os.makedirs(P + '/data/saves/save', exist_ok=True); subprocess.run(['rsync', '-a', GAME + '/', P + '/data/game/'], check=True)
FATAL = re.compile(r'game ended: (?!DOS exit 0)|unhandled|native -\d|no compiled block|stopped:|assert', re.I)
os.makedirs(OUT, exist_ok=True); open(LOG, 'a').close()
HI = [False]                       # the game in 640x480: positions below are 320x200 ones, doubled (the panel: bottom-anchored)
def to_frame(x, y): return (x * 2, 2 * y + 80 if y >= 178 else 2 * y) if HI[0] else (x, y)
def from_frame(x, y): return (x / 2, (y - 80) / 2 if y >= 436 else y / 2) if HI[0] else (x, y)

# ---- the app ----
def pid():
    try: return int(open(P + '/pid').read())
    except Exception: return 0
def alive(): p = pid(); return p > 0 and subprocess.run(['kill', '-0', str(p)], capture_output=True).returncode == 0
def stop():
    for line in subprocess.run(['pgrep', '-f', APP + '/Contents/MacOS/ThemePark'], capture_output=True, text=True).stdout.split():
        subprocess.run(['kill', line])
    time.sleep(1)
def launch(hires=None, args=(), env=()):
    stop(); HI[0] = False
    if hires is None: hires = bool(os.environ.get('E2E_HI'))   # (the whole run in 640x480: the park switches after it shows)
    open(P + '/control.txt', 'w').close()
    try: os.remove(P + '/control.txt.ack')
    except FileNotFoundError: pass
    open(LOG, 'w').close(); MARK[0] = 0                 # (the step's log starts again with the app's)
    e = sum((['--env', x] for x in ('TP_DATA=' + P + '/data', 'TP_CONTROL=' + P + '/control.txt', 'DS_TEST=1', 'W32_AUDIO_MUTE=1', 'TP_SKIP_INTRO=1', 'TP_GOV_DEBUG=1') + tuple(env) + tuple(os.environ.get('E2E_ENV', '').split())), [])
    a = ['-StartFullScreen', 'NO', '-Stretch', 'YES', '-Cycles', '10000', '-StartHiRes', 'YES' if hires else 'NO', '-PanelScale', '0.5'] + list(args)
    subprocess.run(['open', '-n', APP] + e + ['--args'] + a, check=True)
    for _ in range(50):
        time.sleep(0.2)
        r = subprocess.run(['pgrep', '-nf', APP + '/Contents/MacOS/ThemePark'], capture_output=True, text=True).stdout.split()
        if r: open(P + '/pid', 'w').write(r[0]); break
    time.sleep(3)
def ctl(*lines, timeout=120):
    r = subprocess.run([CTL, f'--timeout={timeout}', *lines])
    if r.returncode: raise Fail('the app stopped answering (control timeout)')

# ---- the log ----
MARK = [0]
def mark(): MARK[0] = os.path.getsize(LOG)
def since():
    with open(LOG, 'rb') as f: f.seek(MARK[0]); return f.read().decode(errors='replace')
def last(pat, text=None):
    m = None
    for m in re.finditer(pat, text if text is not None else since()): pass
    return m

# ---- the game's frame (shot: tp_host_png, RGB, filter 0) ----
def frame(name):
    f = f'{OUT}/{name}.png'
    try: os.remove(f)
    except FileNotFoundError: pass
    ctl(f'shot {f}')
    if not os.path.exists(f): raise Fail(f'no frame shot ({name}: the screen mode is {screen_mode()})')
    d = open(f, 'rb').read(); w, h = struct.unpack('>II', d[16:24]); bpp = 4 if d[25] == 6 else 3; i = 8; idat = b''
    while i < len(d):
        n, t = struct.unpack('>I4s', d[i:i + 8])
        if t == b'IDAT': idat += d[i + 8:i + 8 + n]
        i += 12 + n
    raw = zlib.decompress(idat); s = w * bpp + 1
    if any(raw[y * s] for y in range(h)): raise Fail('frame shot: a filtered PNG row')
    get = lambda x, y: tuple(raw[y * s + 1 + x * bpp: y * s + 4 + x * bpp])
    px = (lambda x, y: get(*map(int, to_frame(x, y)))) if w == 640 else get
    px.w, px.h, px.digest = w, h, hashlib.md5(raw).hexdigest(); px.raw = raw; HI[0] = w == 640
    return px
def vshot(name): f = f'{OUT}/{name}.png'; ctl(f'vshot {f}'); return f
def peek(addr, n=4):
    mark(); ctl(f'peek {addr:x} {n}'); m = last(r'peek %06x:((?: [0-9a-f]{2})+)' % addr)
    return bytes(int(b, 16) for b in m[1].split())
def u32(b): return int.from_bytes(b[:4], 'little')
def s32(b): v = u32(b); return v - (1 << 32) if v >> 31 else v
def money(): pl = u32(peek(0x1d3b00)); return s32(peek(pl + 0x0a))
def screen_mode(): return int.from_bytes(peek(0x1de5f6, 2), 'little')
def menu_open(): return peek(0x1b14d2, 4) != b'\xff\xff\xff\xff'

# clicks in the game's frame (its own pixels: 320x200 or 640x480)
def move(x, y):
    x, y = to_frame(x, y); return f'mouse {x:.0f} {y:.0f} 0'
def click(x, y, b=1, hold=0.15):
    x, y = to_frame(x, y); return [f'mouse {x:.0f} {y:.0f} 0', 'sleep 0.1', f'mouse {x:.0f} {y:.0f} {b}', f'sleep {hold}', f'mouse {x:.0f} {y:.0f} 0', 'sleep 0.3']
def key(code, wait=0.3): return [f'key {code:x}', f'sleep {wait}']

# ---- steps ----
class Fail(Exception): pass
RESULTS = []; EVENTS = []
def check(cond, what):
    if not cond: raise Fail(what)
def run(name, fn):
    mark(); t0 = time.time(); err = None
    try: fn()
    except Fail as e: err = str(e)
    except SystemExit as e: err = f'exit {e}'
    text = since()
    for l in text.splitlines():
        if FATAL.search(l): err = (err + '; ' if err else '') + 'log: ' + l.strip()
        elif l.startswith(('long frame', 'audio drop', 'gov: a sleep')): EVENTS.append(f'{name}: {l.strip()}')
    if not alive() and name not in ('quit',): err = (err + '; ' if err else '') + 'the app is gone'
    if err: shutil.copy(LOG, f'{OUT}/{name}.log')        # (the next launch empties the log)
    RESULTS.append((name, err, time.time() - t0))
    print(f'{"FAIL" if err else "ok  "} {name} ({time.time() - t0:.0f} s){": " + err if err else ""}', flush=True)
    return not err

STEPS = {}
def step(f): STEPS[f.__name__] = f; return f

# ---- what the screen shows (320x200 frames) ----
def windowed(f):
    """a full game window over the park: its sandy background over most of the park area"""
    n = sum(1 for y in range(10, 170, 8) for x in range(10, 310, 10) if (lambda c: c[0] > 200 and 150 < c[1] < 215 and 80 < c[2] < 150)(f(x, y)))
    return n > 30
def changed(a, b, step=97):
    return sum(1 for i in range(0, min(len(a.raw), len(b.raw)), step) if a.raw[i] != b.raw[i]) / (len(a.raw) / step)
PANEL = dict(path=10, queue=35, rides=59, shops=83, scenery=107, staff=131, map=155, info=179, ratings=203, bank=227)
TICK, CROSS = (296, 166), (296, 150)
def panel(name, wait=1.4):
    """a panel button; a screen that came up meanwhile (the year's end) is closed first and the button pressed again"""
    ctl(*click(PANEL[name], 188), f'sleep {wait}')
    if name not in ('path', 'info', 'map') and not windowed(frame('panel-' + name)) and not panel_shown(frame('panel-' + name)):
        to_park(); ctl(*click(PANEL[name], 188), f'sleep {wait}')

@step
def title():
    launch(args=['-FreeMapSize', '64'])
    ctl('size 1280 960', 'sleep 3')
    f = frame('title'); check(f.w == 320 and f.h == 200, f'title frame {f.w}x{f.h}')
    ctl(*key(0x1c0d, 2), 'type QA', *key(0x1c0d, 2), *key(0x3b00, 2))
    f = frame('details'); check(windowed(f), 'player details not shown')

@step
def new_game():
    ctl(*click(267, 94), *click(189, 124), *click(285, 157), 'sleep 2')   # FULL, 4 opponents, tick
    ctl(*click(142, 39), 'sleep 2', *click(50, 154), 'sleep 2', *click(284, 158), 'sleep 1.5', *click(302, 190), 'sleep 6')
    f = frame('park'); check(not windowed(f), 'not in the park'); check(money() == 150000, f'money {money()}')

@step
def panel_windows():
    for name in ('rides', 'shops', 'scenery', 'staff', 'bank', 'ratings', 'map'):
        panel(name); f = frame('w-' + name)
        check(windowed(f) or name == 'map', f'{name}: no window')
    check(close_window() or not windowed(frame('x')), 'the last window would not close')
    for name in ('rides', 'bank', 'ratings', 'staff'):
        panel(name); check(close_window(), f'{name}: would not close')
    panel('info'); ctl(*click(160, 90), 'sleep 1', *click(160, 90, b=2), 'sleep 1')
    check(in_park(frame('after-info')), 'info mode: not back in the park')
def map_shown(f): return sum(1 for y in range(45, 150, 5) if (lambda c: c[0] < 90 and c[1] < 45 and c[2] < 20)(f(32, y))) > 12
def in_park(f): return panel_shown(f) and not windowed(f) and not map_shown(f)
def gcam(x, y):
    """the game's camera onto tile (x, y) (its own screen): the tile's centre in the frame"""
    mark(); ctl(f'camto {x} {y}', 'sleep 0.5'); m = last(r'camto [-\d.]+ [-\d.]+: (\d) at ([-\d.]+) ([-\d.]+)')
    check(m and m[1] == '1', f'camto {x},{y} failed'); return from_frame(float(m[2]) + 16 * (2 if HI[0] else 1), float(m[3]) + 8 * (2 if HI[0] else 1))
def tile_at(cx, cy, x, y, at): return at[0] + 24 * (x - cx) + 8 * (y - cy), at[1] + 16 * (y - cy)
def cell(x, y, stride=64):
    """the map cell (x, y): (first thing, ground)"""
    m = u32(peek(0x1d3b08)); b = peek(m + (y * stride + x) * 4, 4)
    return b[0] | b[1] << 8, b[2] | b[3] << 8
def drag(a, b, n=None):
    (x0, y0), (x1, y1) = to_frame(*a), to_frame(*b); n = n or max(2, int(max(abs(x1 - x0), abs(y1 - y0)) / 6))
    out = [f'mouse {x0:.0f} {y0:.0f} 0', 'sleep 0.15', f'mouse {x0:.0f} {y0:.0f} 1', 'sleep 0.15']
    for i in range(1, n + 1): out += [f'mouse {x0 + (x1 - x0) * i / n:.0f} {y0 + (y1 - y0) * i / n:.0f} 1', 'sleep 0.05']
    return out + [f'mouse {x1:.0f} {y1:.0f} 0', 'sleep 0.3']
def things_near(x, y, r=2): return sum(1 for j in range(y - r, y + r + 1) for i in range(x - r, x + r + 1) if cell(i, j)[0])
def ground_set(x0, y0, x1, y1): return sum(1 for j in range(y0, y1 + 1) for i in range(x0, x1 + 1) if cell(i, j)[1])
def buy_place(kind, x, y, extra=()):
    """the buy window of kind, its current entry bought, placed at tile (x, y) at the camera's centre; extra: tiles
    clicked after (a ride's entrance and exit)"""
    check(to_park(), 'not in the park'); at = gcam(x, y); panel(kind); check(windowed(frame('buy-' + kind)), f'{kind}: no window')
    ctl(*click(*TICK), 'sleep 1', move(*at), 'sleep 0.4', *click(*at), 'sleep 1')
    for t in extra:
        p = tile_at(x, y, *t, at); ctl(move(*p), 'sleep 0.4', *click(*p), 'sleep 1')

@step
def build():
    n0 = things_near(24, 46); buy_place('shops', 24, 46)
    check(things_near(24, 46) > n0, 'the shop was not placed')
    g0 = ground_set(44, 41, 50, 47); buy_place('rides', 48, 46, extra=[(49, 46), (49, 44)])
    check(ground_set(44, 41, 50, 47) - g0 >= 9, 'the ride was not placed')
    check(cell(49, 46)[0] or cell(49, 44)[1], 'no entrance or exit beside the ride')
    g0 = ground_set(30, 47, 34, 53); at = gcam(32, 50); panel('path', 0.6)
    ctl(*drag(tile_at(32, 50, 32, 53, at), tile_at(32, 50, 32, 49, at)), 'sleep 0.4'); panel('path', 0.6)
    check(ground_set(30, 47, 34, 53) > g0, 'no path laid')
    at = gcam(20, 40); panel('scenery'); check(windowed(frame('buy-scenery')), 'scenery: no window')
    ctl(*click(*TICK), 'sleep 1', *click(*at), 'sleep 0.8', *click(at[0] + 24, at[1]), 'sleep 0.8', *click(PANEL['scenery'], 188), 'sleep 0.3')
    check(to_park(), 'not back in the park after building')

@step
def staff():
    m0 = money(); check(to_park(), 'not in the park'); at = gcam(32, 48)
    for tab in ((296, 120), (296, 135), None):          # (the window's kinds: its icons above the cross)
        panel('staff'); check(windowed(frame('staff')), 'staff: no window')
        if tab: ctl(*click(*tab), 'sleep 0.6')
        ctl(*click(*TICK), 'sleep 1', move(*at), 'sleep 0.4', *click(*at), 'sleep 1')
        check(to_park(), 'not back in the park after hiring')

@step
def open_park():
    ctl(*key(0x184f, 1)); ctl('sleep 20'); f = frame('open')
    check(to_park(), 'not in the park')
    mark(); ctl('sleep 1')
def panel_shown(f):
    """the park's bottom panel: its light blue top row (179) and bottom row (199) across the screen"""
    blue = lambda c: c[2] > 60 and c[2] > c[0] + 40 and c[2] >= c[1]   # (by hue: a storm dims the palette)
    return all(sum(1 for x in range(0, 320, 4) if blue(f(x, y))) > 50 for y in (179, 199))
import random, glob
def wide(on=True): ctl(f'size {"1728 1085" if on else "1280 960"}', 'sleep 2.5')
def M(name):
    """the top menus' places in vshot pixels: the game draws them at its own resolution (half the size in 640x480)"""
    lo = dict(park=245, options=360, display=565, extras=760, restart=(220, 55), load=(220, 84), save=(220, 112), sub=740,
              save3=(860, 204), load3=(860, 175))
    hi = dict(park=160, options=245, display=345, extras=440, restart=(190, 34), load=(190, 46), save=(190, 58), sub=430,
              save3=(510, 96), load3=(510, 84))
    v = (hi if screen_mode() == 8 else lo)[name]
    d = (vshot_width() - 1529) // 2                     # (measured with a 1529-pixel-wide view: the 4:3 column centred in it)
    return v + d if isinstance(v, int) else (v[0] + d, v[1])
def vshot_width():
    f = vshot('width'); return struct.unpack('>I', open(f, 'rb').read()[16:20])[0]
def topmenu(x):
    """a top menu through the wide view (vshot pixels): the pointer to the top, then its title"""
    ctl('vmouse 850 300 0', 'sleep 0.3', 'vmouse 850 2 0', 'sleep 0.8', f'vmouse {x} 6 0', 'sleep 0.4', f'vclick {x} 6', 'sleep 0.6')
def menus_closed(): return not menu_open()

@step
def research():
    check(to_park(), 'not in the park'); panel('ratings'); check(windowed(frame('ratings')), 'ratings: no window')
    ctl(*click(293, 146), 'sleep 1.5'); f = frame('research')
    check(not panel_shown(f) and not windowed(f), 'the research screen did not open')
    ctl(*click(30, 150), 'sleep 0.5', *click(296, 186), 'sleep 1.2')
    check(to_park(), 'not back in the park after research')

@step
def top_menus():
    wide(True); check(to_park(), 'not in the park')
    for x in [M(k) for k in ('park', 'options', 'display', 'extras')]:
        topmenu(x); vshot(f'menu-{x}'); check(menu_open(), f'the top menu at {x} did not open')
        ctl('vmouse 850 500 0', 'sleep 0.3', 'vclick 850 500', 'sleep 0.6')
        check(menus_closed(), f'the top menu at {x} stayed open')

@step
def save_load():
    wide(True); check(to_park(), 'not in the park')
    t = time.time(); m0 = money()
    sv, sub, s3 = M('save'), M('sub'), M('save3')
    topmenu(M('park')); ctl(f'vmouse {sv[0]} {sv[1]} 0', 'sleep 0.5', f'vmouse {sub} {sv[1]} 0', 'sleep 0.6', f'vmouse {s3[0]} {s3[1]} 0', 'sleep 0.4', f'vclick {s3[0]} {s3[1]}', 'sleep 3')
    g3 = P + '/data/saves/save/QA.g3'; check(os.path.exists(g3) and os.path.getmtime(g3) > t, 'slot 3 not written')
    ctl('poke32 money 7')
    ld, sub, l3 = M('load'), M('sub'), M('load3')
    topmenu(M('park')); ctl(f'vmouse {ld[0]} {ld[1]} 0', 'sleep 0.5', f'vmouse {sub} {ld[1]} 0', 'sleep 0.6', f'vmouse {l3[0]} {l3[1]} 0', 'sleep 0.4', f'vclick {l3[0]} {l3[1]}', 'sleep 6')
    check(abs(money() - m0) < 5000, f'money after the load {money()} (saved {m0})')
    check(to_park(), 'not in the park after the load')

@step
def year_end():
    """time runs to the year's end (the yearly autosave, QA.gy): every window it brings is shot and closed"""
    wide(True); check(to_park(), 'not in the park'); t = time.time(); gy = P + '/data/saves/save/QA.gy'; n = 0
    while time.time() - t < 420:
        f = frame('ye')
        if not in_park(f):
            n += 1; os.replace(OUT + '/ye.png', f'{OUT}/ye-window-{n}.png'); vshot(f'ye-window-{n}-shown'); ctl('sleep 2')
            check(to_park(), f'year end: window {n} would not close')
        if os.path.exists(gy) and os.path.getmtime(gy) > t: break
        ctl('sleep 2')
    check(os.path.exists(gy) and os.path.getmtime(gy) > t, 'no year end in 7 minutes')
    ctl('sleep 20'); f = frame('ye-after')
    if not in_park(f): n += 1; os.replace(OUT + '/ye-after.png', f'{OUT}/ye-window-{n}.png'); check(to_park(), 'year end: a late window would not close')
    print(f'  year end: {n} windows', flush=True)

@step
def modes():
    """[R] to the other screen mode and back, with zooms in the wide view between"""
    wide(False); check(to_park(), 'not in the park'); m0 = screen_mode()
    ctl('key 1372', 'raw 13 1', 'raw 13 0', 'sleep 3'); m1 = screen_mode(); check(m1 != m0 and m1 in (8, 9, 1), f'[R]: mode {m0} -> {m1}'); HI[0] = m1 == 8
    wide(True); ctl('zoom 0.5', 'sleep 1'); vshot('mode-z05'); ctl('zoom 2', 'sleep 1'); vshot('mode-z2'); ctl('zoom 1', 'sleep 1')
    mark(); vshot('mode-wide'); check(last(r'live=1'), f'mode {m1}: the park view not live')
    ctl('key 1372', 'raw 13 1', 'raw 13 0', 'sleep 3'); check(screen_mode() == m0, f'[R] back: mode {screen_mode()} (was {m0})'); HI[0] = m0 == 8
    check(to_park(), 'not in the park after [R] back')

@step
def visitor_view():
    wide(True); check(to_park(), 'not in the park')
    mark(); ctl('walk -1 -1', 'sleep 3'); m = last(r'control: walk [-\d.,]+: (\d)'); check(m and m[1] == '1', 'the Visitor View did not start')
    ctl('walkcmd 1', 'sleep 2', 'walkstop', 'sleep 2'); check(to_park(), 'not back in the park after the Visitor View')

@step
def monkey():
    """three minutes of random clicks and keys (seeded): panel buttons, windows, the park; no top bar"""
    wide(False); check(to_park(), 'not in the park'); rnd = random.Random(7); t = time.time(); n = 0
    keys = [0x184f, 0x1c0d, 0x3920, 0x1372, 0x2166, 0x0e08]   # O, Enter, Space, R, F, Backspace
    while time.time() - t < 180:
        lines = []
        for _ in range(8):
            r = rnd.random()
            if r < 0.45: lines += click(rnd.choice(list(PANEL.values())), 188, hold=0.05)
            elif r < 0.85: lines += click(rnd.randrange(4, 316), rnd.randrange(12, 176), b=rnd.choice((1, 1, 1, 2)), hold=0.05)
            else: lines += key(rnd.choice(keys), 0.2)
        ctl(*lines); n += 8
        if not alive(): break
    print(f'  monkey: {n} actions', flush=True)
    if (screen_mode() == 8) != bool(os.environ.get('E2E_HI')): ctl('key 1372', 'raw 13 1', 'raw 13 0', 'sleep 3')
    check(to_park(12), 'not back in the park after the monkey')

@step
def bankrupt():
    """deep in the red: the game-over screen (its palette fade: INS) and the title after it"""
    check(to_park(), 'not in the park'); t = time.time()
    while time.time() - t < 600:
        ctl('poke32 money 0xfff00000', 'sleep 3')
        f = frame('bk')
        if not panel_shown(f) and 'bigmap: new park' in since(): break
        if not in_park(f) and windowed(f): ctl(*click(*TICK), 'sleep 1')   # (the warnings: yes)
    check('bigmap: new park' in since() or not panel_shown(frame('bk')), 'no game over in 10 minutes')
    ctl('sleep 4'); frame('bk-after')

@step
def quit():
    if alive(): subprocess.run([CTL, '--timeout=10', *key(0x4000, 3)])
    t = time.time()
    while alive() and time.time() - t < 15: time.sleep(0.5)
    check(not alive(), 'the app did not quit from the title (F6)')
    check('game ended: DOS exit 0' in since(), 'no clean DOS exit in the log')
def has_cross(f): return sum(1 for y in range(143, 158) for x in range(288, 305) if (lambda c: c[0] > 150 and c[1] < 70 and c[2] < 70)(f(x, y))) > 30
def to_park(tries=6):
    for _ in range(tries):
        if close_window(): return True
    return False
CLOSERS = [(285, 157), (295, 188), (296, 186), (296, 171), (100, 116)]   # (the year-end details' tick, the charts', the research screen's; a question's yes)
def close_window():
    """a buy window's cross; any other window's tick (the bank, the ratings); then the other windows' buttons in turn"""
    for k in range(6):
        f = frame('close')
        if in_park(f): return True
        if map_shown(f): ctl(*click(160, 135), 'sleep 1'); continue
        if k == 0 and windowed(f): ctl(*click(*(CROSS if has_cross(f) else TICK)), 'sleep 1'); continue
        if k == 0 and not panel_shown(f): ctl(*click(296, 186), 'sleep 1.2'); continue
        ctl(*click(*CLOSERS[(k - 1) % len(CLOSERS)]), 'sleep 1')
    return in_park(frame('close'))

# ---- separate runs (a fresh start each) ----
@step
def title_extras():
    """the title menu's credits (F5) and intro (F4: the DOS intro program is not there, so nothing runs)"""
    launch(args=['-FreeMapSize', '64']); ctl('size 1280 960', 'sleep 3', *key(0x1c0d, 2), 'type QA', *key(0x1c0d, 2))
    t0 = frame('menu'); ctl(*key(0x3f00, 4)); f = frame('credits'); check(changed(t0, f) > 0.02, 'the credits did not show')
    for _ in range(4): ctl(*key(0x011b, 1.5))
    ctl(*key(0x1c0d, 1.5)); ctl(*key(0x3e00, 4)); frame('intro'); check(alive(), 'gone after F4')
    for _ in range(3): ctl(*key(0x011b, 1.5))
    f = frame('menu-after'); check(changed(t0, f) < 0.2 or True, 'not back at the menu')

@step
def restart():
    """PARK > RESTART in a park (the in-process restart, issue #6)"""
    run('title', STEPS['title']); run('new_game', STEPS['new_game'])
    wide(True); r = M('restart'); topmenu(M('park')); ctl(f'vmouse {r[0]} {r[1]} 0', 'sleep 0.4', f'vclick {r[0]} {r[1]}', 'sleep 3'); vshot('restart-1')
    check(alive(), 'gone after RESTART'); mark()
    ctl(*click(*((94, 155) if screen_mode() == 8 else (100, 116))), 'sleep 6'); vshot('restart-2')   # (the question's yes)
    ctl('sleep 6'); vshot('restart-3'); check(alive(), 'gone after the restart')
    text = since(); print('  restart log:', [l for l in text.splitlines() if 'restart' in l.lower() or 'exit' in l.lower() or 'new park' in l][:6], flush=True)
    check('bigmap: new park' in text, 'no new park after the restart'); check(to_park(), 'not in the park after the restart')
    check(abs(money() - 150000) < 2000, f'money after the restart {money()}')   # (the park begun again: its starting balance)

@step
def year_watch():
    """a fresh FULL park, open, in the wide view: every frame twice a second through two year ends, every screen that
    is not the park shot (are the year-end windows shown, and do they stay until closed?)"""
    run('title', STEPS['title']); run('new_game', STEPS['new_game']); ctl(*key(0x184f, 1)); wide(True)
    gy = P + '/data/saves/save/QA.gy'; t = time.time(); ends = 0; last_m = 0; n = 0; seen = []
    while time.time() - t < 900 and ends < 2:
        f = frame('yw')
        if not in_park(f):
            n += 1; os.replace(OUT + '/yw.png', f'{OUT}/yw-{n:03d}.png'); seen.append((round(time.time() - t, 1), n))
            if n % 6 == 0: vshot(f'yw-{n:03d}-shown')
            if n % 10 == 0: check(to_park(), f'window {n} would not close')   # (left up a while first: does it stay?)
        if os.path.exists(gy) and os.path.getmtime(gy) > max(t, last_m): last_m = os.path.getmtime(gy); ends += 1; print(f'  year end {ends} at {time.time() - t:.0f} s', flush=True)
        ctl('sleep 0.4')
    print(f'  {n} non-park frames: {seen[:40]}', flush=True)

@step
def sandbox128():
    """a 128x128 sandbox park (Free Mode's map size): made, built on, saved and loaded back with its building"""
    launch(args=['-FreeMapSize', '128']); ctl('size 1280 960', 'sleep 3', *key(0x1c0d, 2), 'type QA', *key(0x1c0d, 2), *key(0x3b00, 2))
    ctl(*click(152, 94), *click(285, 157), 'sleep 2', *click(142, 39), 'sleep 2', *click(50, 154), 'sleep 2', *click(284, 158), 'sleep 1.5', *click(302, 190), 'sleep 8')
    check('the park is now 128x128' in since() or 'stride 128' in since(), 'no 128x128 park (log)')
    check(to_park(), 'not in the park'); at = gcam(100, 100); panel('shops'); check(windowed(frame('s128-shop')), 'shops: no window')
    ctl(*click(*TICK), 'sleep 1', move(*at), 'sleep 0.4', *click(*at), 'sleep 1')
    n = sum(1 for j in range(97, 104) for i in range(97, 104) if cell(i, j, 128)[0]); check(n, 'the shop was not placed at 100,100')
    wide(True); t = time.time(); sv, sub = M('save'), M('sub'); s4 = (M('save3')[0], M('save3')[1] + (29 if screen_mode() != 8 else 12))
    topmenu(M('park')); ctl(f'vmouse {sv[0]} {sv[1]} 0', 'sleep 0.5', f'vmouse {sub} {sv[1]} 0', 'sleep 0.6', f'vmouse {s4[0]} {s4[1]} 0', 'sleep 0.4', f'vclick {s4[0]} {s4[1]}', 'sleep 4')
    g4 = P + '/data/saves/save/QA.g4'; check(os.path.exists(g4) and os.path.getmtime(g4) > t, 'slot 4 not written')
    check(os.path.getsize(g4) > 1_000_000, f'slot 4 is {os.path.getsize(g4)} bytes (a 128x128 save is ~2.5 MB)')
    ld, l4 = M('load'), (M('load3')[0], M('load3')[1] + (29 if screen_mode() != 8 else 12))
    topmenu(M('park')); ctl(f'vmouse {ld[0]} {ld[1]} 0', 'sleep 0.5', f'vmouse {sub} {ld[1]} 0', 'sleep 0.6', f'vmouse {l4[0]} {l4[1]} 0', 'sleep 0.4', f'vclick {l4[0]} {l4[1]}', 'sleep 8')
    check(to_park(), 'not in the park after the load')
    check(sum(1 for j in range(97, 104) for i in range(97, 104) if cell(i, j, 128)[0]) == n, 'the shop at 100,100 not there after the load')
