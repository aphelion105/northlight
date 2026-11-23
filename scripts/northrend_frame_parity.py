"""Verify resolved frame planes and lighting inputs at certified Presents."""
from array import array
import hashlib
from heapq import merge
from itertools import pairwise
import json
import math
import re
import struct
import sys

from capture_files import read_bytes


FRAME_COUNT = 2
PLANES = {'color': (113, 8, 'Q'), 'depth': (114, 4, 'I')}
COMPOSITION_PRODUCTS = {
    'material-height': PLANES['color'],
    'material-occlusion': (21, 4, 'I'),
    'hdr-linear': PLANES['color'],
    'hdr-exposure': PLANES['color'],
}
HISTORY_INPUTS = {
    'history-raw-light': PLANES['color'],
    'history-prior-light': PLANES['color'],
    'history-prior-distance': PLANES['depth'],
    'history-directional-visibility': PLANES['color'],
}
PRE_GI_LIGHT = 'history-pre-gi-light'
RESOLVED_LIGHT = 'history-resolved-light'
GI_PROBE_SLOTS = (4, 5, 6, 7, 8, 10, 13)
GI_FORMAT_STRIDE = {21: 4, 22: 4, 113: 8, 114: 4, 115: 8, 116: 16}
DRAW_WITNESS = ('draw-probe.json', 'draw-probe-before.bgra', 'draw-probe-after.bgra')


def draw_witness(root, size, msaa):
    journal, before, after = DRAW_WITNESS
    metadata = read_bytes(root / journal)
    if len(metadata) > 4096:
        raise ValueError('Oversized native draw witness metadata')
    row = json.loads(metadata)
    if (not isinstance(row, dict) or type(row.get('call')) is not int or not 0 < row['call'] < (1 << 32) - 1 or
            row.get('size') != list(size) or row.get('format') not in (21, 22) or
            row.get('samples') != msaa or any(row.get(stage) != '00000000' for stage in ('before', 'draw', 'after'))):
        raise ValueError(f'Native draw witness did not record two successful reads: {row}')
    rect = row.get('rect')
    if not isinstance(rect, list) or len(rect) != 4 or any(type(value) is not int for value in rect):
        raise ValueError('Invalid native draw witness rectangle')
    left, top, right, bottom = rect
    if not (0 <= left < right <= size[0] and 0 <= top < bottom <= size[1] and
            right - left <= 768 and bottom - top <= 1024):
        raise ValueError('Native draw witness rectangle exceeds the captured target')
    images = [read_bytes(root / name) for name in (before, after)]
    if any(len(image) != (right - left) * (bottom - top) * 4 for image in images):
        raise ValueError('Native draw witness image does not cover its rectangle')
    row['artifacts_sha256'] = {name: hashlib.sha256(data).hexdigest()
                                for name, data in zip(DRAW_WITNESS, [metadata, *images])}
    return row, images


def finite_vector(values, count):
    return (isinstance(values, list) and len(values) == count and
            all(type(value) in (int, float) and math.isfinite(value) for value in values))


def selected_frames(lines, discarded=None):
    frames = {}
    latest = -1
    for line in lines:
        if line.startswith('FRAME_PARITY_DISCARD '):
            if len(frames) != 1 or line != f'FRAME_PARITY_DISCARD first={latest}':
                raise ValueError('Parity discard must identify the sole saved Present')
            if discarded is not None:
                discarded.add(latest)
            frames.clear()
        elif line.startswith('FRAME_PARITY '):
            match = re.fullmatch(r'FRAME_PARITY present=(\d+) saved=1 depth_samples=2 depth_quality=(\d+)', line)
            if not match:
                raise ValueError('A selected Present did not export both GPU planes at 2× MSAA')
            frame, quality = map(int, match.groups())
            if frame <= latest or len(frames) == FRAME_COUNT:
                raise ValueError('Duplicate, out-of-order or excess parity Presents')
            frames[frame] = quality
            latest = frame
    return frames


def stream_without_skips(lines):
    rows = [line for line in lines if line.startswith('CSTREAM active ')]
    return len(rows) == 1 and ' frameSkip=0 ' in rows[0]


def frame_journal(folder, frame, size, *, atmosphere=False):
    name, scope = (('frame-parity-atmosphere', 'atmosphere-pass-bindings') if atmosphere else
                   ('frame-parity', 'resolved-presented-frame'))
    prefix = folder / f'capture-{frame}-{name}.jsonl'
    rows = [json.loads(line) for line in prefix.read_text(encoding='utf-8').splitlines()]
    width, height = size
    if (not rows or rows[0].get('event') != 'begin' or rows[0].get('renderer_frame') != frame or
            rows[0].get('capture') != frame or rows[0].get('width') != width or rows[0].get('height') != height or
            rows[0].get('scope') != scope or
            rows[-1].get('event') != 'end' or rows[-1].get('export_complete') is not True):
        raise ValueError(f'Present {frame} has no complete matching GPU journal')
    return rows


def planes(folder, frame, size, quality, *, journal=None):
    rows = frame_journal(folder, frame, size) if journal is None else journal
    width, height = size
    color = [row for row in rows if row.get('event') == 'image' and row.get('name') == 'color']
    resolves = [row for row in rows if row.get('event') == 'color-resolve' and row.get('name', 'color') == 'color']
    if (len(color) != 2 or [(row.get('samples'), row.get('sample_quality')) for row in color] != [(2, quality), (0, 0)] or
            len(resolves) != 1 or resolves[0].get('source_samples') != 2 or
            resolves[0].get('method') != 'StretchRect-NONE'):
        raise ValueError(f'Present {frame} has no matching 2× MSAA color resolve')
    depth = [row for row in rows if row.get('event') == 'depth-resolve']
    if depth != [dict(event='depth-resolve', source_format=75, source_samples=2,
                      sample_quality=quality, method='RESZ-INTZ')]:
        raise ValueError(f'Present {frame} has no matching 2× MSAA scene depth resolve')
    for name, (fmt, stride, words) in PLANES.items():
        data = read_bytes(folder / f'capture-{frame}-{name}.fgr')
        if (len(data) != 20 + width * height * stride or
                data[:20] != struct.pack('<4s4I', b'FGR1', width, height, fmt, stride)):
            raise ValueError(f'Invalid {name} image at Present {frame}')
        values = array(words)
        values.frombytes(memoryview(data)[20:])
        if sys.byteorder != 'little':
            values.byteswap()
        low, high = min(values), max(values)
        if low == high or (name == 'depth' and high > 0x3f800000):
            raise ValueError(f'Empty or invalid {name} image at Present {frame}')
        yield name, (hashlib.sha256(data).hexdigest(), values)


def native_color_plane(folder, frame, size, quality, *, journal=None):
    """The optional legacy color target, with its actual resolve and file receipt."""
    rows = frame_journal(folder, frame, size) if journal is None else journal
    return _color_plane(folder, frame, size, quality, rows, 'native-color')


def scene_input_color_plane(folder, frame, size):
    """The resolved copy consumed by the main screen-effects pass."""
    if not (folder / f'capture-{frame}-scene-input-color.fgr').is_file():
        return None, None
    rows = frame_journal(folder, frame, size, atmosphere=True)
    return _color_plane(folder, frame, size, None, rows, 'scene-input-color')


def _color_plane(folder, frame, size, quality, rows, name):
    filename = f'{name}.fgr'
    path = folder / f'capture-{frame}-{filename}'
    if not path.exists():
        return None, None
    images = [r for r in rows if r.get('event') == 'image' and r.get('name') == name]
    receipts = [r for r in rows if r.get('event') == 'file' and r.get('name') == filename]
    resolves = [r for r in rows if r.get('event') == 'color-resolve' and r.get('name') == name]
    sampling = [(r.get('samples'), r.get('sample_quality')) for r in images]
    resolved = (quality is not None and sampling == [(2, quality), (0, 0)] and len(resolves) == 1 and
                resolves[0].get('source_samples') == 2 and resolves[0].get('method') == 'StretchRect-NONE' and
                resolves[0].get('per_sample_image') is False)
    if not resolved and (sampling != [(0, 0)] or resolves):
        raise ValueError(f'Invalid {name} resolve at Present {frame}')
    width, height = size
    fmt = images[0].get('format')
    stride = {21: 4, 22: 4, 113: 8}.get(fmt)
    if (not stride or any((r.get('width'), r.get('height'), r.get('format')) != (width, height, fmt) for r in images)):
        raise ValueError(f'Invalid {name} image layout at Present {frame}')
    length = 20 + width * height * stride
    if len(receipts) != 1 or receipts[0].get('saved') is not True or receipts[0].get('bytes') != length:
        raise ValueError(f'Invalid {name} file receipt at Present {frame}')
    data = read_bytes(path)
    if len(data) != length or data[:20] != struct.pack('<4s4I', b'FGR1', width, height, fmt, stride):
        raise ValueError(f'Invalid {name} pixels at Present {frame}')
    words = array('I' if stride == 4 else 'Q')
    words.frombytes(memoryview(data)[20:])
    if sys.byteorder != 'little':
        words.byteswap()
    return dict(sha256=hashlib.sha256(data).hexdigest(), format=fmt, source_samples=sampling[0][0]), words


def normal_hashes(folder, size, frames):
    width, height = size
    products = {'model-normals': (width, height), 'normals': (width // 2, height // 2)}
    hashes = {}
    for frame in frames:
        hashes[str(frame)] = {}
        for name, (w, h) in products.items():
            data = read_bytes(folder / f'capture-{frame}-{name}.fgr')
            if (len(data) != 20 + w*h*8 or data[:20] != struct.pack('<4s4I', b'FGR1', w, h, 113, 8) or
                    data.count(b'\x00', 20) == w*h*8):
                raise ValueError(f'Missing normal coverage or invalid {name} image at Present {frame}')
            hashes[str(frame)][name] = hashlib.sha256(data).hexdigest()
    return hashes


def world_normal_plane(folder, frame, size):
    width, height = (dimension // 2 for dimension in size)
    journal = read_bytes(folder / f'capture-{frame}-products.jsonl')
    rows = [json.loads(line) for line in journal.splitlines()]
    products = [row for row in rows if row.get('product') == 'normals']
    if (len(products) != 1 or products[0].get('renderer_frame') != frame or
            products[0].get('state') != 'current' or products[0].get('saved') is not True or
            products[0].get('zero_filled') is not False or products[0].get('fps_eligible') is not False):
        raise ValueError(f'Present {frame} has no current world-normal export')
    data = read_bytes(folder / f'capture-{frame}-normals.fgr')
    length = width * height * 8
    if (len(data) != 20 + length or
            data[:20] != struct.pack('<4s4I', b'FGR1', width, height, 113, 8) or
            data.count(b'\x00', 20) == length):
        raise ValueError(f'Invalid world-normal image at Present {frame}')
    return hashlib.sha256(data).hexdigest(), data


def compare_world_normals(live, stand, size, frames, frozen):
    if not frames or frozen.keys() != {str(frame) for frame in frames}:
        raise ValueError('World-normal hashes must cover the selected Presents')
    results = {}
    for frame in frames:
        live_hash, live_data = world_normal_plane(live, frame, size)
        if live_hash != frozen[str(frame)]:
            raise ValueError(f'Live world-normal image changed after capture at Present {frame}')
        stand_hash, stand_data = world_normal_plane(stand, frame, size)
        left, right = (memoryview(data)[20:].cast('Q') for data in (live_data, stand_data))
        results[str(frame)] = {'live_sha256': live_hash, 'stand_sha256': stand_hash,
                               **pixel_difference(left, right, size[0] // 2)}
    return {'match': all(not row['changed_pixels'] for row in results.values()), 'frames': results}


def history_images(folder, size, frame):
    width, height = (dimension // 2 for dimension in size)
    journal = folder / f'capture-{frame}-products.jsonl'
    rows = [json.loads(line) for line in journal.read_text(encoding='utf-8').splitlines()]
    inputs = {}
    for row in rows:
        name = row.get('product', '')
        if not name.startswith('history-'):
            continue
        if ((name not in HISTORY_INPUTS and name not in (PRE_GI_LIGHT, RESOLVED_LIGHT)) or name in inputs or row.get('renderer_frame') != frame or
                row.get('saved') is not True or row.get('fps_eligible') is not False or
                (row.get('state'), row.get('zero_filled')) not in (('current', False), ('skipped', True))):
            raise ValueError(f'Invalid light history journal at Present {frame}: {name}')
        inputs[name] = row['state']
    if (not HISTORY_INPUTS.keys() <= inputs.keys() or inputs['history-raw-light'] != 'current' or
            inputs['history-prior-light'] != inputs['history-prior-distance']):
        raise ValueError(f'Incomplete light history at Present {frame}')
    formats = {**HISTORY_INPUTS, PRE_GI_LIGHT: PLANES['color'], RESOLVED_LIGHT: PLANES['color']}
    for name, (fmt, stride, code) in formats.items():
        if name not in inputs:
            continue
        data = read_bytes(folder / f'capture-{frame}-{name}.fgr')
        if (len(data) != 20 + width * height * stride or
                data[:20] != struct.pack('<4s4I', b'FGR1', width, height, fmt, stride)):
            raise ValueError(f'Invalid {name} image at Present {frame}')
        if inputs[name] == 'skipped':
            if data.count(b'\x00', 20) != width * height * stride:
                raise ValueError(f'Skipped {name} contains pixels at Present {frame}')
            yield name, (None, None)
        else:
            values = array(code)
            values.frombytes(memoryview(data)[20:])
            yield name, (hashlib.sha256(data).hexdigest(), values)


def history_hashes(folder, size, frames):
    return {str(frame): {name: sha for name, (sha, _) in history_images(folder, size, frame)}
            for frame in frames}


def compare_history(folders, size, frames, saved):
    results, overlap = {}, {}
    raw = 'history-raw-light'
    peers = (PRE_GI_LIGHT, 'history-directional-visibility', 'history-prior-light')
    for frame in frames:
        row = results[str(frame)] = {}
        offsets = {}
        if saved[0][str(frame)].keys() != saved[1][str(frame)].keys():
            raise ValueError(f'Compare the same light history inputs at Present {frame}')
        left, right = (history_images(folder, size, frame) for folder in folders)
        for (name, (left_sha, a)), (_, (right_sha, b)) in zip(left, right, strict=True):
            if (left_sha, right_sha) != tuple(item[str(frame)][name] for item in saved):
                raise ValueError(f'Light history input changed after validation: Present {frame} {name}')
            if a is None or b is None:
                row[name] = {'state': 'skipped' if a is b else 'different-use'}
            else:
                changed = array('I') if name == raw or name in peers else None
                if changed is not None:
                    offsets[name] = changed
                row[name] = {'state': 'current', **pixel_difference(a, b, size[0] // 2, changed)}
        overlap[str(frame)] = {
            name: sum(a == b for a, b in pairwise(merge(offsets[raw], offsets[name])))
            if offsets[raw] and offsets[name] else 0
            for name in peers if name in offsets
        }
    return results, overlap


def gi_products(folder, size, frame):
    def invalid(detail):
        return ValueError(f'Invalid GI capture at Present {frame}: {detail}')

    prefix = folder / f'capture-{frame}-'
    journal = read_bytes(prefix.with_name(prefix.name + 'frame-parity-gi-inputs.jsonl'))
    rows = [json.loads(line) for line in journal.splitlines()]
    if not 3 <= len(rows) <= 128 or not all(isinstance(row, dict) for row in rows):
        raise invalid('incomplete journal')
    begin, stage, *events, end = rows
    mode, resident = stage.get('mode'), stage.get('resident')
    if (begin.get('event') != 'begin' or begin.get('scope') != 'gi-pass-inputs' or
            begin.get('capture') != frame or begin.get('renderer_frame') != frame or
            (begin.get('width'), begin.get('height')) != size or
            end.get('event') != 'end' or end.get('render_completed') is not True or
            end.get('export_complete') is not True or stage.get('event') != 'gi-pass' or
            mode not in ('disabled', 'separate', 'fused') or type(resident) is not bool or
            (mode != 'separate' and resident) or
            any(type(stage.get(key)) is not int or stage[key] < 0 for key in ('world_serial', 'atlas_serial'))):
        raise invalid('invalid pass or frame identity')
    slots = set() if mode == 'disabled' else {11}
    if mode == 'separate':
        slots.update(GI_PROBE_SLOTS)
    bindings = {}
    files = {} if mode == 'disabled' else {'gi-constants.f32': 'constants', 'lighting-shader-0.bin': 'shader'}
    for row in events:
        if row.get('event') != 'input-binding':
            continue
        slot, is_null = row.get('slot'), row.get('null')
        if type(slot) is not int or slot not in slots or slot in bindings or type(is_null) is not bool:
            raise invalid('unexpected or duplicate sampler')
        base = f'lighting-input-g5-s{slot}'
        if row.get('group') != 5 or row.get('snapshot_group') != 5 or row.get('file_base') != ('' if is_null else base):
            raise invalid('ambiguous sampler product')
        bindings[slot] = None if is_null else base
        if not is_null:
            files[f'{base}.fgr'] = f's{slot}'
    if bindings.keys() != slots or (resident and any(bindings[slot] is None for slot in (4, 5, 6, 7, 10))):
        raise invalid('missing active sampler')
    shader_bindings = [row for row in events if row.get('event') == 'shader-binding']
    expected_shader = [] if mode == 'disabled' else [{'event': 'shader-binding', 'pass': 0, 'shader': 0}]
    if shader_bindings != expected_shader:
        raise invalid('missing or ambiguous shader binding')
    saved = [row for row in events if row.get('event') == 'file']
    if len(saved) != len(files) or {row.get('name') for row in saved} != files.keys():
        raise invalid('missing or duplicate file')
    physical = {f's{slot}': None for slot in slots}
    for row in saved:
        name = row['name']
        data = read_bytes(prefix.with_name(prefix.name + name))
        if row.get('saved') is not True or row.get('bytes') != len(data):
            raise invalid(f'invalid file receipt for {name}')
        if name.endswith('.fgr'):
            if len(data) < 20:
                raise invalid(f'truncated image header for {name}')
            magic, width, height, fmt, stride = struct.unpack_from('<4s4I', data)
            if (magic != b'FGR1' or not width or not height or
                    stride != GI_FORMAT_STRIDE.get(fmt) or len(data) != 20 + width * height * stride):
                raise invalid(f'invalid image layout for {name}')
        elif name == 'gi-constants.f32' and len(data) != 116 * 16:
            raise invalid('invalid constant bank size')
        elif name == 'lighting-shader-0.bin' and (len(data) < 8 or len(data) % 4 or data[:4] != b'\x00\x03\xff\xff'):
            raise invalid('invalid pixel shader')
        physical[files[name]] = data
    evidence = {key: stage[key] for key in ('mode', 'resident', 'atlas_serial', 'world_serial')}
    solver = [row for row in events if row.get('event') == 'probe-radiance']
    if solver:
        vectors = ('sun_direction', 'sun_irradiance', 'sky_radiance')
        integers = ('request', 'lighting_generation')
        light = solver[0]
        if (len(solver) != 1 or not resident or
                any(type(light.get(key)) is not int or light[key] < 1 for key in integers) or
                not all(finite_vector(light.get(key), 3) for key in vectors)):
            raise invalid('invalid probe solver radiance')
        evidence['probe_radiance'] = {key: light[key] for key in (*integers, *vectors)}
    evidence['journal_sha256'] = hashlib.sha256(journal).hexdigest()
    evidence['files'] = {key: hashlib.sha256(data).hexdigest() if data is not None else None
                         for key, data in physical.items()}
    return evidence, physical


def gi_inputs(folder, size, frames):
    return {str(frame): gi_products(folder, size, frame)[0] for frame in frames}


def compare_gi(folders, size, frames, saved):
    result = {}
    for frame in frames:
        left, right = (gi_products(folder, size, frame) for folder in folders)
        for observed, evidence in zip((left, right), saved, strict=True):
            if observed[0] != evidence[str(frame)]:
                raise ValueError(f'GI evidence changed after validation at Present {frame}')
        metadata = {key: [left[0][key], right[0][key]] for key in ('mode', 'resident', 'atlas_serial', 'world_serial')
                    if left[0][key] != right[0][key]}
        images = {}
        for key in sorted(left[1].keys() | right[1].keys()):
            a, b = left[1].get(key), right[1].get(key)
            row = images[key] = {'match': a == b}
            if a is None or b is None:
                row['bound'] = [a is not None, b is not None]
            elif key == 'constants':
                row['changed_registers'] = [i for i in range(116) if a[i*16:(i+1)*16] != b[i*16:(i+1)*16]]
            elif key.startswith('s') and key != 'shader':
                row['same_layout'] = a[:20] == b[:20]
                if row['same_layout']:
                    _, width, _, _, stride = struct.unpack_from('<4s4I', a)
                    if row['match']:
                        row['changed_pixels'] = 0
                    else:
                        words = '<' + 'I'*(stride//4)
                        row.update(pixel_difference(struct.iter_unpack(words, memoryview(a)[20:]),
                                                    struct.iter_unpack(words, memoryview(b)[20:]), width))
        result[str(frame)] = {'pass_differences': metadata, 'inputs': images}
        a, b = (row[0].get('probe_radiance') for row in (left, right))
        if a is not None or b is not None:
            result[str(frame)]['probe_radiance_differences'] = (
                [a, b] if a is None or b is None else
                {key: [value, b[key]] for key, value in a.items() if value != b[key]})
    return result


def directional_inputs(folder, size, frames):
    evidence = {}
    for frame in frames:
        data = read_bytes(folder / f'capture-{frame}-frame-parity-directional.jsonl')
        rows = [json.loads(line) for line in data.splitlines()]
        if not 3 <= len(rows) <= 7 or not all(isinstance(row, dict) for row in rows):
            raise ValueError(f'Missing directional inputs at Present {frame}')
        begin, anchor, *maps, end = rows
        mask, shadow_pass = anchor.get('source_mask'), anchor.get('shadow_pass')
        if (begin.get('event') != 'begin' or begin.get('capture') != frame or
                begin.get('renderer_frame') != frame or begin.get('scope') != 'directional-shadow-projections' or
                (begin.get('width'), begin.get('height')) != size or
                end.get('event') != 'end' or end.get('export_complete') is not True or
                end.get('render_completed') is not True or anchor.get('event') != 'anchor' or
                type(mask) is not int or not 0 <= mask <= 3 or type(shadow_pass) is not int or shadow_pass < 1 or
                not all(finite_vector(anchor.get(name), 3) for name in ('pivot', 'cascade_anchor', 'far_anchor')) or
                not finite_vector([anchor.get('far_radius')], 1) or anchor['far_radius'] <= 0):
            raise ValueError(f'Invalid directional capture at Present {frame}')
        slots = [slot for slot in range(4) if mask & (1 << (slot // 2))]
        if [row.get('slot') for row in maps] != slots:
            raise ValueError(f'Incomplete or unordered directional projections at Present {frame}')
        for row in maps:
            committed = row.get('committed_pass')
            if (row.get('event') != 'projection' or row.get('action') not in ('R', 'D', 'U') or
                    type(committed) is not int or not 0 <= committed <= shadow_pass or
                    (row['action'] != 'R' and not committed) or
                    any(type(row.get(key)) is not int or row[key] < 0 for key in
                        ('static_cache_generation', 'static_cache_signature')) or
                    not all(finite_vector(row.get(key), 16) for key in ('planned_matrix', 'receiver_matrix')) or
                    ('cache_matrix' not in row or
                     row['cache_matrix'] is not None and not finite_vector(row['cache_matrix'], 16)) or
                    not finite_vector(row.get('planned_cache_offset'), 3)):
                raise ValueError(f'Invalid slot {row["slot"]} directional projection at Present {frame}')
        anchor_keys = ('pivot', 'cascade_anchor', 'far_anchor', 'far_radius', 'source_mask', 'shadow_pass')
        map_keys = ('slot', 'action', 'committed_pass', 'static_cache_generation', 'static_cache_signature',
                    'planned_cache_offset', 'planned_matrix', 'receiver_matrix', 'cache_matrix')
        evidence[str(frame)] = {'sha256': hashlib.sha256(data).hexdigest(),
                                'anchor': {key: anchor[key] for key in anchor_keys},
                                'maps': [{key: row[key] for key in map_keys} for row in maps]}
    return evidence


def compare_directional(folders, size, frames, saved):
    observed = [directional_inputs(folder, size, frames) for folder in folders]
    if any(report != previous for report, previous in zip(observed, saved, strict=True)):
        raise ValueError('Directional projection evidence changed after validation')
    reference, candidate = observed
    result = {}
    matrix_keys = ('planned_matrix', 'receiver_matrix', 'cache_matrix')
    for key in reference:
        left, right = reference[key], candidate[key]
        anchors = {name: [value, right['anchor'][name]] for name, value in left['anchor'].items()
                   if value != right['anchor'][name]}
        a, b = ({row['slot']: row for row in run['maps']} for run in (left, right))
        maps = {}
        for slot in sorted(a.keys() & b.keys()):
            differences = {name: [value, b[slot][name]] for name, value in a[slot].items()
                           if name not in matrix_keys and value != b[slot][name]}
            for name in matrix_keys:
                x, y = a[slot][name], b[slot][name]
                if x != y:
                    differences[name] = ([x, y] if x is None or y is None else
                                         {str(i): [u, v] for i, (u, v) in enumerate(zip(x, y, strict=True)) if u != v})
            maps[str(slot)] = differences
        result[key] = {'anchor_differences': anchors, 'map_differences': maps,
                       'reference_only': sorted(a.keys() - b.keys()), 'candidate_only': sorted(b.keys() - a.keys())}
    return result


def lighting_evidence(folder, size, frames):
    history = history_hashes(folder, size, frames)
    if any(PRE_GI_LIGHT not in row for row in history.values()):
        raise ValueError('Missing light product before GI on a selected Present')
    return {'light_history_inputs': history, 'directional_inputs': directional_inputs(folder, size, frames),
            'gi_inputs': gi_inputs(folder, size, frames)}


def compare_live_lighting(folders, size, frames, live, replay):
    frame_keys = tuple(map(str, frames))

    def evidence(key):
        # A stand may also have audited leading frames. The audited live
        # capture proves exactly the Presents shared by the image comparison.
        source, stand = live.get(key), replay.get(key)
        if (not isinstance(source, dict) or not isinstance(stand, dict) or
                source.keys() != set(frame_keys) or any(frame not in stand for frame in frame_keys)):
            raise ValueError(f'Light comparison is missing certified {key}')
        return source, {frame: stand[frame] for frame in frame_keys}

    history, overlap = compare_history(folders, size, frames, evidence('light_history_inputs'))
    return {'history': history, 'history_overlap_with_raw': overlap,
            'directional': compare_directional(folders, size, frames, evidence('directional_inputs')),
            'gi': compare_gi(folders, size, frames, evidence('gi_inputs'))}


LOCAL_LIGHT_INPUT_FIELDS = ('worldFrame', 'ready', 'map', 'clock', 'dt', 'continuous', 'trigger', 'draw',
                            'view', 'authored', 'native', 'nativeCount', 'nativeTime', 'banks', 'bankCount',
                            'drawLights', 'decoded', 'equipment', 'equipmentCount', 'particles', 'immediate',
                            'selected', 'selectedCount')


def local_light_input_frames(lines, last_present):
    result = {}
    for line in lines:
        if not line.startswith('LOCAL_INPUTS '):
            continue
        data = re.split(r' (\w+)=', line)
        row = dict(zip(data[1::2], data[2::2], strict=True))
        if not row.get('rendererFrame', '').isdecimal() or any(field not in row for field in LOCAL_LIGHT_INPUT_FIELDS):
            raise ValueError('Malformed local-light input audit')
        frame = int(row['rendererFrame'])
        if frame <= last_present:
            result.setdefault(frame, []).append(row)
    return result


def observed_world_scene_start(lines, geometry_start, epoch_first, require_active=True):
    rows = local_light_input_frames(lines, geometry_start)
    if not rows:
        return None  # Older diagnostic DLLs did not emit this audit.
    present = min((frame for frame, attempts in rows.items() if frame >= epoch_first and any(
        row['map'] != '-' and not row['map'].startswith('Menu:') for row in attempts)), default=None)
    if present is None and not require_active:
        return None
    if present is None or not 0 < present <= geometry_start:
        raise ValueError('Local-light input audit did not record the first active World scene')
    return present


def compare_local_light_inputs(live_lines, stand_lines, presents):
    """Locate the first divergent local-light input, including warm-up."""
    live, stand = (local_light_input_frames(lines, max(presents)) for lines in (live_lines, stand_lines))
    if not live or not stand:
        return None  # Older diagnostic DLLs did not emit this audit.
    shared = sorted(live.keys() & stand.keys())
    differences = {}
    for frame in shared:
        for field in LOCAL_LIGHT_INPUT_FIELDS:
            left = [row[field] for row in live[frame]]
            right = [row[field] for row in stand[frame]]
            if left == right:
                continue
            entry = differences.setdefault(field, dict(frames=0, first_present=frame, live=left, stand=right))
            entry['frames'] += 1
    return dict(shared_presents=len(shared), live_only=sorted(live.keys() - stand.keys()),
                stand_only=sorted(stand.keys() - live.keys()),
                certified_presents_covered=all(frame in live and frame in stand for frame in presents),
                differences=differences)


def static_shadow_stream(lines, presents):
    rows = {}
    for line in lines:
        if line.startswith('STATIC_PARITY_FAILURE '):
            raise ValueError(f'Static shadow streaming failed: {line}')
        if not line.startswith('STATIC_PARITY '):
            continue
        match = re.fullmatch(r'STATIC_PARITY rendererFrame=(\d+) upload=([01]) placements=(\d+) cpuModels=(\d+) '
                             r'ready=(\d+) pending=(\d+) bytes=(\d+) content=([0-9a-f]{16})', line)
        if not match:
            raise ValueError('Malformed static shadow streaming evidence')
        frame, upload, placements, cpu, ready, pending, uploaded = map(int, match.groups()[:-1])
        if frame in presents:
            rows.setdefault(str(frame), []).append(dict(upload=upload, placements=placements, cpu_models=cpu,
                                                        ready=ready, pending=pending, bytes=uploaded, content=match[8]))
    if any(not any(row['upload'] and row['ready'] for row in rows.get(str(frame), ())) for frame in presents):
        raise ValueError('A selected Present lacks the audited static shadow upload and resident casters')
    return rows


def shadow_bounds_work(lines, presents=None):
    rows = {}
    for line in lines:
        if not line.startswith('SHADOW_BOUNDS_PARITY '):
            continue
        match = re.fullmatch(r'SHADOW_BOUNDS_PARITY rendererFrame=(\d+) packets=(\d+) valid=(\d+) operations=(\d+) '
                             r'vertices=(\d+) cascades=(\d+) actors=(\d+) content=([0-9a-f]{16})', line)
        if not match:
            raise ValueError('Malformed shadow bounds evidence')
        frame, packets, valid, operations, vertices, cascades, actors = map(int, match.groups()[:-1])
        if presents is not None and frame not in presents:
            continue
        if str(frame) in rows or valid > packets or (presents is not None and (not valid or not operations)):
            raise ValueError(f'Invalid shadow bounds work at Present {frame}')
        rows[str(frame)] = dict(packets=packets, valid=valid, operations=operations, vertices=vertices,
                                cascades=cascades, actors=actors, content=match[8])
    if presents is not None and rows.keys() != set(map(str, presents)):
        raise ValueError('A selected Present lacks shadow bounds work evidence')
    return rows


def compare_present_work(live, stand):
    shared = sorted(live.keys() & stand.keys(), key=int)
    differing = [frame for frame in shared if live[frame] != stand[frame]]
    first = differing[0] if differing else None
    return {'shared_presents': len(shared), 'differing_presents': len(differing),
            'live_only': sorted(map(int, live.keys() - stand.keys())),
            'stand_only': sorted(map(int, stand.keys() - live.keys())),
            'first_difference': {'present': int(first), 'live': live[first], 'stand': stand[first]} if first else None}


GI_TIMELINE_FIELDS = {
    'GI_ACTOR_CAPTURE': ('actor', 'packets', 'vertices', 'menu'),
    'GI_ACTOR_REQUEST': ('actor', 'request', 'base', 'menu'),
    'GI_ATLAS_PRESENT': ('serial', 'request', 'actor', 'generation', 'processed',
                         'dynamicSolved', 'coarse', 'partial'),
}


def gi_timeline(lines, first, last):
    events = {}
    for line in lines:
        prefix, _, values = line.partition(' ')
        if prefix not in GI_TIMELINE_FIELDS:
            continue
        fields = ('present', *GI_TIMELINE_FIELDS[prefix])
        match = re.fullmatch(' '.join(f'{field}=(\\d+)' for field in fields), values)
        if not match:
            raise ValueError(f'Malformed {prefix} timeline evidence')
        present, *data = map(int, match.groups())
        if first <= present <= last:
            row = dict(zip(fields[1:], data, strict=True))
            events.setdefault(prefix, {}).setdefault(str(present), []).append(row)
    return events


def compare_gi_timeline(live_lines, stand_lines, first, last):
    live, stand = (gi_timeline(lines, first, last) for lines in (live_lines, stand_lines))
    if not live and not stand:
        return None  # Older DLLs did not publish these events.
    return {stage: compare_present_work(live.get(stage, {}), stand.get(stage, {}))
            for stage in GI_TIMELINE_FIELDS if stage in live or stage in stand}


def fixed_gi_publication(lines, last):
    if 'GI_PARITY_POLICY completed-near-and-coarse' not in lines:
        raise ValueError('The renderer did not enable fixed GI publication')
    if failure := next((line for line in lines if line.startswith('GI_PARITY_FAILURE ')), None):
        raise ValueError(f'Fixed GI publication failed: {failure}')
    atlas = gi_timeline(lines, 0, last).get('GI_ATLAS_PRESENT', {})
    rows = [row for group in atlas.values() for row in group]
    if not rows or any(not row['processed'] or row['partial'] for row in rows):
        raise ValueError('Fixed GI publication requires completed atlas evidence')
    return {'uploads': len(rows), 'last_upload_present': max(map(int, atlas))}


def compare_shadow_bounds_timeline(live_lines, stand_lines):
    return compare_present_work(shadow_bounds_work(live_lines), shadow_bounds_work(stand_lines))


def actor_shadow_work(lines):
    rows = {}
    for line in lines:
        if not line.startswith('SHADOW_ACTOR_PARITY '):
            continue
        match = re.fullmatch(r'SHADOW_ACTOR_PARITY rendererFrame=(\d+) source=([01]) matrix=([0-9a-f]{16}) '
                             r'packets=([0-9a-f]{16}) count=(\d+) held=(\d+) carried=(\d+)', line)
        if not match:
            raise ValueError('Malformed actor shadow routing evidence')
        frame, source, matrix, packets, count, held, carried = match.groups()
        sources = rows.setdefault(frame, {})
        count, held, carried = map(int, (count, held, carried))
        if source in sources or held + carried > count:
            raise ValueError(f'Invalid actor shadow routing at Present {frame}, source {source}')
        sources[source] = dict(matrix=matrix, packets=packets, count=count, held=held, carried=carried)
    return rows


def directional_depth_images(folder, frame, names=('shadow-near', 'shadow-far'), *, allow_skipped=False):
    journal = folder / f'capture-{frame}-products.jsonl'
    rows = [json.loads(line) for line in journal.read_text(encoding='utf-8').splitlines()]
    for name in names:
        evidence = [row for row in rows if row.get('product') == name]
        expected = dict(product=name, renderer_frame=frame, state='current', saved=True,
                        zero_filled=False, fps_eligible=False)
        skipped = allow_skipped and evidence == [{**expected, 'state': 'skipped', 'zero_filled': True}]
        if evidence != [expected] and not skipped:
            raise ValueError(f'No current {name} image at Present {frame}')
        data = read_bytes(folder / f'capture-{frame}-{name}.fgr')
        if len(data) != 20 + 1024**2 * 4 or data[:20] != struct.pack('<4s4I', b'FGR1', 1024, 1024, 114, 4):
            raise ValueError(f'Invalid {name} image at Present {frame}')
        if skipped:
            if data.count(b'\0', 20) != len(data) - 20:
                raise ValueError(f'Skipped {name} contains pixels at Present {frame}')
            yield name, (None, None)
            continue
        values = array('I')
        values.frombytes(memoryview(data)[20:])
        yield name, (hashlib.sha256(data).hexdigest(), values)


def directional_depth_hashes(folder, presents, names=('shadow-near', 'shadow-far'), *, allow_skipped=False):
    return {str(frame): {name: sha for name, (sha, _) in
                         directional_depth_images(folder, frame, names, allow_skipped=allow_skipped)}
            for frame in presents}


def compare_directional_depth(live, stand, presents, frozen, *, allow_skipped=False):
    report = {}
    for frame in presents:
        row = report[str(frame)] = {}
        for (name, (live_sha, a)), (_, (stand_sha, b)) in zip(
                directional_depth_images(live, frame, allow_skipped=allow_skipped),
                directional_depth_images(stand, frame, allow_skipped=allow_skipped), strict=True):
            if name not in frozen.get(str(frame), {}) or frozen[str(frame)][name] != live_sha:
                raise ValueError(f'Live {name} image changed after capture at Present {frame}')
            row[name] = {'live_sha256': live_sha, 'stand_sha256': stand_sha}
            if a is None or b is None:
                row[name]['state'] = 'skipped' if a is b else 'different-use'
            else:
                row[name].update(pixel_difference(a, b, 1024))
    return report


def pixel_difference(left, right, width, changed=None):
    count, first = 0, None
    if left != right:
        for offset, (a, b) in enumerate(zip(left, right, strict=True)):
            if a != b:
                count += 1
                if changed is not None:
                    changed.append(offset)
                if first is None:
                    first = [offset % width, offset // width]
    return {'changed_pixels': count, **({'first_changed_xy': first} if first is not None else {})}


def compare_native_color(left, right, planes_by_side, size):
    live, a = left
    stand, b = right
    result = {'live': live, 'stand': stand, 'state': 'unavailable'}
    if live is None or stand is None:
        return result
    if any(live[key] != stand[key] for key in ('format', 'source_samples')):
        result['state'] = 'incompatible'
        return result
    width, height = size
    offsets = array('I')
    storage = pixel_difference(a, b, width, offsets)
    xmin, ymin, xmax, ymax = width, height, -1, -1
    native_count = depth_overlap = color_overlap = 0
    compare_bits = 0xffffff if live['format'] == 22 else -1  # X8 is not a color channel.
    ld, rd = (side['depth'][1] for side in planes_by_side)
    lc, rc = (side['color'][1] for side in planes_by_side)
    for offset in offsets:
        if not ((a[offset] ^ b[offset]) & compare_bits):
            continue
        y, x = divmod(offset, width)
        xmin, ymin, xmax, ymax = min(xmin, x), min(ymin, y), max(xmax, x), max(ymax, y)
        native_count += 1
        depth_overlap += ld[offset] != rd[offset]
        color_overlap += lc[offset] != rc[offset]
    return {**result, 'state': 'compared', 'changed_pixels': native_count,
            'changed_storage_pixels': storage['changed_pixels'],
            'changed_bounds_xyxy': [xmin, ymin, xmax, ymax] if native_count else None,
            'overlap_with_depth': depth_overlap, 'overlap_with_presented_color': color_overlap}


def composition_products(folder, frame, size, journal):
    products_path = folder / f'capture-{frame}-products.jsonl'
    products = {}
    if products_path.is_file():
        for line in products_path.read_text(encoding='utf-8').splitlines():
            row = json.loads(line)
            name = row.get('product')
            if name not in ('material-height', 'material-occlusion'):
                continue
            state = row.get('state')
            if (name in products or row.get('renderer_frame') != frame or row.get('fps_eligible') is not False or
                    (state, row.get('saved'), row.get('zero_filled')) not in
                    (('current', True, False), ('skipped', True, True), ('unavailable', False, False),
                     ('skipped', False, True))):
                raise ValueError(f'Invalid {name} product receipt at Present {frame}')
            products[name] = (state, row['saved'])
    for name, (fmt, stride, code) in COMPOSITION_PRODUCTS.items():
        width, height = (1, 1) if name == 'hdr-exposure' else size
        length = 20 + width * height * stride
        if name.startswith('hdr-'):
            images = [row for row in journal if row.get('event') == 'image' and row.get('name') == name]
            files = [row for row in journal if row.get('event') == 'file' and row.get('name') == f'{name}.fgr']
            saved = bool(files and files[0].get('saved') is True)
            if images or files:
                if (len(images) != 1 or len(files) != 1 or type(files[0].get('saved')) is not bool or
                        (images[0].get('width'), images[0].get('height'), images[0].get('format'),
                         images[0].get('samples'), images[0].get('sample_quality')) != (width, height, fmt, 0, 0) or
                        (saved and files[0].get('bytes') != length)):
                    raise ValueError(f'Invalid {name} image receipt at Present {frame}')
            state = 'current' if saved else 'unavailable'
        else:
            state, saved = products.get(name, ('unavailable', False))
        evidence = {'state': state}
        pixels = None
        if saved:
            data = read_bytes(folder / f'capture-{frame}-{name}.fgr')
            if (len(data) != length or data[:20] != struct.pack('<4s4I', b'FGR1', width, height, fmt, stride) or
                    (state == 'skipped' and data.count(b'\x00', 20) != length - 20)):
                raise ValueError(f'Invalid {name} product at Present {frame}')
            evidence['sha256'] = hashlib.sha256(data).hexdigest()
            if state == 'current':
                pixels = array(code)
                pixels.frombytes(memoryview(data)[20:])
                if sys.byteorder != 'little':
                    pixels.byteswap()
        yield name, evidence, pixels


def composition_receipts(folder, frame, size, journal):
    return {name: receipt for name, receipt, _ in composition_products(folder, frame, size, journal)}


def hdr_boundary_images(folder, frame, size):
    names = ('hdr-after-world', 'hdr-before-finish')
    paths = [folder / f'capture-{frame}-{name}.fgr' for name in names]
    if not any(path.is_file() for path in paths):
        return {}
    if not all(path.is_file() for path in paths):
        raise ValueError(f'Incomplete HDR boundary images at Present {frame}')
    width, height = size
    header = struct.pack('<4s4I', b'FGR1', width, height, 113, 8)
    images = {}
    for name, path in zip(names, paths, strict=True):
        data = read_bytes(path)
        if len(data) != len(header) + width * height * 8 or not data.startswith(header):
            raise ValueError(f'Invalid {name} at Present {frame}')
        images[name] = (hashlib.sha256(data).hexdigest(), memoryview(data)[len(header):].cast('Q'))
    return images


def compare_hdr_boundaries(folders, frame, size, frozen):
    live, stand = (hdr_boundary_images(folder, frame, size) for folder in folders)
    if {name: sha for name, (sha, _) in live.items()} != frozen or stand.keys() != live.keys():
        raise ValueError(f'HDR boundary evidence differs from the live receipt at Present {frame}')
    return {name: {'live_sha256': sha, 'stand_sha256': stand[name][0],
                   **pixel_difference(values, stand[name][1], size[0])}
            for name, (sha, values) in live.items()}


def compare_composition(folders, journals, frame, size, color):
    products = [composition_products(folder, frame, size, journal) for folder, journal in zip(folders, journals, strict=True)]
    result = {}
    for (name, left_receipt, left), (_, right_receipt, right) in zip(*products, strict=True):
        state = left_receipt['state'] if left_receipt['state'] == right_receipt['state'] else 'different-use'
        row = result[name] = {'live': left_receipt, 'stand': right_receipt, 'state': state}
        if state != 'current':
            continue
        width = 1 if name == 'hdr-exposure' else size[0]
        changed = array('I')
        row.update(pixel_difference(left, right, width, changed))
        stride = COMPOSITION_PRODUCTS[name][1]

        def pixel(offset):
            return {'xy': [offset % width, offset // width],
                    'left_storage_hex': left[offset].to_bytes(stride, 'little').hex(),
                    'right_storage_hex': right[offset].to_bytes(stride, 'little').hex()}

        if changed:
            row['first_changed_pixel'] = pixel(changed[0])
        if name != 'hdr-exposure':
            overlap = (offset for offset in changed if color[0][offset] != color[1][offset])
            first = next(overlap, None)
            row['overlap_with_presented_color'] = 0 if first is None else 1 + sum(1 for _ in overlap)
            if first is not None:
                row['first_presented_color_overlap'] = pixel(first)
    return result


def compare(live, stand, size, frames, present_calls):
    if not frames or {str(frame) for frame in frames} != present_calls.keys():
        raise ValueError('Image comparison needs the exact certified Present calls')
    results = []
    for frame, quality in frames.items():
        row = {'present_ordinal': frame, 'trace_call': present_calls[str(frame)], 'sample_quality': quality}
        results.append(row)
        folders = (live, stand)
        journals = [frame_journal(folder, frame, size) for folder in folders]
        primary = [dict(planes(folder, frame, size, quality, journal=rows))
                   for folder, rows in zip(folders, journals, strict=True)]
        for name in PLANES:
            (left_hash, left), (right_hash, right) = (side[name] for side in primary)
            row[name] = {'live_sha256': left_hash, 'stand_sha256': right_hash, **pixel_difference(left, right, size[0])}
        native = [native_color_plane(folder, frame, size, quality, journal=rows)
                  for folder, rows in zip(folders, journals, strict=True)]
        row['native_color'] = compare_native_color(*native, primary, size)
        scene_input = [scene_input_color_plane(folder, frame, size) for folder in folders]
        row['scene_input_color'] = compare_native_color(*scene_input, primary, size)
        row['composition'] = compare_composition(folders, journals, frame, size, [side['color'][1] for side in primary])
    plane_match = {name: all(not row[name]['changed_pixels'] for row in results) for name in PLANES}
    return {'match': all(plane_match.values()), 'plane_match': plane_match,
            'comparison': 'bit-exact', 'color': 'presented scRGB RGBA16F, StretchRect resolve',
            'depth': 'scene D24S8, Northlight RESZ to INTZ, point copy to R32F',
            'msaa': 2, 'size': list(size), 'pixels_per_frame': size[0] * size[1],
            'per_sample_images': False, 'frames': results}
