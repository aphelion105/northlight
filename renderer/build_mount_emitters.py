#!/usr/bin/env python3
"""Stage mount lights from server-cached creature models; never edit DBC or WDB."""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths
northlight_paths.use_source_modules()
import client_archives
from world_scene_builder import Assets, array, string, unpack
from build_spell_emitters import spell_sources, append_spell_lights, validate_spell_lights
from mpq import Archive

# Build 12340: Spell.EffectAura[95..97] -> creature entry [110..112],
# server QueryCreatureResponse -> display IDs, then native DBC model rows.
REFERENCES = [
    'https://github.com/TrinityCore/TrinityCore/blob/3.3.5/src/server/game/Spells/Auras/SpellAuraEffects.cpp',
    'https://github.com/TrinityCore/TrinityCore/blob/3.3.5/src/server/game/Server/Packets/QueryPackets.cpp',
    'https://github.com/TrinityCore/TrinityCore/blob/3.3.5/src/server/shared/DataStores/DBCStructure.h',
]


def creature_cache(blob):
    if len(blob) < 24 or blob[:4] != b'BOMW' or unpack('<I', blob, 4)[0] != 12340:
        raise ValueError('Requires build 12340 creaturecache.wdb')
    result, at = {}, 24
    while at < len(blob):
        entry, size = unpack('<II', blob, at)
        at += 8
        if not entry and not size:
            if at != len(blob):
                raise ValueError('Trailing WDB data')
            break
        if size > 65536 or at + size > len(blob):
            raise ValueError('Invalid WDB record length')
        body = blob[at:at+size]
        at += size
        if entry & 0x80000000:
            continue
        cursor = 0
        for _ in range(6):  # Four names, title, cursor icon. No Lua execution.
            end = body.find(b'\0', cursor)
            if end < cursor or end-cursor > 4096:
                raise ValueError('Invalid WDB string')
            cursor = end+1
        if len(body)-cursor != 77:
            raise ValueError('Unknown 12340 creature record layout')
        result[entry] = tuple(v for v in unpack('<4I', body, cursor+24) if v)
    return result


def dbc(assets, name, fields):
    blob = assets.read('dbfilesclient\\'+name+'.dbc')
    magic, count, actual, size, strings = unpack('<4s4I', blob)
    if magic != b'WDBC' or actual != fields or size != fields*4 or len(blob) != 20+count*size+strings:
        raise ValueError('Unsupported 3.3.5 '+name+' schema')
    rows = array(blob, 20, count, '<%dI' % fields)
    if len({r[0] for r in rows}) != count:
        raise ValueError('Duplicate '+name+' IDs')
    return rows, blob[20+count*size:]


def mount_models(assets, cache):
    spells, _ = dbc(assets, 'spell', 234)
    displays, _ = dbc(assets, 'creaturedisplayinfo', 16)
    models, names = dbc(assets, 'creaturemodeldata', 28)
    entries = collections.defaultdict(list)
    for row in spells:
        for i in range(3):
            if row[71+i] == 6 and row[95+i] == 78 and 0 < row[110+i] < 0x80000000:
                entries[row[110+i]].append(row[0])
    display_by_id, model_by_id = {r[0]:r for r in displays}, {r[0]:r for r in models}
    colors = collections.defaultdict(set)
    for row in displays:
        if row[13]:
            colors[row[1]].add(row[13])
    candidates, missing = {}, {}
    for entry, spells in sorted(entries.items()):
        if entry not in cache:
            missing[entry] = 'No current-server creature-cache record'
            continue
        for display in cache[entry]:
            row = display_by_id.get(display)
            model = model_by_id.get(row[1]) if row else None
            if not model:
                missing[entry] = 'Display/model ID absent in this client'
                continue
            path = string(names, model[2]).lower().replace('/', '\\')
            if path.endswith('.mdx'):
                path = path[:-4]+'.m2'
            if not path.startswith('creature\\') or not path.endswith('.m2') or any(p in ('.','..') for p in path.split('\\')):
                missing[entry] = 'Unsupported mount model path'
                continue
            item = candidates.setdefault(path, {'references': [], 'replacement_colors': set()})
            item['references'].append({'creature':entry, 'display':display, 'spells':sorted(set(spells))})
            item['replacement_colors'].update(colors[model[0]])
    for item in candidates.values():
        item['replacement_colors'] = sorted(item['replacement_colors'])
    return candidates, missing


def build(assets, cache_blob, output):
    output.mkdir(parents=True, exist_ok=True)
    candidates, missing = mount_models(assets, creature_cache(cache_blob))
    report = {'format':'native WotLK v264 mount lights', 'models':{}, 'skipped':{},
              'unresolved_creatures':missing, 'creature_cache_sha256':hashlib.sha256(cache_blob).hexdigest(),
              'source_archives':assets.fingerprint(), 'references':REFERENCES,
              'limitations':['Only mount entries known in this server cache can resolve to models.',
                             'At most two strongest proven emitters per model; original particles unchanged.',
                             'Shared models also gain their light when used by NPCs.',
                             'Authored rate and enabled-state are mirrored; external model/emitter visibility still follows the native engine.',
                             'Native light update cost is per animated model, outside the bridge cap.',
                             'No in-game visual or FPS validation performed.']}
    payload = {}
    for path, info in sorted(candidates.items()):
        try:
            original = assets.read(path)
            emitters, refused = spell_sources(assets, original, max_sources=None)
            # Keep real source anchors/colours instead of inventing a centre light.
            selected = sorted(emitters, key=lambda s:(-s['intensity'],s['particles'][0]))[:2]
            _, particle_offset = unpack('<II', original, 296)
            stride = 492 if unpack('<I', original, 16)[0]&512 else 476
            if info['replacement_colors'] and any(unpack('<h', original, particle_offset+i*stride+42)[0] >= 0 for s in selected for i in s['particles']):
                raise ValueError('Shared display variants replace particle colour; no universal M2 light colour')
            patched = append_spell_lights(original, selected)
            validate_spell_lights(original, patched, selected)
        except (ValueError, FileNotFoundError, struct.error) as exc:
            report['skipped'][path] = str(exc)
            continue
        payload[path] = patched
        report['models'][path] = {**info, 'emitters':selected, 'qualified_sources':len(emitters),
            'omitted_sources':len(emitters)-len(selected), 'rejected_particles':refused,
            'before_sha256':hashlib.sha256(original).hexdigest(), 'after_sha256':hashlib.sha256(patched).hexdigest(),
            'source_archive':assets.origin(path)}
    if not payload:
        raise ValueError('No proven mount emitters in the resolved client/server models')
    target = output/'mount-lights-staged.mpq'
    with Archive(target, create=True, capacity=max(4096,len(payload)*2)) as archive:
        temporary = output/'model.tmp'
        for path, blob in payload.items():
            temporary.write_bytes(blob)
            archive.add(temporary, path)
        temporary.unlink()
    with Archive(target) as archive:
        for path, blob in payload.items():
            if archive.read(path) != blob:
                raise ValueError('MPQ readback mismatch: '+path)
    report['counts'] = {'resolved_models':len(candidates), 'patched':len(payload),
        'native_lights':sum(len(v['emitters']) for v in report['models'].values()),
        'mpq_byte_roundtrips':len(payload), 'dbc_files':0, 'wdb_files':0}
    report['mpq_sha256'] = hashlib.sha256(target.read_bytes()).hexdigest()
    (output/'mount-lights-report.json').write_text(json.dumps(report,indent=2)+'\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    client_archives.add_arguments(parser)
    parser.add_argument('--creature-cache', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    assets = Assets.from_args(args)
    try:
        cache = args.creature_cache or assets.client/'Cache/WDB'/assets.locale/'creaturecache.wdb'
        result = build(assets, cache.read_bytes(), args.output)
    finally:
        assets.close()
    print(json.dumps(result['counts'],indent=2))
