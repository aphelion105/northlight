#!/usr/bin/env python3
"""Stage optional native spell lights; read client assets, never install or edit DBCs."""
import argparse
import collections
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths
northlight_paths.use_source_modules()
import client_archives
from world_scene_builder import Assets, array, string, unpack
from world_lights_builder import constant_track
from build_equipment_emitters import NATIVE_REFERENCE, REFERENCES, sources as particle_sources
from equipment_emitter_patch import append_bone_lights
from mpq import Archive


def lifecycle_track(blob, offset, fmt, default):
    """Read inline native linear/step tracks, including empty sequence defaults."""
    interpolation, sequence, nt, ot, nk, ok = unpack('<hh4I', blob, offset)
    globals, global_offset = unpack('<II', blob, 20)
    if interpolation not in (0, 1) or not -1 <= sequence < globals or nt != nk or nt > 128:
        raise ValueError('Unsupported lifecycle track header')
    if sequence >= 0 and (nt != 1 or not unpack('<I', blob, global_offset + sequence * 4)[0]):
        raise ValueError('Invalid lifecycle global sequence')
    curves = []
    for (tc, to), (kc, ko) in zip(array(blob, ot, nt, '<II'), array(blob, ok, nk, '<II')):
        if tc != kc or tc > 4096:
            raise ValueError('Invalid lifecycle key counts')
        times = [v for (v,) in array(blob, to, tc, '<I')]
        values = [v for (v,) in array(blob, ko, kc, fmt)]
        if any(a >= b for a, b in zip(times, times[1:])) or not all(math.isfinite(v) for v in values):
            raise ValueError('Invalid lifecycle times/values')
        if not values:
            times, values = [0], [default]
        curves.append({'times': times, 'values': values})
    return {'interpolation': interpolation, 'sequence': sequence,
            'curves': curves or [{'times': [0], 'values': [default]}]}


def add_bytes(out, data):
    out.extend(bytes(-len(out) % 4))
    offset = len(out)
    out.extend(data)
    return offset


def write_track(out, track, fmt, scale=1):
    times, keys = [], []
    for curve in track['curves']:
        count = len(curve['times'])
        times.append((count, add_bytes(out, struct.pack('<%dI' % count, *curve['times']))))
        values = [v * scale for v in curve['values']]
        keys.append((count, add_bytes(out, b''.join(struct.pack(fmt, v) for v in values))))
    ot = add_bytes(out, b''.join(struct.pack('<II', *v) for v in times))
    ok = add_bytes(out, b''.join(struct.pack('<II', *v) for v in keys))
    return struct.pack('<hh4I', track['interpolation'], track['sequence'], len(times), ot, len(keys), ok)


def spell_sources(assets, blob):
    if len(blob) < 304 or blob[:4] != b'MD20' or unpack('<I', blob, 4)[0] != 264:
        raise ValueError('Not WotLK v264')
    if unpack('<I', blob, 264)[0]:
        raise ValueError('Existing native lights preserved')
    count, offset = unpack('<II', blob, 296)
    stride = 492 if unpack('<I', blob, 16)[0] & 0x200 else 476
    if count > 4096 or offset + count * stride > len(blob):
        raise ValueError('Invalid particle array')
    result, rejected = [], {}
    for index in range(count):
        try:
            p = offset + index * stride
            rate = lifecycle_track(blob, p + 176, '<f', 0.)
            enabled = lifecycle_track(blob, p + 456, '<B', 1)
            rates = [v for c in rate['curves'] for v in c['values']]
            states = [v for c in enabled['curves'] for v in c['values']]
            peak = max(rates)
            if not all(0 <= v < 100000 for v in rates) or not peak or not all(v in (0, 1) for v in states) or not max(states):
                raise ValueError('No supported positive emission/enable lifecycle')
            # Reuse the strict actual additive texture/lifetime qualification on
            # one particle. Only this disposable qualification copy is neutralized.
            probe = bytearray(blob)
            struct.pack_into('<II', probe, 296, 1, p)
            probe[p + 176:p + 196] = write_track(probe, {'interpolation': 0, 'sequence': -1,
                'curves': [{'times': [0], 'values': [peak]}]}, '<f')
            probe[p + 456:p + 476] = struct.pack('<hh4I', 0, -1, 0, 0, 0, 0)
            source = particle_sources(assets, bytes(probe))[0]
            source.update(particles=[index], rate_track=rate, enable_track=enabled,
                          lifecycle='original bone + sequence/global time; rate-scaled intensity and original enabled-state')
            # Only identical schedules can share a source; unlike unrelated
            # particles, they always switch off together.
            neighbour = next((s for s in result if s['bone'] == source['bone'] and s['rate_track'] == rate
                and s['enable_track'] == enabled and sum((a-b)**2 for a,b in zip(s['position'], source['position'])) < .25**2), None)
            if neighbour:
                rgb = [neighbour['rgb'][k]*neighbour['intensity'] + source['rgb'][k]*source['intensity'] for k in range(3)]
                power = max(rgb)
                neighbour['rgb'], neighbour['intensity'] = tuple(v/power for v in rgb), min(1., power)
                neighbour['particles'].append(index)
                neighbour['particle_blends'] += source['particle_blends']
                neighbour['textures'] = sorted(set(neighbour['textures'] + source['textures']))
            else:
                result.append(source)
        except (ValueError, FileNotFoundError, struct.error) as exc:
            rejected[index] = str(exc)
    if not result:
        raise ValueError('No proven additive source: ' + json.dumps(collections.Counter(rejected.values()), sort_keys=True))
    if len(result) > 2:
        raise ValueError('More than two independent sources/schedules; excluded')
    return result, rejected


def append_spell_lights(original, emitters):
    out = bytearray(append_bone_lights(original, emitters))
    _, records = unpack('<II', out, 264)
    global_count, _ = unpack('<II', original, 20)
    for index, source in enumerate(emitters):
        record = records + index * 156
        out[record + 76:record + 96] = write_track(out, source['rate_track'], '<f', source['intensity'] / source['rate'])
        out[record + 136:record + 156] = write_track(out, source['enable_track'], '<B')
        # CM2Model::AnimateMT commits diffuseIntensity * RGB only when the RGB
        # track updates. Two equal keys keep that native branch running every
        # frame (count > m_uint90); a single constant RGB key freezes intensity.
        timestamp = add_bytes(out, struct.pack('<2I', 0, 1))
        rgb = add_bytes(out, struct.pack('<6f', *source['rgb'], *source['rgb']))
        times = add_bytes(out, struct.pack('<II', 2, timestamp))
        keys = add_bytes(out, struct.pack('<II', 2, rgb))
        struct.pack_into('<hh4I', out, record + 56, 1, global_count, 1, times, 1, keys)
    validate_spell_lights(original, bytes(out), emitters)
    return bytes(out)


def validate_spell_lights(original, patched, emitters):
    preserved = bytearray(patched[:len(original)])
    for start, end in ((20, 28), (264, 272)):
        preserved[start:end] = original[start:end]
    if preserved != original:
        raise ValueError('Original model bytes changed')
    count, offset = unpack('<II', patched, 264)
    if count != len(emitters):
        raise ValueError('Native light count mismatch')
    for index, source in enumerate(emitters):
        record = offset + index * 156
        if unpack('<HH', patched, record) != (1, source['bone']):
            raise ValueError('Original source bone mismatch')
        for actual, wanted in zip(unpack('<3f', patched, record+4), source['position']):
            if not math.isclose(actual, wanted, rel_tol=1e-6, abs_tol=1e-6):
                raise ValueError('Original source position mismatch')
        for relative, fmt, key, scale in ((76, '<f', 'rate_track', source['intensity']/source['rate']), (136, '<B', 'enable_track', 1)):
            actual = lifecycle_track(patched, record+relative, fmt, 0 if fmt == '<f' else 1)
            wanted = source[key]
            if (actual['interpolation'], actual['sequence']) != (wanted['interpolation'], wanted['sequence']):
                raise ValueError('Lifecycle clock/interpolation mismatch')
            if len(actual['curves']) != len(wanted['curves']):
                raise ValueError('Lifecycle sequence count mismatch')
            for a, b in zip(actual['curves'], wanted['curves']):
                if a['times'] != b['times'] or not all(math.isclose(x,y*scale,rel_tol=1e-6,abs_tol=1e-7) for x,y in zip(a['values'],b['values'])):
                    raise ValueError('Lifecycle samples mismatch')
        if not all(math.isclose(x,y,rel_tol=1e-6,abs_tol=1e-6) for x,y in zip(constant_track(patched,record+56,'<3f'), source['rgb'])):
            raise ValueError('Actual emission colour mismatch')
        _, _, _, times, _, _ = unpack('<hh4I', patched, record+56)
        if unpack('<I', patched, times)[0] != 2:
            raise ValueError('Animated intensity requires per-frame RGB update')


def effect_models(assets):
    blob = assets.read('dbfilesclient\\spellvisualeffectname.dbc')
    magic, count, fields, size, length = unpack('<4s4I', blob)
    if magic != b'WDBC' or fields != 7 or size != 28 or len(blob) != 20 + count * size + length:
        raise ValueError('Unsupported 3.3.5 SpellVisualEffectName schema')
    strings = blob[20 + count * size:]
    models = collections.defaultdict(list)
    for row in array(blob, 20, count, '<7I'):
        path = string(strings, row[2])
        if path.endswith('.mdx'):
            path = path[:-4] + '.m2'
        if path.startswith('spells\\') and path.endswith('.m2'):
            models[path].append(row[0])
    return dict(models)


def build(assets, output, excluded):
    output.mkdir(parents=True, exist_ok=True)
    candidates = effect_models(assets)
    report = {'format': 'native WotLK v264 spell lights with authored lifecycle',
        'models': {}, 'skipped': {}, 'excluded_models': sorted(excluded), 'source_archives': assets.fingerprint(),
        'format_references': REFERENCES + [NATIVE_REFERENCE + 'Engine/Source/Model2/M2Model.cpp'],
        'native_lifecycle_symbols': ['CM2Model::AnimateParticlesMT', 'M2AnimateTrack<float, float>',
            'M2AnimateTrack<uint8_t, uint8_t>', 'CM2Model::AnimateMT', 'CM2Model::SetEmittersEnabled', 'CM2Model::DetachFromScene'],
        'installation_ready': False, 'requires_external_emitter_lifecycle_review': True,
        'limitations': ['Only SpellVisualEffectName-referenced Spells/*.m2; no DBC is changed or packed.',
            'Intensity follows authored emission rate immediately; existing particle tails are not simulated.',
            'External CM2Model::SetEmittersEnabled is independent of native lights. The staged patch requires lifecycle review before installation; no runtime hook is included.',
            'Native lights unlink on model detach/free; both source and light also use native model alpha.',
            'Only inline step/linear lifecycle tracks, actual additive particles 3/4, actual texture RGB and lifetime colour.',
            'At most two independent sources per model; runtime shares LocalLightLimit and caps admitted dynamic sources at eight.',
            'Model VS2/3 capture is required for Northlight local/fog lighting; particle-only draws may not expose a light.',
            'No installation or in-game test was performed.'],
        'license': "Original MIT helper; derived model binaries are generated only from the user's client and must not be redistributed."}
    payload = {}
    for path, visual_ids in sorted(candidates.items()):
        if path in excluded:
            report['skipped'][path] = 'Excluded: equipment/enchant patch owns this model'
            continue
        try:
            original = assets.read(path)
            emitters, rejected = spell_sources(assets, original)
            patched = append_spell_lights(original, emitters)
        except (ValueError, FileNotFoundError, struct.error) as exc:
            report['skipped'][path] = str(exc)
            continue
        payload[path] = patched
        report['models'][path] = {'before_sha256': hashlib.sha256(original).hexdigest(),
            'after_sha256': hashlib.sha256(patched).hexdigest(), 'emitters': emitters,
            'rejected_particles': rejected, 'source_archive': assets.origin(path), 'spell_visual_effect_ids': visual_ids}
    if not payload:
        raise ValueError('No supported spell emitters')
    mpq = output / 'spell-lights-staged.mpq'
    with Archive(mpq, create=True, capacity=max(4096, len(payload)*2)) as archive:
        temporary = output / 'model.tmp'
        for name, data in payload.items():
            temporary.write_bytes(data)
            archive.add(temporary, name)
        temporary.unlink()
    with Archive(mpq) as archive:
        names = {p.lower() for p in archive.names() if not p.startswith('(')}
        if names != set(payload) or any(not n.endswith('.m2') for n in names):
            raise ValueError('MPQ contains unexpected/non-M2 files')
        for name, data in payload.items():
            if archive.read(name) != data:
                raise ValueError('MPQ byte roundtrip failed: ' + name)
    report['counts'] = {'referenced_models': len(candidates), 'excluded_owned_models': sum(p in excluded for p in candidates),
        'patched': len(payload), 'native_lights': sum(len(v['emitters']) for v in report['models'].values()),
        'animated_rate_sources': sum(len({x for c in e['rate_track']['curves'] for x in c['values']}) > 1 for v in report['models'].values() for e in v['emitters']),
        'gated_enable_sources': sum(any(0 in c['values'] for c in e['enable_track']['curves']) for v in report['models'].values() for e in v['emitters']),
        'validated_native_models': len(payload), 'mpq_byte_roundtrips': len(payload), 'dbc_files': 0, 'skin_files': 0, 'texture_files': 0}
    report['mpq_sha256'] = hashlib.sha256(mpq.read_bytes()).hexdigest()
    (output / 'spell-lights-report.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    client_archives.add_arguments(parser)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--exclude-report', type=Path, required=True, help='Equipment/enchant report whose model paths must not be replaced')
    args = parser.parse_args()
    excluded = {p.lower().replace('/', '\\') for p in json.loads(args.exclude_report.read_text())['models']}
    assets = Assets.from_args(args)
    try:
        result = build(assets, args.output, excluded)
    finally:
        assets.close()
    print(json.dumps(result['counts'], indent=2))
