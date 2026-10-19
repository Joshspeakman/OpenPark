#!/usr/bin/env python3
"""Render a local authored-model manifest through the actual Visitor renderer.

All inputs/outputs are local game-derived material; keep them under work/.
Requires visitor_snapshot, which rejects missing/incompatible requested models.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import html
import json
import os
from pathlib import Path
import subprocess


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('manifest', 'snapshot', 'memory', 'profile', 'out'):
        p.add_argument('--' + name, required=True, type=Path)
    a = p.parse_args()
    assets = json.loads(a.manifest.read_text())
    a.out.mkdir(parents=True, exist_ok=True)
    jobs = []
    for asset in assets:
        slug = asset['slug']
        if not slug or any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789-' for c in slug):
            p.error('Asset slugs must be lowercase letters, digits and hyphens')
        jobs.append((asset, 'source', -90, True))
        jobs.extend((asset, f'{i:02}', -90 + i * 15, False) for i in range(24))
    def render(job):
        asset, label, yaw, source = job
        target = a.out / f"{asset['slug']}-{label}.png"
        cmd = [str(a.snapshot.resolve()), str(a.memory.resolve()),
               str(a.profile.resolve() / 'game/DATA/MPALETTE.DAT'), str(target.resolve()),
               '128', 'frame', str(asset['frame']), '0', str(yaw)]
        cmd += ['source'] if source else ['model', str(asset['cls']), str(asset['type'])]
        env = dict(os.environ, TP_DATA=str(a.profile.resolve()), TP_WALK_MESH='1',
                   TP_SNAPSHOT_EYE=str(asset.get('eye', asset['height'] * .65 + .8)),
                   TP_SNAPSHOT_PITCH=str(asset.get('pitch', .18)))
        proc = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=30)
        return dict(file=target.name, code=proc.returncode, log=proc.stderr)
    with ThreadPoolExecutor(max_workers=3) as pool:
        results = list(pool.map(render, jobs))
    (a.out / 'render-log.json').write_text(json.dumps(results, indent=2) + '\n')
    failures = [r['file'] for r in results if r['code']]
    if failures:
        raise SystemExit(f'Render failures: {failures}')
    cards = []
    for asset in assets:
        slug, name = asset['slug'], html.escape(asset['name'])
        cards.append(f'''<article data-slug="{slug}"><h2>{name}</h2>
<div class="views"><figure class="source"><img src="{slug}-source.png" alt="Original {name} sprite">
<figcaption>Original sprite</figcaption></figure><figure class="model">
<a href="{slug}-03.png"><img src="{slug}-03.png" alt="Authored {name} model"></a>
<figcaption>Native renderer · <span>45°</span></figcaption></figure></div>
<label>Rotate <input aria-label="Rotate {name}" type="range" min="0" max="23" value="3"></label>
<button type="button">Play turntable</button></article>''')
    page = '''<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Faithful 3D — authored asset review</title>
<style>body{font:16px system-ui;background:#18232c;color:#f3efe4;margin:24px auto;max-width:1200px;padding:0 16px}
h1{font-size:30px}p{line-height:1.6;max-width:85ch}article{background:#26353f;border:1px solid #40535d;border-radius:12px;padding:20px;margin:24px 0}
.views{display:flex;align-items:center;gap:24px}figure{margin:0;min-width:0}.source{flex:1}.model{flex:3}
img{width:100%;image-rendering:pixelated}figcaption{margin:8px 0;color:#c7d0d2}input{width:55%;vertical-align:middle;margin:16px}button{padding:8px;background:#d5b36b;border:0;border-radius:4px}
@media(max-width:650px){.views{flex-direction:column}.source{max-width:180px}}
</style><h1>Faithful 3D</h1><p>Eight authored models using the original palette pixels.
Toy Land's entrance is solid. Rotate each asset to inspect its silhouette and inferred side/rear surfaces.
These are offline captures from the native Visitor renderer, not headed game screenshots.
This is a static review set; full ride mechanisms and character animation are still outstanding.</p>'''
    script = '''<script>for(const card of document.querySelectorAll('article')){
const slider=card.querySelector('input'), img=card.querySelector('.model img'), link=card.querySelector('.model a'), button=card.querySelector('button');let timer;
const update=()=>{const i=Number(slider.value);img.src=`${card.dataset.slug}-${String(i).padStart(2,'0')}.png`;link.href=img.src;card.querySelector('span').textContent=`${i*15}°`};
slider.addEventListener('input',update);button.addEventListener('click',()=>{if(timer){clearInterval(timer);timer=null;button.textContent='Play turntable'}else{timer=setInterval(()=>{slider.value=(Number(slider.value)+1)%24;update()},180);button.textContent='Pause'}});
}</script></html>'''
    (a.out / 'index.html').write_text(page + '\n'.join(cards) + script)
    print(f'{len(results)} renders passed: {a.out / "index.html"}')


if __name__ == '__main__':
    main()
