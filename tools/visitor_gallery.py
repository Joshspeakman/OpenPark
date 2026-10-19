#!/usr/bin/env python3
"""Local source-frame review gallery; does not certify complete game objects.

Requires visitor_inventory.py output, the copied memory/palette, and a compiled
visitor_snapshot. All output is game-derived and stays in ignored work/.
"""
from concurrent.futures import ThreadPoolExecutor
import html
import json
import os
from pathlib import Path
import subprocess


def shape(obj):
    kind, i = obj['category'], obj['index']
    if kind == 'shop':
        return 0
    if kind == 'scenery':
        return 6 if i == 10 else 0 if i in (11, 12, 13, 14) else 3
    if i == 4:
        return 6
    if i in (0, 1, 5, 6, 14, 22, 23, 24, 25):
        return 0
    return 2 if i in (2, 3, 10, 11, 12, 15, 17, 19, 21, 26, 28) else 1


def main():
    root = Path(__file__).resolve().parents[1]
    work = root / 'work/visitor-3d'
    inv = json.loads((work / 'inventory.json').read_text())
    out = work / 'gallery'
    out.mkdir(exist_ok=True)
    jobs, rows = [], []
    for obj in inv['objects']:
        key = f"{obj['category']}-{obj['index']:02}"
        title = html.escape(obj['name'])
        if not obj['animations']:
            rows.append(f'<article><h2>{title}</h2><p>Placed track and car composites: '
                        'no standalone definition frame. Requires live park review.</p></article>')
            continue
        chain = inv['animation_chains'][obj['animations'][0]]
        if not chain:
            continue
        frame = chain[0]
        images = []
        for label, yaw in [('source-facing', -71.565), ('side', 18.435), ('rear', 108.435)]:
            filename = f'{key}-{label}.png'
            jobs.append((frame, shape(obj), yaw, filename))
            images.append(f'<figure><a href="{filename}"><img loading="lazy" src="{filename}"></a>'
                          f'<figcaption>{label}, frame {frame}</figcaption></figure>')
        rows.append(f'<article><h2>{title}</h2><div class="views">{"".join(images)}</div>'
                    '<p>Isolated source frame, inferred shape. Runtime riders, track, signs, '
                    'and multi-part construction may add further detail.</p></article>')
    def render(job):
        frame, form, yaw, filename = job
        proc = subprocess.run([str(work / 'native/snapshot'), str(work / 'baseline/park-memory.bin'),
            str(work / 'profile/game/DATA/MPALETTE.DAT'), str(out / filename), '128',
            'frame', str(frame), str(form), str(yaw)], capture_output=True,
            env=dict(os.environ,TP_WALK_SHELL_STUDY='1',TP_WALK_MESH='1'))
        return filename, proc.returncode, proc.stderr.decode(errors='replace')
    with ThreadPoolExecutor(max_workers=3) as pool:
        results = list(pool.map(render, jobs))
    (out / 'render-log.json').write_text(json.dumps(results, indent=2) + '\n')
    page = '''<!doctype html><meta charset="utf-8"><title>Visitor 3D — provisional source-frame review</title>
<style>body{font:16px system-ui;background:#18222c;color:#f2ede5;margin:2rem}h1{font-size:26px}
article{background:#27333e;padding:1rem;margin:1rem 0}h2{font-size:18px}.views{display:flex;gap:1rem}
figure{margin:0;flex:1;min-width:0}img{width:100%;image-rendering:pixelated}figcaption{margin-top:.5rem}
p{max-width:85ch;line-height:1.5}</style>
<h1>Visitor 3D — provisional source-frame review</h1>
<p><strong>Visual quality has not passed.</strong> This gallery examines original palette pixels on
inferred shells. These are offline renderer captures, not headed game screenshots or 65 authored
models. Side/rear textures repeat source art. Roofs, maze relief, scale and grounding need review.
The five track rides require placed track/car composites. No generated art is substituted.</p>'''
    (out / 'index.html').write_text(page + '\n'.join(rows))
    failures = [(name, code) for name, code, _ in results if code]
    print(f'{len(results)} renders; {len(failures)} failed: {out / "index.html"}')
    if failures:
        raise SystemExit(str(failures))


if __name__ == '__main__':
    main()
