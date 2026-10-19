#!/usr/bin/env python3
"""Inventory the owned game's loaded object definitions and animation chains.

Input: a 64 MiB TP_DUMP snapshot after the game loads its tables, and DATA/.
Output is game-derived: keep it under ignored work/. Does not edit game files.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path


def inventory(memory, data):
    m = memory.read_bytes()
    def word(at): return struct.unpack_from('<H', m, at)[0]
    def dword(at): return struct.unpack_from('<I', m, at)[0]
    starts = (data / 'MSTA-0.ANI').read_bytes()
    frames = (data / 'MFRA-0.ANI').read_bytes()
    elements = (data / 'MELE-0.ANI').read_bytes()
    lang = (data / 'LANG0-0.DAT').read_bytes()
    # First word is the number of 16-bit section lengths. Names are indexed
    # by object ID within the string stream following that header.
    header = (struct.unpack_from('<H', lang)[0] + 1) * 2
    strings = lang[header:].split(b'\0')
    def name(oid): return strings[oid].decode('latin1').strip()
    def chain(animation):
        if animation < 0 or animation * 2 + 2 > len(starts): return []
        f = struct.unpack_from('<H', starts, animation * 2)[0]
        seen = []
        while f and f not in seen and f * 8 + 8 <= len(frames):
            seen.append(f)
            f = struct.unpack_from('<H', frames, f * 8 + 6)[0]
        return seen
    rows = []
    for kind, base, count, stride in [('ride', 0x1cced0, 29, 200),
                                      ('shop', 0x1d3c62, 17, 107),
                                      ('scenery', 0x1cca92, 19, 56)]:
        for i in range(count):
            at = base + i * stride
            oid = word(at if kind == 'scenery' else at + 0x22)
            animations = ([word(at + 2)] if kind == 'scenery' else
                          [word(at + 0x26)] if kind == 'shop' else
                          [word(at + k) for k in (0x24, 0x26, 0x28)])
            animations = sorted(set(a for a in animations if a))
            fs = sorted(set(f for a in animations for f in chain(a)))
            label = name(oid)
            if kind != 'scenery':
                runtime_name = m[at:at+32].split(b'\0')[0].decode('latin1').strip()
                if runtime_name != label:
                    raise ValueError(f'name lookup mismatch: {kind} {i}: {label!r} != {runtime_name!r}')
            rows.append(dict(category=kind, index=i, object_id=oid, name=label,
                             animations=animations, frames=fs, frame_count=len(fs),
                             geometry='inferred source-textured shell',
                             source='runtime composite; tracked rides use placed track/car pieces' if not fs else 'original frame chains'))
    all_chains = [chain(a) for a in range(len(starts)//2)]
    return dict(snapshot_sha256=hashlib.sha256(m).hexdigest(),
                definitions=len(rows), objects=rows, animation_slots=len(all_chains),
                nonempty_animation_slots=sum(bool(c) for c in all_chains),
                unique_animation_frames=len(set(f for c in all_chains for f in c)),
                frame_records=len(frames)//8, element_records=len(elements)//10,
                sprite_records=(data/'MSPR-0.TAB').stat().st_size//6,
                ground_records=(data/'MBLK-0.TAB').stat().st_size//6,
                animation_chains=all_chains,
                limitations=['Record inventory is not proof of visual correctness.',
                             'Hidden surfaces and depth are inferred; no original 3D models exist.',
                             'Runtime composites retain the current frame and rider/overlay substitutions.'])


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('memory', type=Path); p.add_argument('data', type=Path)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args(); result = inventory(a.memory, a.data)
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text(json.dumps(result, indent=2)+'\n')
    print(f"{result['definitions']} definitions, {result['nonempty_animation_slots']} animation chains, "
          f"{result['unique_animation_frames']} unique frames -> {a.out}")
