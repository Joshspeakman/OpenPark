"""Read the owned game's modular track records, without inventing state names.

The family header supplies the cap animation base and segment-table pointer.
native_land.c class12 consumes +0/+2; car selection at 0x11de7f consumes
+5+direction*2. Construction at 0x129bb0 selects cap_base+shape. Unknown
record fields remain raw; references alone do not establish animation coverage.
"""
import struct


def track_families(memory, table=0x1b4397, counts=(21, 23, 11)):
    if table < 0 or table+40 > len(memory) or len(counts) != 3:
        raise ValueError('Invalid track family table')
    result = []
    for family, count in enumerate(counts, 1):
        if count < 1 or count > 256:
            raise ValueError('Invalid track segment count')
        at = table+family*10
        cap, pointer, a, b = struct.unpack_from('<HIHH', memory, at)
        if not pointer or pointer+count*25 > len(memory) or cap+count > 65536:
            raise ValueError('Invalid track segment table or cap range')
        segments = []
        for shape in range(count):
            address = pointer+shape*25
            word = lambda off: struct.unpack_from('<H', memory, address+off)[0]
            refs = [dict(field='base', animation=word(0)),
                    dict(field='stack', animation=word(2)),
                    dict(field='cap', animation=cap+shape)]
            if family == 3:
                # class12 bypasses the record's base/stack for monorail.
                refs = [dict(field='base', animation=469),
                        dict(field='cap', animation=cap+shape)]
                if shape == 0:
                    refs.append(dict(field='construction_ground', animation=689))
            refs.extend(dict(field=f'car_direction:{d}', animation=word(5+d*2))
                        for d in range(4) if word(5+d*2))
            segments.append(dict(shape=shape, address=address, references=refs,
                                 connection_mask=memory[address+17],
                                 slope_fields=list(struct.unpack_from('<bbb', memory, address+18)),
                                 raw_record=memory[address:address+25].hex()))
        result.append(dict(family=family, cap_animation_base=cap,
                           record_pointer=pointer, unknown_header_words=[a, b],
                           runtime_references=([dict(field='train_foreground', animation=366)]
                                               if family == 1 else []),
                           segments=segments,
                           status='source references; geometry and live state coverage unaccepted'))
    return result


def attach_track_references(assets, memory, families):
    by_family = {f['family']: f for f in families}
    for asset in assets:
        if asset['category'] != 'ride':
            continue
        ident = int(asset['id'].split(':')[1])
        family = struct.unpack_from('<b', memory, 0x1cced0+ident*200+0xb4)[0]
        if family not in by_family:
            continue  # Ground surfaces and vehicles still require their own map.
        asset['track_family'] = family
        asset['composite_required'] = True
        for ref in by_family[family]['runtime_references']:
            asset['references'].append(dict(field=f"track:{family}:{ref['field']}",
                animation=ref['animation'], role='native train_draw foreground component'))
        for segment in by_family[family]['segments']:
            for ref in segment['references']:
                asset['references'].append(dict(
                    field=f"track:{family}:{segment['shape']}:{ref['field']}",
                    animation=ref['animation'],
                    role='modular track definition reference; native state applicability requires verification'))
