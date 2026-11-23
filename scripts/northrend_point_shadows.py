"""Point shadow products and batching evidence from a certified replay."""
import hashlib
import json
import math
import re
import struct

from capture_files import read_bytes
from northrend_frame_parity import pixel_difference


FACE_HEADER = struct.pack('<4s4I', b'FGR1', 256, 256, 114, 4)


def census(lines, requested):
    rows = {str(frame): [] for frame in requested}
    for line in lines:
        if not line.startswith('POINT_BATCH_AUDIT rendererFrame='):
            continue
        values = {key: int(value) for key, value in re.findall(r'(\w+)=(\d+)', line)}
        frame = str(values.pop('rendererFrame'))
        if frame in rows:
            rows[frame].append(values)
    for frame, faces in rows.items():
        keys = {(row['slot'], row['face']) for row in faces}
        if not faces or len(keys) != len(faces) or any(
                row['slot'] >= 128 or row['face'] >= 6 or
                row['packets'] != row['opaque'] + row['cutout'] + row['unknownBounds'] or
                not 0 <= row['singleRuns'] <= row['single'] <= row['opaque'] or
                row['single'] + row['weighted'] > row['opaque'] for row in faces):
            raise ValueError(f'Incomplete point batching census at Present {frame}')
    return rows


def cube_products(folder, frames):
    products = {}
    for frame in frames:
        rows = [json.loads(line) for line in
                (folder / f'capture-{frame}-point-shadow-cubes.jsonl').read_text(encoding='utf-8').splitlines()]
        if (not rows or rows[0].get('scope') != 'point-shadow-cubes' or
                rows[0].get('renderer_frame') != frame or rows[0].get('capture') != frame or
                rows[-1].get('event') != 'end' or rows[-1].get('export_complete') is not True):
            raise ValueError(f'Incomplete cube export at Present {frame}')
        if [row['stage'] for row in rows if row['event'] == 'point-source-stage'] != ['point-correction-after-refresh']:
            raise ValueError(f'Uncertified cube stage at Present {frame}')
        files = {row['name']: row for row in rows if row['event'] == 'file'}
        slots = {row['slot']: row for row in rows if row['event'] == 'point-publication'}
        sources = {}
        for source in rows:
            if source['event'] != 'point-source' or not source['enabled'] or not source['ready']:
                continue
            slot, source_id = source['slot'], source['source_id']
            slot_info = slots.get(slot)
            if (not slot_info or not source['usable'] or slot_info['schedule_source_id'] != source_id or
                    source_id in sources or not re.fullmatch(r'[1-9]\d*', source_id)):
                raise ValueError(f'Uncertified point source at Present {frame}, slot {slot}')
            position, span = slot_info['schedule_position'], slot_info['schedule_range']
            if len(position) != 3 or len(span) != 2 or not all(map(math.isfinite, (*position, *span))) or span[1] <= 0:
                raise ValueError(f'Invalid cube projection at Present {frame}, slot {slot}')
            faces = []
            for side in range(6):
                name = f'point-{slot}-face-{side}.fgr'
                if files.get(name, {}).get('saved') is not True:
                    raise ValueError(f'Missing cube face at Present {frame}, slot {slot}, face {side}')
                data = read_bytes(folder / f'capture-{frame}-{name}')
                if len(data) != len(FACE_HEADER) + 256 * 256 * 4 or not data.startswith(FACE_HEADER):
                    raise ValueError(f'Invalid cube face format at Present {frame}, slot {slot}, face {side}')
                faces.append(hashlib.sha256(data).hexdigest())
            sources[source_id] = dict(slot=slot, origin=position, range=span, map=slot_info['map'],
                                      complete=slot_info['schedule_complete'],
                                      mesh_generation=slot_info['schedule_generation'], faces=faces)
        if not sources:
            raise ValueError(f'No ready point cubes at Present {frame}')
        products[str(frame)] = sources
    return products


def compare_cubes(reference, candidate):
    if reference.keys() != candidate.keys():
        raise ValueError('Cube captures must certify the same Present ordinals')
    frames = {}
    projection_keys = ('origin', 'range', 'map', 'mesh_generation')
    for frame, left in reference.items():
        right = candidate[frame]
        comparable = {}
        different_projection = []
        for source in sorted(left.keys() & right.keys()):
            before, after = left[source], right[source]
            if (not (before['complete'] and after['complete']) or
                    any(before[key] != after[key] for key in projection_keys)):
                different_projection.append(source)
                continue
            comparable[source] = [side for side, (a, b) in enumerate(zip(before['faces'], after['faces'], strict=True))
                                  if a != b]
        frames[frame] = dict(reference_only=sorted(left.keys() - right.keys()),
                             candidate_only=sorted(right.keys() - left.keys()),
                             incompatible_publications=different_projection, different_faces=comparable)
    return frames


def sampled_products(folder, size, frames):
    """Read the cubes bound at submitted point-correction draws, in draw order."""
    evidence, planes = {}, {}
    width, height = size
    plane_header = struct.pack('<4s4I', b'FGR1', width//2, height//2, 113, 8)
    for frame in frames:
        prefix = folder / f'capture-{frame}-'

        def load(name):
            return read_bytes(prefix.with_name(prefix.name + name))

        def journal(name, scope):
            raw = load(name + '.jsonl')
            rows = list(map(json.loads, raw.splitlines()))
            if (len(rows) < 3 or not all(isinstance(row, dict) for row in rows) or
                    rows[0].get('scope') != scope or rows[0].get('capture') != frame or
                    rows[0].get('renderer_frame') != frame or (rows[0].get('width'), rows[0].get('height')) != size or
                    rows[-1].get('event') != 'end' or rows[-1].get('render_completed') is not True or
                    rows[-1].get('export_complete') is not True):
                raise ValueError(f'Incomplete {scope} at Present {frame}')
            return rows, hashlib.sha256(raw).hexdigest()

        rows, point_hash = journal('point-light-inputs', 'local-light-contribution-inputs')
        roster_rows, roster_hash = journal('frame-parity-lights', 'local-light-contribution-inputs')
        roster = [row for row in roster_rows if row['event'] == 'source']
        if ([row['slot'] for row in roster] != list(range(len(roster))) or
                [row['count'] for row in roster_rows if row['event'] == 'selection'] != [len(roster)]):
            raise ValueError(f'Invalid local source order at Present {frame}')
        source_ids = {row['source_id']: row for row in roster}
        stages = [row.get('active') for row in rows if row['event'] == 'point-input-stage']
        files = {row['name']: row for row in rows if row['event'] == 'file'}
        issues = [row for row in rows if row['event'] == 'point-correction']
        slots = [row.get('slot') for row in issues]
        if (len(stages) != 1 or type(stages[0]) is not bool or len(source_ids) != len(roster) or
                any(type(slot) is not int or not 0 <= slot < 128 for slot in slots) or
                slots != sorted(set(slots)) or (issues and not stages[0])):
            raise ValueError(f'Invalid point correction order or stage at Present {frame}')

        def product(name, header, size):
            data = load(name)
            if (files.get(name, {}).get('saved') is not True or files[name].get('bytes') != size or
                    len(data) != size or not data.startswith(header)):
                raise ValueError(f'Invalid {name} at Present {frame}')
            return data

        stage_planes = {name: product(f'local-{name}.fgr', plane_header, 20+8*(width//2)*(height//2))
                        for name in ('before-point', 'after-point')} if stages[0] else {}
        draws, order = {}, []
        for row in issues:
            slot, source_id = row['slot'], row.get('source_id')
            if (source_id not in source_ids or source_id != row.get('cube_source_id') or
                    any(type(row.get(key)) is not bool for key in ('complete', 'budget_held', 'soft')) or
                    type(row.get('age_ms')) is not int or not 0 <= row['age_ms'] <= 0xffffffff or
                    len(row.get('scissor', ())) != 4 or any(type(x) is not int for x in row['scissor'])):
                raise ValueError(f'Unproved cube binding at Present {frame}, slot {slot}')
            source = {key: val for key, val in source_ids[source_id].items() if key != 'source_id'}
            ordinal = str(source['slot'])
            if ordinal in draws:
                raise ValueError(f'Repeated point correction at Present {frame}, source {ordinal}')
            order.append(ordinal)
            bank = product(f'sampled-point-{slot}-bank.f32', b'', 14*16)
            faces = [product(f'sampled-point-{slot}-face-{side}.fgr', FACE_HEADER, 20+256*256*4) for side in range(6)]
            draws[ordinal] = {'source': source, 'slot': slot, 'bank': hashlib.sha256(bank).hexdigest(),
                              'faces': [hashlib.sha256(data).hexdigest() for data in faces],
                              **{key: row[key] for key in ('complete', 'budget_held', 'age_ms', 'soft', 'scissor')}}
        evidence[str(frame)] = {'point_journal': point_hash, 'local_roster': roster_hash, 'draws': draws,
                                'order': order, 'planes': {k: hashlib.sha256(v).hexdigest() for k, v in stage_planes.items()}}
        planes[str(frame)] = stage_planes
    return evidence, planes


def compare_sampled(folders, size, frames, saved):
    (live, live_planes), (stand, stand_planes) = (sampled_products(folder, size, frames) for folder in folders)
    if (live, stand) != tuple(saved):
        raise ValueError('Point correction evidence changed after validation')
    result = {}
    for frame, left in live.items():
        right = stand[frame]
        a, b = (r['draws'] for r in (left, right))
        matches = {}
        for key in a.keys() & b.keys():
            x, y = a[key], b[key]
            matches[key] = {'source_match': x['source'] == y['source'], 'bank_match': x['bank'] == y['bank'],
                            'state_differences': {name: [x[name], y[name]] for name in ('soft', 'scissor', 'complete', 'budget_held')
                                                  if x[name] != y[name]},
                            'different_faces': [i for i, (u, v) in enumerate(zip(x['faces'], y['faces'], strict=True)) if u != v]}
        p, q = live_planes[frame], stand_planes[frame]
        result[frame] = {'live_only': sorted(a.keys() - b.keys()), 'replay_only': sorted(b.keys() - a.keys()),
                         'order_matches': left['order'] == right['order'], 'comparable_sources': matches,
                         'planes': {name: pixel_difference(memoryview(p[name])[20:].cast('Q'), memoryview(q[name])[20:].cast('Q'), size[0]//2)
                                    for name in p.keys() & q.keys()}}
    return result
