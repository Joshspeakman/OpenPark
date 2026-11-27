#!/usr/bin/env python3
"""The README's game screenshots: a park of the player's (a save of theirs, copied: never written) loaded into the e2e
harness's own copy of the app (tests/e2e: muted, scratch data, its own bundle identifier), then shots by a plan.
    platform/setup/dev/readme_shots.py <repo> <NAME:slot> '<plan json>' <out-dir>
    NAME:slot   the save: ~/Games/ThemeParkNative/saves/save/<NAME>.<slot> (with .big/.free), e.g. JOSH:g0
    plan        [{"name": ..., "ctl": [control lines]}, ...]; "card": 1 takes the controls card (controlsshot) instead,
                "walk": 1 the Visitor View (walkshot). Control lines: tests/e2e/ctl.py, main.m's runControl (size, zoom,
                pan, key, controls, walk -1 0, sleep).
The 2026-11-25 set: JOSH:g0 with size 1600 900; zoom 0.6 (opening-day), 1.8 (burger-oclock), 1.2 + pan 500 -150
(haunted-row), R (whole-park, 640 x 480), zoom 0.85 + pan -150 60 + the card (help-at-hand). Since 2026-11-26 the
README shows them as they are (make_readme_art plain, the card composited with --over; no frame, ribbon or caption)."""
import sys, os, shutil, time, re, json
sys.path.insert(0, sys.argv[1] + '/tests/e2e'); slot = sys.argv[2]; plan = json.loads(sys.argv[3]); outdir = sys.argv[4]; sys.argv = sys.argv[:1]
from e2e import *
SRC = os.path.expanduser('~/Games/ThemeParkNative/saves/save')
os.makedirs(outdir, exist_ok=True)
run('title', STEPS['title']); run('new_game', STEPS['new_game'])
def go():
    d = P + '/data/saves/save'
    for f in os.listdir(d):
        if f.startswith('JOSH.') or f.startswith('TESTPARK.') or f.startswith('QA.g3'): os.remove(f'{d}/{f}')
    name, src = slot.split(':')                        # e.g. JOSH:g0
    for ext in ('', '.big', '.free'):
        s = f'{SRC}/{name}.{src}{ext}'
        if os.path.exists(s): shutil.copy2(s, f'{d}/QA.g3{ext}')   # (a copy: the player's file is only read)
    wide(True); check(to_park(), 'not in the park')
    ld, sub, l3 = M('load'), M('sub'), M('load3')
    topmenu(M('park')); ctl(f'vmouse {ld[0]} {ld[1]} 0', 'sleep 0.5', f'vmouse {sub} {ld[1]} 0', 'sleep 0.6', f'vmouse {l3[0]} {l3[1]} 0', 'sleep 0.4', f'vclick {l3[0]} {l3[1]}', 'sleep 8')
    check(to_park(), 'not in the park after the load')
    for p in plan:                                     # each: name, list of control lines before the shot
        ctl(*p['ctl'])
        if p.get('card'):
            f = f'{outdir}/{p["name"]}.png'; ctl(f'controlsshot {f}'); print('  shot', p['name'], flush=True); continue
        if p.get('walk'):
            f = f'{outdir}/{p["name"]}.png'; ctl(f'walkshot {f}')
        else:
            f = vshot(p['name']); shutil.copy(f, f'{outdir}/{p["name"]}.png')
        print('  shot', p['name'], flush=True)
run('shots', go)
stop()
print('\n'.join(f'{"FAIL" if e else "ok"} {n} {e or ""}' for n, e, t in RESULTS))
