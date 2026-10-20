#!/usr/bin/env python3
"""Map owned art records, animation graphs and runtime references, without guessing timing.

All generated output is game-derived and belongs under ignored work/.
Record reachability is not semantic/state coverage or completed replacement art.
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import html
import json
from pathlib import Path
import struct
from visitor_inventory import inventory
from remaster_tracks import track_families, attach_track_references


def graph(starts, frames, elements):
    if len(starts) % 2 or len(frames) % 8 or len(elements) % 10:
        raise ValueError('Misaligned animation tables')
    nf, ne = len(frames) // 8, len(elements) // 10
    nodes = []
    for i in range(nf):
        first, width, height, unknown, flags, nxt = struct.unpack_from('<HBBBBH', frames, i * 8)
        parts, seen, e = [], set(), first
        while e and e < ne and e not in seen:
            seen.add(e)
            spr, x, y, fl, link = struct.unpack_from('<HhhHH', elements, e * 10)
            parts.append(dict(element=e, sprite_offset=spr, sprite_index=spr // 6 if spr % 6 == 0 else None,
                              x=x, y=y, flags=fl, rider_marker=bool(fl & 128)))
            e = link
        nodes.append(dict(id=i, next=nxt, width=width, height=height, unknown=unknown, flags=flags,
                          parts=parts, element_termination='end' if not e else 'cycle' if e in seen else 'invalid',
                          element_terminal=e))
    chains = []
    for i, (first,) in enumerate(struct.iter_unpack('<H', starts)):
        order, seen, f = [], {}, first
        while f and f < nf and f not in seen:
            seen[f] = len(order); order.append(f); f = nodes[f]['next']
        chains.append(dict(id=i, start=first, frames=order,
                           termination='end' if not f else 'cycle' if f in seen else 'invalid',
                           terminal=f, loop_start=seen.get(f), timing='runtime driven; not encoded as a duration here'))
    used = {f for c in chains for f in c['frames']}
    return dict(chains=chains, frames=nodes, unreachable_frames=[i for i in range(1, nf) if i not in used])


def staff_assets(memory, base=0x1d35c0, limit=64):
    """Native 54-byte staff records, terminated by the zero word at +0x22.

    Name/record base: 0x14f729. Animation lookups: 0x128729 (+0x26 plus
    direction), 0x132366 (+0x28 plus direction), 0x132fa2 (+0x2a).
    0x1d35ec is the first record's +0x2c job kind, not its beginning.
    """
    if base < 0 or limit < 1 or limit > 64:
        raise ValueError('Invalid staff table bounds')
    assets = []
    for i in range(limit):
        at = base + i*54
        if at+54 > len(memory):
            raise ValueError('Truncated staff table or missing terminator')
        word = lambda offset: struct.unpack_from('<H', memory, at+offset)[0]
        if not word(0x22):
            return assets
        name = memory[at:at+32].split(b'\0')[0].decode('latin1')
        refs = [dict(field=f'+0x{o:x}', animation=word(o),
                     role='staff animation definition; activity name unverified')
                for o in (0x26, 0x28, 0x2a) if word(o)]
        for offset in (0x26, 0x28):
            if word(offset):
                refs.extend(dict(field=f'direction:0x{offset:x}:{d}',
                                 animation=word(offset)+d,
                                 role=f'directional staff group +0x{offset:x}; native byte 0x12 selects direction')
                            for d in range(4))
        assets.append(dict(id=f'staff:{i}', name=name, category='staff',
                           native_variant=i, record_offset=i*54,
                           references=refs, replacement='not animation validated'))
    raise ValueError('Staff table has no bounded terminator')


def build(memory, data):
    original = inventory(memory, data)
    m = memory.read_bytes()
    def word(at): return struct.unpack_from('<H', m, at)[0]
    families = {}
    for prefix in ('M', 'MD'):
        paths = [data / f'{prefix}{name}-0.ANI' for name in ('STA', 'FRA', 'ELE')]
        if all(p.exists() for p in paths):
            families[prefix] = graph(*(p.read_bytes() for p in paths))
    assets = []
    for a in original['objects']:
        refs = []
        if a['category'] == 'ride': base, offsets = 0x1cced0 + a['index'] * 200, (0x24, 0x26, 0x28)
        elif a['category'] == 'shop': base, offsets = 0x1d3c62 + a['index'] * 107, (0x26,)
        else: base, offsets = 0x1cca92 + a['index'] * 56, (2,)
        for off in offsets:
            v = word(base + off)
            if v: refs.append(dict(field=f'+0x{off:x}', animation=v, role='definition reference; semantic role pending verification'))
        assets.append(dict(id=f"{a['category']}:{a['index']}", name=a['name'], category=a['category'], references=refs,
                           composite_required=not bool(refs), replacement='not animation validated'))
    tracks = track_families(m)
    attach_track_references(assets, m, tracks)
    assets.extend(staff_assets(m))
    # Preserve all eighteen table entries, distinguishing direct frame values
    # from animation indices before assigning behavior names.
    for i in range(8):
        refs = [dict(field=f'slot:{slot}', animation=word(0x1b49f4 + i*36 + slot*2),
                     role='guest animation table; action/direction not yet named') for slot in range(18) if slot != 11]
        # 0x14222d/0x142363/0x143d36 copy slot 11 straight to word 10.
        # Treating it as an animation index invents unrelated frame ownership.
        frame_refs = [dict(field='slot:11', frame=word(0x1b49f4+i*36+22),
                           role='direct composite frame; native assignments 0x14222d, 0x142363, 0x143d36')]
        seated = [word(0x1b9a78 + i*94 + p*2) for p in range(47)]
        assets.append(dict(id=f'guest:{i}', name=f'Guest variant {i}', category='guest', references=refs,
                           frame_references=frame_refs,
                           seated_sprite_offsets=seated, alternate_seated_sprite_offsets=[word(0x1b9a58+i*4+p*2) for p in range(2)],
                           shop_action_sprite_offsets=[word(0x1b9d68+i*46+p*2) for p in range(23)],
                           replacement='same canonical model required walking/seated; not animation validated'))
        # Slot zero is the base of four adjacent walking-direction chains.
        # Verified against native state 8 and byte 0x12 (N/E/S/W) in live traces.
        refs[0]['role'] = 'walking base (state 8); direction selects base + 0..3'
        refs.extend(dict(field=f'walk_direction:{d}', animation=refs[0]['animation']+d,
                         role=f'walking {direction}; native state 8')
                    for d,direction in enumerate(('north','east','south','west')))
        # 0x125c11/0x125caf/0x13067a add byte 0x12 before the animation lookup.
        refs.extend(dict(field=f'action_direction:12:{d}', animation=word(0x1b49f4+i*36+24)+d,
                         role='directional action group; native lookup 0x125c11; action name unverified')
                    for d in range(4))
    owners = defaultdict(list)
    for a in assets:
        for ref in a['references']:
            if ref['animation'] < len(families['M']['chains']):
                owners[ref['animation']].append(dict(asset=a['id'], field=ref['field']))
            else: ref['invalid_animation_reference'] = True
    for c in families['M']['chains']: c['definition_owners'] = owners[c['id']]
    files = []
    for p in sorted(data.iterdir()):
        if not p.is_file(): continue
        raw = p.read_bytes()
        role = 'sprite table candidate; family/palette must be verified' if p.suffix.upper()=='.TAB' else 'unclassified resource'
        if p.suffix.upper()=='.ANI': role='animation table'
        if p.name.startswith(('MUSIC','SNDS')) or p.suffix.upper()=='.BNK': role='audio resource; retain sound/motion event alignment'
        if p.name.startswith('LANG'): role='localized text'
        video = None
        if p.name.startswith('RIDEANI.') and len(raw)>=16:
            size,magic,frames,width,height=struct.unpack_from('<IHHHH',raw)
            video=dict(ride=int(p.suffix[1:]),magic=hex(magic),frames=frames,width=width,height=height,
                       timing='native FLIC player; separate from park sprite timing',reported_bytes=size)
            role='first-person FLIC ride video, custom 16-byte header; not a park pose sequence'
        files.append(dict(name=p.name, bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest(),
                          records=len(raw)//6 if p.suffix.upper()=='.TAB' and len(raw)%6==0 else None,
                          role=role, video=video))
    checks={
        'ride':['construction/closed/open','start/run/slow/stop','loading/unloading','actual guest seat identity and order','speed overlay/shake','breakdown/repair','track/car assembly where applicable'],
        'shop':['idle/signage','service/customer positioning','purchase and carried item','closed/damaged state'],
        'scenery':['static silhouette and anchors','any frame loop','placement/selection overlay','directional parts/terrain connection'],
        'guest':['four walking directions','idle/queue/turn','eat/drink/items/balloons','emotions/illness/gestures','enter/seat/exit same identity','raised and special composite states'],
        'staff':['four walking directions','job activity','idle/turn/selection','shared special animation','entertainer routine or repair/sweep/patrol as applicable']}
    for a in assets:
        a['acceptance_checks']=[dict(check=s,status='needs verification; applicability per asset') for s in checks[a['category']]]
    return dict(schema=1, snapshot_sha256=original['snapshot_sha256'], assets=assets, families=families, files=files,
                track_families=tracks,
                counts={k:original[k] for k in ('sprite_records','ground_records','animation_slots','unique_animation_frames')},
                runtime_rules=[
                    dict(source='runtime/native_land.c:class2', rule='Guest main frame plus carried-item overlays; seat sprites substituted by guest variant; raised and special states exist.'),
                    dict(source='runtime/native_land.c:ride_draw', rule='Rider linked lists, capacity/kind, phase counter and seat element 0x80 determine identity and placement.'),
                    dict(source='runtime/native_land.c:class4', rule='Ride frame, speed-dependent overlay, shake, extra frames and raised states are separate channels.'),
                    dict(source='runtime/native_land.c:class8', rule='Staff main frame, shared special animation and job/selection markers are separate.'),
                    dict(source='runtime/native_land.c:class12', rule='Track bases, repeated supports and tick-selected caps retain a shared ground origin and distinct native heights; train composites need car/rider bindings.'),
                    dict(source='runtime/native_land.c:cell_things', rule='Static scenery uses decoration lookup; animated class 24 advances on tick mask 15.')],
                unresolved=['Meaning and transitions of every runtime state are not proven by these tables.',
                            'Frame links do not encode observed cadence. Preserve source frame/state; measure timing in native runs.',
                            'Track assemblies, overlays, UI families, effects and secondary animation tables need semantic mapping.',
                            'No completed Reimagined animation coverage is implied by a complete structural record inventory.'])


def write_review(out, catalog):
    # Inline metadata works from a local file without fetch/CORS or a server.
    payload = json.dumps(catalog, separators=(',', ':')).replace('<', '\\u003c')
    page = '''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>OpenPark asset and animation map</title><style>
body{font:15px system-ui;background:#18222d;color:#eee;margin:24px auto;max-width:1250px;padding:0 20px}a{color:#ffd17c}p{line-height:1.5}input,select,button{font:inherit;padding:8px;margin:4px}table{width:100%;border-collapse:collapse}td,th{padding:8px;border-bottom:1px solid #435264;text-align:left}pre{white-space:pre-wrap}img{image-rendering:pixelated;max-width:100%;max-height:420px}#viewer{position:sticky;top:0;background:#243240;padding:16px;display:flex;gap:24px;align-items:center}#preview{min-width:220px}small{color:#b7c7d6}</style>
<h1>Asset and animation map</h1><p>Structural source inventory, with ownership and unresolved state/timing work recorded separately. Preview playback follows original frame links at an adjustable review rate; it does not claim original game timing.</p>
<nav><a href="../index.html">Art review</a> · <a href="catalog.json">Full catalog JSON</a></nav>
<div id="viewer"><div id="preview"><img id="picture" alt="Original source frame"></div><div><strong id="caption">Choose an animation</strong><p id="detail"></p><button id="play">Play</button><button id="step">Step</button><label>Review FPS <input id="fps" type="number" min="1" max="30" value="8"></label><p><input id="position" type="range" min="0" max="0" value="0"></p></div></div>
<input id="search" placeholder="Filter asset name, type or animation"><select id="family"><option>M</option><option>MD</option></select><select id="scope"><option value="assets">Asset definitions</option><option value="animations">All animation slots</option><option value="files">All resource files</option></select><p id="counts"></p><table><thead id="head"></thead><tbody id="rows"></tbody></table>
<h2>Unresolved verification</h2><ul id="limits"></ul><script>const catalog=PAYLOAD;
const $=id=>document.getElementById(id);let selected=null,index=0,timer=null;
const esc=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
function show(){if(!selected)return;const fs=selected.frames;if(!fs.length){$('picture').removeAttribute('src');$('caption').textContent='Empty animation';return}index=Math.min(index,fs.length-1);$('position').value=index;if($('family').value==='M'){$('picture').src=`frames/M-${fs[index]}.png`;$('picture').style.height='220px'}else $('picture').removeAttribute('src');$('caption').textContent=`Animation ${selected.id} · frame ${fs[index]} · ${index+1}/${fs.length}`;$('detail').textContent=`${selected.termination}; loop starts at ${selected.loop_start??'none'}. ${selected.timing}`+($('family').value==='MD'?' Secondary-family image export pending.':' Cropped source frame; anchor alignment must be checked in game.')}
function choose(id){selected=catalog.families[$('family').value].chains[id];index=0;if(!selected){$('picture').removeAttribute('src');$('caption').textContent='Reference belongs to the primary M family';return}$('position').max=Math.max(0,selected.frames.length-1);show()}
function advance(){if(!selected)return;if(index+1<selected.frames.length)index++;else if(selected.loop_start!==null)index=selected.loop_start;else{clearInterval(timer);timer=null;$('play').textContent='Play'}show()}
$('play').onclick=()=>{if(timer){clearInterval(timer);timer=null;$('play').textContent='Play'}else{timer=setInterval(advance,1000/Math.max(1,Math.min(30,+$('fps').value)));$('play').textContent='Pause'}};$('step').onclick=advance;$('position').oninput=()=>{index=+$('position').value;show()};
const button=i=>`<button data-animation="${i}">${i}</button>`;
function render(){const q=$('search').value.toLowerCase(),scope=$('scope').value;let rows=[];
if(scope==='assets'){$('head').innerHTML='<tr><th>ID</th><th>Asset</th><th>Animation / direct frame refs</th><th>Status</th></tr>';for(const a of catalog.assets)if(JSON.stringify(a).toLowerCase().includes(q))rows.push(`<tr><td>${esc(a.id)}</td><td>${esc(a.name)}</td><td>${a.references.map(r=>button(r.animation)).join(' ')||'Placed composite required'} ${(a.frame_references||[]).map(r=>`<span>Frame ${esc(r.frame)} (${esc(r.field)})</span>`).join(' ')}</td><td>${esc(a.replacement)}<details><summary>Motion checks</summary>${a.acceptance_checks.map(c=>`<p>${esc(c.check)} — ${esc(c.status)}</p>`).join('')}</details></td></tr>`)}
else if(scope==='animations'){$('head').innerHTML='<tr><th>Animation</th><th>Frames</th><th>Termination</th><th>Owners</th></tr>';for(const c of catalog.families[$('family').value].chains)if(JSON.stringify(c).toLowerCase().includes(q))rows.push(`<tr><td>${button(c.id)}</td><td>${c.frames.length}</td><td>${esc(c.termination)} ${c.loop_start??''}</td><td>${esc((c.definition_owners||[]).map(o=>o.asset).join(', '))||'No definition owner; still in catalog'}</td></tr>`)}
else{$('head').innerHTML='<tr><th>File</th><th>Bytes</th><th>Records</th><th>Role</th></tr>';for(const f of catalog.files)if(JSON.stringify(f).toLowerCase().includes(q))rows.push(`<tr><td>${esc(f.name)}</td><td>${f.bytes}</td><td>${f.records??''}</td><td>${esc(f.role)}</td></tr>`)}
$('rows').innerHTML=rows.join('');$('counts').textContent=`${rows.length} rows · ${catalog.assets.length} definition/guest records · ${catalog.files.length} resource files`;
for(const b of document.querySelectorAll('[data-animation]'))b.onclick=()=>choose(+b.dataset.animation)}
for(const id of ['search','family','scope'])$(id).oninput=()=>{clearInterval(timer);timer=null;selected=null;$('picture').removeAttribute('src');$('caption').textContent='Choose an animation';$('play').textContent='Play';render()};$('limits').innerHTML=catalog.unresolved.map(x=>`<li>${esc(x)}</li>`).join('');render();</script></html>'''
    (out/'index.html').write_text(page.replace('PAYLOAD', payload))


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('memory',type=Path);p.add_argument('data',type=Path);p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();c=build(a.memory,a.data);a.out.mkdir(parents=True,exist_ok=True)
    (a.out/'catalog.json').write_text(json.dumps(c,indent=2)+'\n');write_review(a.out,c)
    print(json.dumps(dict(assets=len(c['assets']),files=len(c['files']),families={k:dict(chains=len(v['chains']),frames=len(v['frames']),unreachable=len(v['unreachable_frames'])) for k,v in c['families'].items()})))


if __name__=='__main__':main()
