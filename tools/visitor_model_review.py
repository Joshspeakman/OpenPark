#!/usr/bin/env python3
"""Render paired authored-model turntables with the native snapshot tool.

Inputs and captures are owned-game-derived and must stay in ignored work/.
Every request uses snapshot's strict model mode: a missing/incompatible model
fails instead of passing a sprite fallback off as an authored render.
"""
import argparse,hashlib,html,json,os,struct,subprocess
from pathlib import Path

def main():
 p=argparse.ArgumentParser(description=__doc__)
 for name in ('snapshot','memory','palette','manifest','before','after','out'):p.add_argument('--'+name,type=Path,required=True)
 p.add_argument('--views',type=int,default=24);p.add_argument('--map-width',type=int,default=128)
 p.add_argument('--title',default='Faithful 3D — geometry detail pass')
 p.add_argument('--before-label',default='Previous geometry');p.add_argument('--after-label',default='Refined geometry')
 a=p.parse_args()
 if not 4<=a.views<=72:p.error('--views must be between 4 and 72')
 a.out.mkdir(parents=True,exist_ok=True);assets=json.loads(a.manifest.read_text());runs=[]
 for asset in assets:
  slug=asset['slug']
  if not slug or any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789-' for c in slug):raise ValueError('Unsafe asset slug')
  for version,profile in [('before',a.before),('after',a.after)]:
   env=dict(os.environ,TP_DATA=str(profile.resolve()),TP_ART_MODE='faithful')
   env.pop('TP_WALK_SHELL_STUDY',None)
   for key in ('eye','pitch'):
    env.pop('TP_SNAPSHOT_'+key.upper(),None)
    if key in asset:env['TP_SNAPSHOT_'+key.upper()]=str(asset[key])
   model=profile/'visitor3d'/f"{asset['cls']:02x}-{asset['type']}.mesh"
   header=model.read_text().splitlines()[0].split()
   for view in range(a.views):
    yaw=-90+360*view/a.views;out=a.out/f'{slug}-{version}-{view:02}.png'
    cmd=[str(a.snapshot.resolve()),str(a.memory.resolve()),str(a.palette.resolve()),str(out.resolve()),str(a.map_width),'frame',str(asset['frame']),'0',str(yaw),'model',str(asset['cls']),str(asset['type'])]
    result=subprocess.run(cmd,env=env,text=True,capture_output=True,timeout=20)
    if result.returncode:raise RuntimeError(f"{slug} {version} {view}: {result.stderr}")
    data=out.read_bytes();assert data[:8]==b'\x89PNG\r\n\x1a\n' and struct.unpack('>II',data[16:24])==(960,600)
    runs.append(dict(asset=slug,version=version,view=view,yaw=yaw,triangles=int(header[2]),model_sha256=hashlib.sha256(model.read_bytes()).hexdigest(),image_sha256=hashlib.sha256(data).hexdigest(),log=result.stderr))
  print(slug+' paired views complete',flush=True)
 (a.out/'render-log.json').write_text(json.dumps(runs,indent=2)+'\n')
 cards=''.join(f'''<article><h2>{html.escape(s['name'])}</h2><p>{html.escape(s.get('changes',''))}</p><div class="pair"><figure><figcaption>{html.escape(a.before_label)}</figcaption><img data-slug="{s['slug']}" data-version="before" alt="{html.escape(s['name'])} before"></figure><figure><figcaption>{html.escape(a.after_label)}</figcaption><img data-slug="{s['slug']}" data-version="after" alt="{html.escape(s['name'])} refined"></figure></div></article>''' for s in assets)
 page='''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Faithful 3D geometry refinement</title><style>body{font:16px system-ui;background:#19232d;color:#eef2f7;max-width:1440px;margin:28px auto;padding:0 20px}p{line-height:1.6;max-width:95ch}a{color:#ffd077}.pair{display:grid;grid-template-columns:1fr 1fr;gap:12px}figure{margin:0}figcaption{padding:10px}img{width:100%;image-rendering:pixelated}article{margin:36px 0}nav{position:sticky;top:0;padding:10px;background:#233344;z-index:2}button,input{font:inherit;margin:6px;padding:8px}@media(max-width:700px){.pair{grid-template-columns:1fr}}</style>
 <h1>HEADING</h1><p>Paired captures from the native renderer, with identical camera, original palette pixels and nearest sampling. More depth is authored around existing details. Doorways remain closed surfaces. Hidden surfaces are inferred; full visual fidelity and animation coverage are not claimed.</p><nav><button id="play">Rotate</button><button id="front">Front</button><button id="quarter">Quarter</button><button id="rear">Rear</button><input id="angle" aria-label="Turntable angle" type="range" min="0" max="MAX" value="2"><span id="label"></span></nav>CARDS<p><a href="render-log.json">Capture evidence and model hashes</a></p><script>let n=2,timer=null;const total=TOTAL;const $=id=>document.getElementById(id);function show(){for(const img of document.querySelectorAll('[data-slug]'))img.src=`${img.dataset.slug}-${img.dataset.version}-${String(n).padStart(2,'0')}.png`;$('angle').value=n;$('label').textContent=`View ${n+1}/${total}`}$('angle').oninput=()=>{n=+$('angle').value;show()};$('front').onclick=()=>{n=0;show()};$('quarter').onclick=()=>{n=Math.round(total/12);show()};$('rear').onclick=()=>{n=Math.round(total/2);show()};$('play').onclick=()=>{if(timer){clearInterval(timer);timer=null;$('play').textContent='Rotate'}else{timer=setInterval(()=>{n=(n+1)%total;show()},180);$('play').textContent='Pause'}};show();</script></html>'''
 (a.out/'index.html').write_text(page.replace('HEADING',html.escape(a.title)).replace('MAX',str(a.views-1)).replace('TOTAL',str(a.views)).replace('CARDS',cards))
 print(f'{len(runs)} strict model captures completed')
if __name__=='__main__':main()
