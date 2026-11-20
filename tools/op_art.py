#!/usr/bin/env python3
"""op_art.py <art folder>: the launch window's art for the drag-and-drop app's setup (recomp's setup.py, game.json "art"),
as tools/build_app.sh fetches it for an app built from a checkout: Theme Park's key art and logo from GOG's public store
API (keyart.jpg, logo.png), into the player's own data folder. Anything that can't be fetched is left out: the window
then draws without it. Standard library only."""
import json
import os
import sys
import urllib.request

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
try:
    with urllib.request.urlopen('https://api.gog.com/v2/games/1207660423?locale=en-US', timeout=30) as r:
        links = json.load(r)['_links']
except Exception:
    sys.exit(0)
for name, key in (('keyart.jpg', 'galaxyBackgroundImage'), ('logo.png', 'logo')):
    dest = os.path.join(out, name)
    if key not in links or (os.path.exists(dest) and os.path.getsize(dest) > 20000):
        continue
    try:
        with urllib.request.urlopen(links[key]['href'], timeout=60) as r:
            data = r.read()
        if len(data) > 20000 and data[:3] in (b'\xff\xd8\xff', b'\x89PN'):
            with open(dest + '.part', 'wb') as f:
                f.write(data)
            os.replace(dest + '.part', dest)
    except Exception:
        pass
print(' '.join(sorted(os.listdir(out))))
