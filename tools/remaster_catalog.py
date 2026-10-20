#!/usr/bin/env python3
"""Exhaustive local remaster work ledger and fail-closed release audit.

Unlike a definition list, this includes orphan frames, every source part,
seated substitutions, ground, videos and unclassified resources. A concept,
recolour or original-art fallback is never counted as a finished replacement.
Generated ledgers contain owned-game data: keep them in ignored work/.
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import struct

from visitor_animation_catalog import build
from sprites import decode


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def pixel_hash(rows):
    """FNV-1a of little-endian 16-bit indices; 256 is transparent."""
    h = 14695981039346656037
    for row in rows:
        for pixel in row:
            v = 256 if pixel is None else pixel
            for b in (v & 255, v >> 8):
                h = ((h ^ b) * 1099511628211) & ((1 << 64) - 1)
    return h


def bank_records(data, name, bank):
    tab = (data / (name + '.TAB')).read_bytes()
    raw = (data / (name + '.DAT')).read_bytes()
    if len(tab) % 6:
        raise ValueError(f'Misaligned bank: {name}')
    records = []
    for i, (offset, w, h) in enumerate(struct.iter_unpack('<IBB', tab)):
        _, _, rows = decode(raw, offset, w, h)
        records.append(dict(id=f'{name}:{i}', bank=bank, index=i, width=w, height=h, source_offset=offset,
                            source_hash=f'{pixel_hash(rows):016x}',
                            empty=not any(p is not None for r in rows for p in r),
                            owners=[], frame_uses=[], seated_uses=[]))
    return records


def make_ledger(memory, data):
    catalog = build(memory, data)
    parts = bank_records(data, 'MSPR-0', 0) + bank_records(data, 'MBLK-0', 1)
    by_id = {p['id']: p for p in parts}
    frames, animations, problems = [], [], []
    for family, graph in catalog['families'].items():
        owners = defaultdict(set)
        # Definition refs are proven only for M. Do not assign MD meanings by ID.
        if family == 'M':
            for a in catalog['assets']:
                for ref in a['references']:
                    if ref['animation'] < len(graph['chains']):
                        for f in graph['chains'][ref['animation']]['frames']:
                            owners[f].add(a['id'])
                for ref in a.get('frame_references', []):
                    if 0 < ref['frame'] < len(graph['frames']):
                        owners[ref['frame']].add(a['id'])
                    else:
                        problems.append(dict(id=a['id'], problem='invalid direct frame reference', field=ref['field']))
        for frame in graph['frames']:
            if frame['id'] == 0:
                continue
            fid = f"{family}:frame:{frame['id']}"
            dependencies = []
            for part in frame['parts']:
                key = f"MSPR-0:{part['sprite_index']}"
                if part['sprite_offset'] == 0:
                    continue
                if key not in by_id:
                    problems.append(dict(id=fid, problem='unresolved sprite reference', part=part))
                    continue
                dependencies.append(key)
                by_id[key]['frame_uses'].append(fid)
                by_id[key]['owners'].extend(owners[frame['id']])
            frames.append(dict(id=fid, next=frame['next'], parts=sorted(set(dependencies)),
                               owners=sorted(owners[frame['id']]),
                               rider_slots=sum(p['rider_marker'] for p in frame['parts']),
                               empty=not dependencies,
                               unreachable=frame['id'] in graph['unreachable_frames']))
            if frame['element_termination'] != 'end':
                problems.append(dict(id=fid, problem='element chain ' + frame['element_termination']))
        for chain in graph['chains']:
            animations.append(dict(id=f"{family}:animation:{chain['id']}",
                                   frames=[f'{family}:frame:{f}' for f in chain['frames']],
                                   termination=chain['termination'], loop_start=chain['loop_start'],
                                   timing='native simulation; never an independent playback clock'))
            if chain['termination'] == 'invalid':
                problems.append(dict(id=animations[-1]['id'], problem='invalid frame reference'))
    for a in catalog['assets']:
        for field in ('seated_sprite_offsets', 'alternate_seated_sprite_offsets', 'shop_action_sprite_offsets'):
            for pose, offset in enumerate(a.get(field, [])):
                if not offset:
                    continue
                key = f'MSPR-0:{offset // 6}'
                if offset % 6 or key not in by_id:
                    problems.append(dict(id=a['id'], problem='unresolved substituted sprite', table=field, offset=offset))
                    continue
                by_id[key]['seated_uses'].append(dict(guest=a['id'], table=field, pose=pose))
                by_id[key]['owners'].append(a['id'])
    for part in parts:
        part['owners'] = sorted(set(part['owners']))
        part['frame_uses'] = sorted(set(part['frame_uses']))
        part['unowned'] = not part['owners']
        part['required'] = not part['empty']
    resources = []
    # Only explicit interface families are exempt. Unknown art remains a blocker.
    ui = ('MFONT-', 'MPANEL-', 'HPANEL-', 'MPOINTER', 'MREQ-', 'MMENU-')
    audio = ('MUSIC', 'SNDS', 'INST.BNK', 'DRUM.BNK')
    known = {'MSPR-0.TAB', 'MSPR-0.DAT', 'MBLK-0.TAB', 'MBLK-0.DAT',
             'MPALETTE.DAT', 'MSTA-0.ANI', 'MFRA-0.ANI', 'MELE-0.ANI',
             'MDSTA-0.ANI', 'MDFRA-0.ANI', 'MDELE-0.ANI'}
    for f in catalog['files']:
        name = f['name']
        role = ('interface-exempt' if name.startswith(ui) else
                'nonvisual-exempt' if name.startswith(audio) or name.startswith('LANG') else
                'covered-by-records' if name in known else
                'ride-video' if f.get('video') else 'scope-unresolved')
        resources.append(dict(name=name, sha256=f['sha256'], role=role,
                              video=f.get('video'), bytes=f['bytes']))
    return dict(schema=1, art_mode='reimagined', source_snapshot_sha256=catalog['snapshot_sha256'],
                constraints=dict(ui_exempt=True, both_views=True, native_timing=True,
                                 canonical_walking_and_riding_identity=True,
                                 original_fallback_is_complete=False),
                assets=catalog['assets'], parts=parts, frames=frames,
                animations=animations, resources=resources, source_problems=problems,
                runtime_channels=[
                    'track_assembly_tubing', 'track_assembly_race_cars',
                    'track_assembly_coaster', 'track_assembly_big_dipper', 'track_assembly_monorail',
                    'vehicle_train_assembly', 'seated_guest_substitution', 'shop_customer_substitution',
                    'ride_speed_damage_overlays', 'guest_carried_items', 'guest_thoughts_and_expressions',
                    'park_entrance_and_gates', 'animated_scenery', 'world_signs',
                    'litter_and_cleanup', 'weather_streaks', 'weather_particles', 'lightning',
                    'construction_and_placement_previews'],
                integration_requirements=[
                    'shared_models_installed', 'normal_view_no_fallback', 'visitor_view_no_fallback',
                    'custom_coaster_layouts', 'custom_tubing_layouts', 'all_rider_identities',
                    'expression_and_item_transitions', 'launcher_selection_persists',
                    'headed_normal_capture', 'headed_visitor_capture', 'busy_park_performance',
                    'ui_unchanged', 'source_simulation_unchanged'])


def verified_file(root, record, field):
    value = record.get(field)
    if not isinstance(value, dict) or set(value) != {'path', 'sha256'}:
        return False
    path = (root / value['path']).resolve()
    if not path.is_relative_to(root.resolve()) or not path.is_file():
        return False
    return sha(path) == value['sha256']


def audit(ledger, completion, root):
    """An inventory is not coverage. Require current artifacts and evidence.

    This checks completeness and evidence integrity, not the truth of an artist's
    visual judgement. Evidence must be reviewed separately before release.
    """
    blockers, complete = [], Counter()
    for part in ledger['parts']:
        if not part['required']:
            continue
        done = completion.get('parts', {}).get(part['id'], {})
        fields = ('canonical_model', 'overhead_bake', 'visitor_capture', 'normal_capture')
        missing = [f for f in fields if not verified_file(root, done, f)]
        if done.get('source_hash') != part['source_hash']:
            missing.append('source binding')
        if done.get('treatment') != 'authored-remaster':
            missing.append('authored remaster treatment')
        if not done.get('visual_review', {}).get('accepted'):
            missing.append('visual review')
        if missing:
            blockers.append(dict(id=part['id'], missing=missing))
        else:
            complete['parts'] += 1
    for animation in ledger['animations']:
        if not animation['frames']:
            continue
        done = completion.get('animations', {}).get(animation['id'], {})
        missing = [f for f in ('state_trace', 'normal_capture', 'visitor_capture')
                   if not verified_file(root, done, f)]
        if done.get('frames') != animation['frames'] or done.get('loop_start') != animation['loop_start']:
            missing.append('complete source sequence and loop')
        if not done.get('riders_expressions_items_checked'):
            missing.append('riders/expressions/carried items')
        if missing:
            blockers.append(dict(id=animation['id'], missing=missing))
        else:
            complete['animations'] += 1
    for resource in ledger['resources']:
        if resource['role'] in ('scope-unresolved', 'ride-video'):
            done = completion.get('resources', {}).get(resource['name'], {})
            # Any additional UI exemption needs a documented source-code reason.
            exempt = (resource['role'] == 'scope-unresolved' and done.get('scope') == 'interface-exempt'
                      and verified_file(root, done, 'scope_evidence'))
            if not exempt and not all(verified_file(root, done, f)
                                      for f in ('replacement', 'validation')):
                blockers.append(dict(id=resource['name'], missing=[resource['role']]))
    for asset in ledger['assets']:
        done = completion.get('assets', {}).get(asset['id'], {})
        if not verified_file(root, done, 'state_coverage'):
            blockers.append(dict(id=asset['id'], missing=['semantic states and transitions']))
    for check in ledger.get('integration_requirements', []):
        done=completion.get('integration',{}).get(check,{})
        if done.get('passed') is not True or not verified_file(root,done,'evidence'):
            blockers.append(dict(id=f'integration:{check}',missing=['verified integration evidence']))
    for channel in ledger.get('runtime_channels', []):
        done=completion.get('runtime_channels',{}).get(channel,{})
        missing=[f for f in ('bindings','normal_capture','visitor_capture')
                 if not verified_file(root,done,f)]
        if missing:
            blockers.append(dict(id=f'channel:{channel}',missing=missing))
    blockers.extend(ledger['source_problems'])
    return dict(complete=not blockers, verified_counts=dict(complete), blocker_count=len(blockers),
                blockers=blockers,
                note='Passing is necessary, not sufficient: inspect visual and timing evidence.')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('memory', type=Path)
    ap.add_argument('data', type=Path)
    ap.add_argument('--out', required=True, type=Path)
    ap.add_argument('--completion', type=Path)
    ap.add_argument('--require-complete', action='store_true')
    a = ap.parse_args()
    ledger = make_ledger(a.memory, a.data)
    completion = json.loads(a.completion.read_text()) if a.completion else {}
    report = audit(ledger, completion, a.completion.parent if a.completion else a.out)
    a.out.mkdir(parents=True, exist_ok=True)
    for name, value in [('ledger', ledger), ('audit', report)]:
        (a.out / f'{name}.json').write_text(json.dumps(value, indent=2) + '\n')
    print(json.dumps(dict(assets=len(ledger['assets']), parts=len(ledger['parts']),
                          required_parts=sum(p['required'] for p in ledger['parts']),
                          frames=len(ledger['frames']), animations=len(ledger['animations']),
                          source_problems=len(ledger['source_problems']),
                          complete=report['complete'], blockers=report['blocker_count'])))
    if a.require_complete and not report['complete']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
