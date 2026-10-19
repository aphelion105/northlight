#!/usr/bin/env python3
"""Build a separate WotLK light-only MPQ from this client's own models.

No game launch. Exact source hashes and evidence are written alongside the MPQ.
Existing native lights, OFF/BROKEN/UNLIT models and unsupported animation are
preserved. Classic v256 particle offsets are never used for WotLK v264.
"""
import argparse
import collections
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import sys

sys.path[:0] = [str(Path(__file__).resolve().parents[1]), str(Path(__file__).resolve().parent)]
from world_scene_builder import Assets, array, string, unpack
from world_lights_builder import constant_track, extract_m2, static_bone_offset
from emitter_pin_report import batches, texture_colour
from outdoor_light_profiles import load, pinned
from m2_emitter_patch import append_lights, verify_preserved
from mpq import Archive

LIGHT_WORDS = re.compile(r'lamp|lantern|torch|brazier|candle|candel|bonfire|campfire|firepit|fireplace')
UNLIT = re.compile(r'off|broken|unlit|extinguish|unlight|firewood|burntout')
FLAME = re.compile(r'flame|fire[0-9_.]|firelick|fireball')
GLOW = re.compile(r'glow|flame|lampglass|candle|lantern|flare')
NOT_BULB = re.compile(r'ground|ring|reflect|smoke|shadow')


def particle_points(assets, blob):
    """Visible additive WotLK flames, with constant enabled/bone state only."""
    count, offset = unpack('<II', blob, 296)
    stride = 492 if unpack('<I', blob, 16)[0] & 0x200 else 476
    if count > 4096 or (count and offset + count * stride > len(blob)):
        raise ValueError('Invalid v264 particle array')
    nt, ot = unpack('<II', blob, 80)
    textures = array(blob, ot, nt, '<4I')
    points = []
    for i in range(count):
        p = offset + stride * i
        bone, tex = unpack('<hH', blob, p + 20)
        blend, _, replacement = unpack('<BBh', blob, p + 40)
        if blend not in (3, 4) or replacement not in (-1, 0) or tex >= len(textures):
            continue
        t = textures[tex]
        name = string(blob, t[3]) if t[0] == 0 and t[2] else ''
        if not FLAME.search(name.rsplit('\\', 1)[-1]) or NOT_BULB.search(name):
            continue
        if constant_track(blob, p + 456, '<B', (1,))[0] != 1:
            continue
        translation = static_bone_offset(blob, bone)
        position = tuple(a + b for a, b in zip(unpack('<3f', blob, p + 8), translation))
        nc, tc, kc, oc = unpack('<4I', blob, p + 260)
        na, ta, ka, oa = unpack('<4I', blob, p + 276)
        if not kc or nc != kc or na != ka or ka != kc:
            continue
        if array(blob, tc, nc, '<H') != array(blob, ta, na, '<H'):
            continue
        colors = array(blob, oc, kc, '<3f')
        alpha = [x[0] for x in array(blob, oa, ka, '<h')]
        if not all(0 <= a <= 32767 for a in alpha) or not sum(alpha):
            continue
        if not all(math.isfinite(x) and 0 <= x <= 255.01 for c in colors for x in c):
            continue
        color = tuple(sum(c[k] * a for c, a in zip(colors, alpha)) / sum(alpha) / 255 for k in range(3))
        texture = texture_colour(assets, name)
        if 'linear' not in texture:
            continue
        color = tuple(color[k] * texture['linear'][k] for k in range(3))
        if max(color) > 0:
            points.append((position, color, 'v264 additive flame: ' + name))
    return points


def glow_points(assets, path, blob):
    skin = assets.read(path[:-3] + '00.skin')
    n, off = unpack('<II', skin, 36)
    units = array(skin, off, n, '<2B11H')
    nc, oc = unpack('<II', blob, 72)
    no, oo = unpack('<II', blob, 88)
    nl, ol = unpack('<II', blob, 144)
    opacity_lookup = [x[0] for x in array(blob, ol, nl, '<H')]
    points = []
    for info, unit in zip(batches(assets, path), units):
        texture = info['texture']
        if not (info['flags'][0] & 1) or info['flags'][1] not in (3, 4):
            continue
        if not GLOW.search(texture) or NOT_BULB.search(texture):
            continue
        if max(b - a for a, b in zip(*info['bounds'])) > 8:
            continue
        index = unit[5]
        if unit[11] >= len(opacity_lookup):
            raise ValueError('Glow opacity lookup outside M2')
        opacity = opacity_lookup[unit[11]]
        if opacity != 65535:
            if opacity >= no:
                raise ValueError('Glow opacity index outside M2')
            if constant_track(blob, oo + opacity * 20, '<h', (32767,))[0] <= 0:
                continue
        tint = (1., 1., 1.)
        if index != 65535:
            if index >= nc:
                raise ValueError('Glow colour index outside M2 colors')
            tint = constant_track(blob, oc + index * 40, '<3f', tint)
            if constant_track(blob, oc + index * 40 + 20, '<h', (32767,))[0] <= 0:
                continue
        color = texture_colour(assets, texture)
        if 'linear' not in color:
            continue
        rgb = tuple(tint[k] * color['linear'][k] for k in range(3))
        if all(math.isfinite(x) and 0 <= x <= 4 for x in rgb) and max(rgb) > 0:
            points.append((tuple(info['center']), rgb, 'v264 unlit additive skin batch: ' + texture))
    return points


def profile(assets, path, blob, catalog):
    sha = hashlib.sha256(blob).hexdigest()
    if path in catalog:
        value = pinned(catalog[path]).get(sha)
        if value:
            pin = catalog[path] if sha == catalog[path]['asset_sha256'] else catalog[path]['variants'][sha]
            return {**value, 'family': catalog[path]['family'], 'position_basis': pin['position_basis']}
    fire = particle_points(assets, blob)
    points = glow_points(assets, path, blob) if LIGHT_WORDS.search(path.rsplit('\\', 1)[-1]) else []
    # A flame's own colour includes its lifetime tint and actual texture.
    points = fire or points
    if not points:
        raise ValueError('No supported luminous geometry or additive flame')
    # One compact luminous region per model. Distant independent flames need a
    # reviewed multi-emitter policy rather than a light invented between them.
    position = tuple(sum(p[0][k] for p in points) / len(points) for k in range(3))
    if any(sum((p[0][k] - position[k]) ** 2 for k in range(3)) > 9 for p in points):
        raise ValueError('Multiple separated luminous regions')
    rgb = tuple(sum(p[1][k] for p in points) / len(points) for k in range(3))
    peak = max(rgb)
    rgb = tuple(x / peak for x in rgb)
    flame = bool(fire) or bool(re.search(r'torch|brazier|fire', path.rsplit('\\', 1)[-1]))
    return {'position': position, 'rgb': rgb, 'intensity': 1.15,
            'start': 1., 'end': 14. if flame else 10.,
            'family': 'wotlk_fire' if flame else 'wotlk_lantern',
            'position_basis': '; '.join(sorted({p[2] for p in points}))}


def build(assets, output, catalog_path):
    output.mkdir(parents=True, exist_ok=True)
    catalog = load() if catalog_path == Path(__file__).with_name('outdoor-light-profiles.json') else load(catalog_path)
    document = json.loads(json.dumps({'version':1,'profiles':catalog}))
    original_profiles = json.loads(catalog_path.read_text())['profiles']
    payload = {}
    report = {'format': 'WotLK MD20/v264', 'models': {}, 'skipped': {}, 'counts': {},
              'source_archives': assets.fingerprint(), 'comfy_reference': 'aloofbit/comfyatmosphere@3d8ac3b',
              'licensing': 'Original MIT implementation using Northlight readers; no GPL source or Classic game assets copied.'}
    stats = collections.Counter()
    for path in sorted(n for n in assets.providers if n.startswith('world\\') and n.endswith('.m2')):
        stats['world_models_scanned'] += 1
        if path not in catalog and UNLIT.search(path.rsplit('\\', 1)[-1]):
            continue
        blob = assets.read(path)
        if len(blob) < 304 or blob[:4] != b'MD20' or unpack('<I', blob, 4)[0] != 264:
            continue
        if unpack('<I', blob, 264)[0]:
            stats['native_lights_preserved'] += 1
            continue
        named = bool(LIGHT_WORDS.search(path.rsplit('\\', 1)[-1]))
        if not named and path not in catalog:
            continue
        try:
            p = profile(assets, path, blob, catalog)
            emitters = p.get('emitters',[p])
            patched = append_lights(blob,emitters)
            verify_preserved(blob,patched,len(emitters))
            decoded = extract_m2(patched, collections.Counter())
            if len(decoded) != len(emitters):
                raise ValueError('Patched native light did not round-trip')
        except (ValueError, IndexError, struct.error, FileNotFoundError) as exc:
            stats['unsupported_candidates'] += 1
            if named:
                report['skipped'][path] = str(exc)
            continue
        before, after = hashlib.sha256(blob).hexdigest(), hashlib.sha256(patched).hexdigest()
        entry = document['profiles'].setdefault(path, {**p, 'asset_sha256': before})
        entry.setdefault('variants', {})[after] = {k: p[k] for k in ('position', 'rgb', 'intensity', 'start', 'end', 'position_basis')}
        if 'emitters' in p:entry['variants'][after]['emitters']=p['emitters']
        entry['injected_sha256'] = after
        payload[path] = patched
        report['models'][path] = {**p, 'original_sha256': before, 'patched_sha256': after,
                                  'source_archive': assets.origin(path), 'original_bytes': len(blob), 'patched_bytes': len(patched)}
        stats['patched_models'] += 1
        if 'expansion02' in path:
            stats['wotlk_expansion_models'] += 1
    if not payload:
        raise ValueError('No supported model changes')
    archive = output / 'patch-y.mpq'
    with Archive(archive, create=True, capacity=max(4096, len(payload) * 2)) as mpq:
        for i, (name, blob) in enumerate(payload.items()):
            temp = output / 'model.tmp'
            temp.write_bytes(blob)
            mpq.add(temp, name)
        temp.unlink()
    with Archive(archive) as mpq:
        for name, blob in payload.items():
            if mpq.read(name) != blob:
                raise ValueError('MPQ round-trip failed: ' + name)
    report['counts'] = dict(stats)
    report['mpq_sha256'] = hashlib.sha256(archive.read_bytes()).hexdigest()
    (output / 'outdoor-light-profiles.json').write_text(json.dumps(document, indent=2) + '\n')
    extension = {}
    for name, value in document['profiles'].items():
        old = original_profiles.get(name)
        if old is None:
            extension[name] = value
        elif old != value:
            extension[name] = {'injected_sha256': value['injected_sha256'],
                               'variants': {s: v for s, v in value['variants'].items() if s not in old.get('variants', {})}}
    (output / 'wotlk-emitter-profiles.json').write_text(json.dumps({'version': 1, 'profiles': extension}, indent=2) + '\n')
    load(output / 'outdoor-light-profiles.json')
    (output / 'emitter-patch.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report['counts']), flush=True)
    return report


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--client', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    assets = Assets(args.client, without='y')
    try:
        build(assets, args.output, Path(__file__).with_name('outdoor-light-profiles.json'))
    finally:
        assets.close()
