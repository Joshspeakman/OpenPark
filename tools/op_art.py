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


def park_shots():
    """GOG's screenshots that show a park (mostly lawn among paths and rides), cut above the game's bar, as
    park-shot-<n>.png: the launch window's backdrop until the player's own park picture exists (the app saves it at
    quit). Picked by their pixels (sips makes a BMP to read), not their place in GOG's list."""
    import struct, subprocess, tempfile
    if any(f.startswith('park-shot-') for f in os.listdir(out)):
        return
    try:
        with urllib.request.urlopen('https://api.gog.com/products/1207660423?expand=screenshots', timeout=30) as r:
            urls = [x['formatter_template_url'].replace('_{formatter}', '') for x in json.load(r).get('screenshots', [])]
    except Exception:
        return
    k = 0
    with tempfile.TemporaryDirectory() as tmp:
        for n, url in enumerate(urls):
            png, bmp = os.path.join(tmp, f's{n}.png'), os.path.join(tmp, f's{n}.bmp')
            try:
                with urllib.request.urlopen(url, timeout=60) as r:
                    open(png, 'wb').write(r.read())
                subprocess.run(['sips', '-s', 'format', 'bmp', png, '--out', bmp], check=True, capture_output=True)
            except Exception:
                continue
            d = open(bmp, 'rb').read(); off = struct.unpack_from('<I', d, 10)[0]; w, h = struct.unpack_from('<ii', d, 18)
            bpp = struct.unpack_from('<H', d, 28)[0] // 8; row = (w * bpp + 3) & ~3; top = h < 0; h = abs(h); green = total = 0
            for y in range(0, int(h * 0.8), 6):
                yy = y if top else h - 1 - y
                for x in range(0, w, 6):
                    o = off + yy * row + x * bpp; b, g, rr = d[o], d[o + 1], d[o + 2]
                    total += 1; green += g > rr + 15 and g > b + 15
            if not (total * 0.3 < green < total * 0.55):
                continue
            k += 1
            subprocess.run(['sips', '-c', str(h * 85 // 100), str(w), '--cropOffset', '0', '0', png, '--out',
                            os.path.join(out, f'park-shot-{k}.png')], capture_output=True)
            if k >= 4:
                break


park_shots()
