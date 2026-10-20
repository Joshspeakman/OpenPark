#!/usr/bin/env python3
"""Summarize a bounded native animation observation; never infer unobserved states."""
import argparse,csv,json,statistics
from collections import defaultdict
from pathlib import Path

def summarize(path,catalog):
    rows=list(csv.DictReader(path.open()))
    assets={a['id']:a for a in catalog['assets']}
    seen=defaultdict(lambda:dict(actors=set(),states=set(),frames=set(),directions=set(),frame_holds=[],transitions=set()))
    prev={};changed={};total=0
    for row in rows:
        t=float(row.pop('seconds'));r={k:int(v) for k,v in row.items()};total+=1
        cls=r['class'];key=(f"guest:{r['variant']}" if cls==2 else
            f"ride:{r['record_offset']//200}" if cls==4 and r['record_offset']%200==0 else
            f"shop:{r['record_offset']//107}" if cls==10 and r['record_offset']%107==0 else f"class:{cls}/variant:{r['variant']}")
        if cls==0:continue
        a=seen[key];a['actors'].add(r['thing']);a['states'].add(r['state']);a['frames'].add(r['frame']);a['directions'].add(r['direction'])
        old=prev.get(r['thing'])
        if old and old[0]==key:
            p=old[1]
            if r['state']!=p['state']:a['transitions'].add((p['state'],r['state']))
            identity=(key,r['state'],r['direction'])
            before=changed.get(r['thing'])
            if r['frame']!=p['frame']:
                if before and before[0]==identity and 0<t-before[1]<10:a['frame_holds'].append(t-before[1])
                changed[r['thing']]=(identity,t)
            elif r['state']!=p['state'] or r['direction']!=p['direction']:changed.pop(r['thing'],None)
        prev[r['thing']]=(key,r)
    result=[]
    for key,a in sorted(seen.items()):
        holds=a.pop('frame_holds')
        result.append(dict(asset=key,name=assets.get(key,{}).get('name',key),**{k:sorted(v) for k,v in a.items()},
                           observed_frame_hold_ms=dict(samples=len(holds),median=round(statistics.median(holds)*1000,2),
                              minimum=round(min(holds)*1000,2),maximum=round(max(holds)*1000,2)) if holds else None))
    return dict(changed_records=total,observed_groups=result,
                limitations=['Samples are taken at most 50 Hz; intermediate transitions can be missed.',
                             'Frame holds are sampled observations, pooled across states; not an authored playback FPS.',
                             'Staff variants remain raw class/variant IDs pending role verification.',
                             'An unobserved asset/state has not passed validation.'])

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('trace',type=Path);p.add_argument('catalog',type=Path);p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();r=summarize(a.trace,json.loads(a.catalog.read_text()));a.out.write_text(json.dumps(r,indent=2)+'\n')
    print(f"{r['changed_records']} changed records; {len(r['observed_groups'])} observed groups")
if __name__=='__main__':main()
