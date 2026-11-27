#!/usr/bin/env python3
"""store_art.py ART_DIR: Theme Park's store art and park pictures from GOG's public store API, for the launch window and
the icon on Windows and Linux: what tools/build_app.sh's fetch_art and fetch_park_shots fetch on the Mac, with the
standard library only (no sips, curl or image libraries).

  keyart.jpg, logo.png, box.jpg   GOG's key art, logo and box art, as GOG serves them (JPEG and PNG); each kept only if
                                  over 20,000 bytes and a whole picture, written as .part and then renamed, and never
                                  fetched again once there
  park-shot-<n>.png               up to 4 of GOG's screenshots that show a park, picked by their pixels (over the top 80 %,
                                  every 6th pixel each way: 30-55 % lawn, g > r + 15 and g > b + 15; the world map is
                                  nearly all green), cut to their top 85 % (above the game's bar); none fetched if any is
                                  there

ART_DIR is the player's own data folder's art/ (never this project). TP_NO_DOWNLOADS=1 skips it all. Whatever can't be
fetched is left out (the launcher then draws without it, and the icon uses GOG's icon from the package): it always exits
0. GOG's screenshots are PNG (their formatter URLs end in .png), read with tools/ico_image.py's decoder; a cut keeps the
file's own rows, so its pixels are GOG's exactly."""
import json
import os
import struct
import sys
import urllib.request
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ico_image  # noqa: E402

GAME = 1207660423
ART_API = 'https://api.gog.com/v2/games/%d?locale=en-US' % GAME
SHOTS_API = 'https://api.gog.com/products/%d?expand=screenshots' % GAME
ART = (('keyart.jpg', 'galaxyBackgroundImage'), ('logo.png', 'logo'), ('box.jpg', 'boxArtImage'))
PNG = b'\x89PNG\r\n\x1a\n'


def get(url, timeout):
    with urllib.request.urlopen(url, timeout=timeout) as r:
        return r.read()


def png_chunks(data):
    """[(kind, body)] of a PNG, every chunk's CRC checked; ValueError if it is not a whole PNG."""
    if data[:8] != PNG:
        raise ValueError('not a PNG')
    at, out = 8, []
    while at + 12 <= len(data):
        n, kind = struct.unpack('>I4s', data[at:at + 8])
        body = data[at + 8:at + 8 + n]
        if len(body) != n or struct.unpack('>I', data[at + 8 + n:at + 12 + n])[0] != zlib.crc32(kind + body):
            raise ValueError('a damaged chunk')
        out.append((kind, body))
        at += 12 + n
        if kind == b'IEND':
            return out
    raise ValueError('cut short')


def readable(data):
    """True for a whole JPEG or PNG (what the Mac asks sips): a PNG's chunks and pixel data, a JPEG's frame header,
    scan and end marker."""
    try:
        if data[:8] == PNG:
            chunks = png_chunks(data)
            w, h = struct.unpack('>II', chunks[0][1][:8])
            return chunks[0][0] == b'IHDR' and w > 0 and h > 0 and len(zlib.decompress(b''.join(b for k, b in chunks if k == b'IDAT'))) > 0
        if data[:3] == b'\xff\xd8\xff':
            at, size = 2, None
            while at + 4 <= len(data) and data[at] == 0xff:
                marker, n = data[at + 1], struct.unpack('>H', data[at + 2:at + 4])[0]
                if marker in (0xc0, 0xc1, 0xc2, 0xc3, 0xc5, 0xc6, 0xc7, 0xc9, 0xca, 0xcb, 0xcd, 0xce, 0xcf):
                    size = struct.unpack('>HH', data[at + 5:at + 9])
                if marker == 0xda:              # (the scan: entropy-coded data to the end marker, which it can't hold)
                    return bool(size and all(size)) and data.rfind(b'\xff\xd9') > at
                at += 2 + n
    except (ValueError, struct.error, zlib.error, IndexError):
        pass
    return False


def fetch_art(out, say=print):
    """keyart.jpg, logo.png and box.jpg into `out` (build_app.sh's fetch_art)."""
    try:
        links = json.loads(get(ART_API, 30))['_links']
    except Exception:
        say("   (GOG's store art could not be listed)")
        return
    for name, key in ART:
        dest = out / name
        if key not in links or (dest.exists() and dest.stat().st_size > 0):
            continue
        part = out / (name + '.part')
        try:
            data = get(links[key]['href'], 60)
            if len(data) <= 20000 or not readable(data):
                raise ValueError('not a picture')
            part.write_bytes(data)
            os.replace(part, dest)
        except Exception:
            part.unlink(missing_ok=True)
            say('   (%s could not be downloaded from GOG)' % name)


def is_park(w, h, rows):
    """build_app.sh's test: over the top 80 % of rows, every 6th pixel each way, 30-55 % of them lawn."""
    green = total = 0
    for y in range(0, int(h * 0.8), 6):
        row = rows[y]
        for x in range(0, w, 6):
            r, g, b = row[4 * x], row[4 * x + 1], row[4 * x + 2]
            total += 1
            green += g > r + 15 and g > b + 15
    return total * 0.3 < green < total * 0.55


def crop_png(data, height):
    """The top `height` rows of a non-interlaced PNG: its own filtered rows (each depends only on the rows above it),
    recompressed under a shorter header; its palette, transparency and colour chunks kept."""
    chunks = png_chunks(data)
    w, h, depth, ctype, comp, filt, interlace = struct.unpack('>IIBBBBB', chunks[0][1])
    if interlace or not 0 < height <= h:
        raise ValueError('cannot cut this PNG')
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    stride = (w * channels * depth + 7) // 8
    raw = zlib.decompress(b''.join(b for k, b in chunks if k == b'IDAT'))[:height * (stride + 1)]
    if len(raw) != height * (stride + 1):
        raise ValueError('cut short')
    chunk = lambda k, b: struct.pack('>I', len(b)) + k + b + struct.pack('>I', zlib.crc32(k + b))
    keep = b''.join(chunk(k, b) for k, b in chunks if k in (b'PLTE', b'tRNS', b'gAMA', b'cHRM', b'sRGB', b'iCCP', b'sBIT'))
    return (PNG + chunk(b'IHDR', struct.pack('>IIBBBBB', w, height, depth, ctype, comp, filt, 0)) + keep
            + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def park_shots(out, say=print, most=4):
    """park-shot-<n>.png into `out` (build_app.sh's fetch_park_shots)."""
    if any(f.name.startswith('park-shot-') and f.suffix == '.png' for f in out.iterdir()):
        return
    try:
        urls = [x['formatter_template_url'].replace('_{formatter}', '') for x in json.loads(get(SHOTS_API, 30)).get('screenshots', [])]
    except Exception:
        say("   (GOG's screenshots could not be listed)")
        return
    k = 0
    for url in urls:
        try:
            data = get(url, 60)
            w, h, rows = ico_image._png_rgba(data)
            if not is_park(w, h, rows):
                continue
            shot = crop_png(data, h * 85 // 100)
        except Exception:
            continue
        k += 1
        dest = out / ('park-shot-%d.png' % k)
        part = out / (dest.name + '.part')
        part.write_bytes(shot)
        os.replace(part, dest)
        if k >= most:
            break
    say("   %d of GOG's park pictures for the launch window" % k)


def main():
    if len(sys.argv) != 2 or sys.argv[1].startswith('-'):
        sys.exit(__doc__)
    if os.environ.get('TP_NO_DOWNLOADS'):
        return
    out = Path(sys.argv[1])
    try:
        out.mkdir(parents=True, exist_ok=True)
        fetch_art(out)
        park_shots(out)
    except OSError as e:
        print("   (GOG's art could not be saved: %s)" % e)


if __name__ == '__main__':
    main()
