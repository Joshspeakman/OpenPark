#!/usr/bin/env python3
"""Bounded, muted, background-window regression of the isolated remaster build.

Original mode verifies shared code, not remastered artwork. Reimagined requires
the complete pack. Captures only the owned process's windows; never the desktop.
"""
import argparse
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import tempfile
import time


def main():
    root=Path(__file__).resolve().parents[1]
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--art',choices=['original','reimagined'],default='original')
    a=p.parse_args()
    app=root/'build/visitor-study/OpenPark Reimagined Study.app'
    info=plistlib.loads((app/'Contents/Info.plist').read_bytes())
    if info.get('CFBundleIdentifier')!='com.joshspeakman.openpark.reimaginedstudy' or not info.get('TPRequireCompleteRemaster'):
        raise SystemExit('Refusing to run a different application')
    source=root/'work/remaster/profile'
    if a.art=='reimagined' and not (source/'reimagined/release.json').is_file():
        raise SystemExit('Complete Reimagined pack missing; no partial-art headed test')
    out=Path(tempfile.mkdtemp(prefix='runtime-',dir=root/'work/remaster'))
    profile=out/'profile';profile.mkdir()
    (profile/'game').symlink_to(source/'game',target_is_directory=True)
    shutil.copytree(source/'saves',profile/'saves')
    if a.art=='reimagined':
        shutil.copytree(source/'reimagined',profile/'reimagined')
    control=out/'control.txt'
    lines=['size 960 600','showbackground','sleep 5','key 1c0d','sleep 5']
    for key in ('1454','1245','1f53','1454','1950','1e41','1352','254b'):
        lines+=['key '+key,'sleep 0.15']
    lines+=['sleep 2','key 1c0d','sleep 5','key 3d00','sleep 9',
            'zoom 0.65','center 2200 1760','sleep 2',
            f'vshot {out/"normal.png"}',f'windowinfo {out/"normal.window.json"}',
            'ack normal','sleep 2','walk 56.12 114.7','walkteleport 56.12 114.7 -90','sleep 3',
            f'walkshot {out/"visitor.png"}',f'windowinfo {out/"visitor.window.json"}',
            'ack visitor','sleep 2','ack complete','quit']
    control.write_text('\n'.join(lines)+'\n')
    env=dict(os.environ,DS_TEST='1',W32_AUDIO_MUTE='1',TP_SKIP_INTRO='1',
             TP_DATA=str(profile),TP_CONTROL=str(control),TP_TEST='48:quit',
             TP_ART_MODE=a.art,TP_WALK_MESH='1',TP_WALK_MESH_LOG='1')
    result=dict(art_mode=a.art,app=str(app),status='failed',captures=[],
                validates_remastered_art=False,seconds=0)
    start=time.monotonic()
    with (out/'launch.log').open('wb') as log:
        proc=subprocess.Popen([str(app/'Contents/MacOS/ThemePark'),'-Sound','NO',
                               '-StartFullScreen','NO','-PlayIntro','NO','-FreeMapSize','128'],
                              env=env,stdout=log,stderr=log)
        seen=set()
        try:
            while proc.poll() is None and time.monotonic()-start<52:
                for view in ('normal','visitor'):
                    meta=out/(view+'.window.json')
                    if view in seen or not meta.is_file():
                        continue
                    try:
                        window=json.loads(meta.read_text())
                    except json.JSONDecodeError:
                        continue
                    seen.add(view)
                    valid=window['pid']==proc.pid and window['visible'] and window['window_id']>0
                    shot=out/(view+'-window.png')
                    capture=subprocess.run(['/usr/sbin/screencapture','-x','-l',str(window['window_id']),str(shot)],
                                           capture_output=True,timeout=5) if valid else None
                    result['captures'].append(dict(view=view,window=window,
                        captured=bool(capture and capture.returncode==0 and shot.is_file()),
                        error=capture.stderr.decode(errors='replace') if capture else 'not a visible owned window'))
                time.sleep(.1)
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=4)
                except subprocess.TimeoutExpired:
                    proc.kill();proc.wait()
        result['exit']=proc.returncode
    ack=Path(str(control)+'.ack')
    result['control_complete']=ack.is_file() and 'complete' in ack.read_text().splitlines()
    result['seconds']=round(time.monotonic()-start,2)
    if result['exit']==0 and result['control_complete'] and len(result['captures'])==2 and all(c['captured'] for c in result['captures']):
        result['status']='captured; visual inspection required'
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(output=str(out),**result),indent=2))
    return 0 if result['status'].startswith('captured') else 1


if __name__=='__main__':
    raise SystemExit(main())
