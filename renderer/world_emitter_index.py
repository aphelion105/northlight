"""Renderer-owned world emitters staged in MPQ and compiled into the FGL cache.

World M2 native lights are never injected alongside these records: that would
apply the same source in both the client's lighting and Northlight LocalDirect.
"""
import collections
import hashlib
import json
import math
import struct

from outdoor_light_profiles import load, pinned
from world_emitter_coverage import inspect_model, consolidate, DISABLED

ARCHIVE_PATH = 'northlight\\world-emitters-v1.json'
CAST_SHADOW, FIRE, CANDLE = 2, 4, 8


def profile(assets, path, blob, catalog, cache=None):
    if not path.startswith('world\\') or not path.endswith('.m2') or len(blob) < 304:
        return None
    if blob[:4] != b'MD20' or struct.unpack_from('<I', blob, 4)[0] != 264 or struct.unpack_from('<I', blob, 264)[0]:
        return None
    if DISABLED.search(path.rsplit('\\', 1)[-1]):
        return None
    digest = hashlib.sha256(blob).hexdigest()
    reviewed = pinned(catalog[path]).get(digest) if path in catalog else None
    if reviewed is not None:
        family = catalog[path]['family']
        kind = 'candle' if 'candle' in family else 'fire' if 'fire' in family else 'energy'
        sources = reviewed.get('emitters', [reviewed])
        regions = [dict(position=s['position'], emission=kind,
                        flux=tuple(c*s['intensity']/.35 for c in s['rgb']),
                        position_basis='Exact reviewed model pin '+digest) for s in sources]
        # Reuse reviewed energy/ranges; only compact multi-region fixtures merge.
        if len(regions) > 1:
            try:
                emitters = consolidate(regions)
                for emitter in emitters:
                    emitter['end'] = max(s['end'] for s in sources)
                    emitter['start'] = min(s['start'] for s in sources)
            except ValueError:
                emitters = [{**s, 'emission': kind} for s in sources]
        else:
            emitters = [{**sources[0], 'emission': kind}]
        return {'asset_sha256': digest, 'basis': 'reviewed', 'emitters': emitters}
    measured = inspect_model(assets, path, blob, cache)
    if measured['status'] != 'addable':
        return None
    return {'asset_sha256': digest, 'basis': 'measured', 'partial': measured.get('partial', False),
            'emitters': measured['emitters']}


def flags(source):
    return CAST_SHADOW | (FIRE if source.get('emission') == 'fire' else CANDLE if source.get('emission') == 'candle' else 0)


def records(value):
    return [(tuple(s['position']), tuple(c*s['intensity'] for c in s['rgb']), s['start'], s['end'],
             0x80000000+i, 3, flags(s)) for i,s in enumerate(value['emitters'])]


def build(assets):
    catalog, cache, models, stats = load(), {}, {}, collections.Counter()
    for path in sorted(n for n in assets.providers if n.startswith('world\\') and n.endswith('.m2')):
        stats['scanned'] += 1
        try:
            value = profile(assets, path, assets.read(path), catalog, cache)
        except (ValueError, FileNotFoundError, IndexError, KeyError, struct.error):
            stats['unsupported'] += 1
            continue
        if value is None:
            continue
        # Keep the MPQ small: full material/texture evidence stays in audit reports.
        models[path] = {**{k:v for k,v in value.items() if k != 'emitters'},
                        'emitters': [{k:s[k] for k in ('position','rgb','intensity','start','end','emission')} for s in value['emitters']]}
        stats[value['basis']] += 1
        stats['emitters'] += len(value['emitters'])
    document = {'version': 1, 'owner': 'northlight-local-direct', 'models': models}
    return json.dumps(document, sort_keys=True, separators=(',', ':'), allow_nan=False).encode('utf-8'), dict(stats)


class Index:
    def __init__(self, assets):
        try:
            payload = assets.read(ARCHIVE_PATH)
            self.origin = 'mpq'
        except FileNotFoundError:
            payload, _ = build(assets)
            self.origin = 'generated-for-mpq'
        if len(payload) > 32*1024*1024:
            raise ValueError('World emitter index exceeds bounded metadata size')
        self.digest = hashlib.sha256(payload).hexdigest()
        document = json.loads(payload)
        if document.get('version') != 1 or document.get('owner') != 'northlight-local-direct':
            raise ValueError('Unsupported world emitter index owner/version')
        self.models = document['models']
        if not isinstance(self.models, dict) or len(self.models) > 20000:
            raise ValueError('World emitter model count')
        for path,value in self.models.items():
            if not path.startswith('world\\') or not path.endswith('.m2') or any(p in ('.','..') for p in path.split('\\')):
                raise ValueError('World emitter model path')
            digest = value['asset_sha256']
            if not isinstance(digest,str) or len(digest)!=64 or any(c not in '0123456789abcdef' for c in digest):
                raise ValueError('World emitter model hash')
            if not 1 <= len(value['emitters']) <= 16:
                raise ValueError('World emitter source count')
            for s in value['emitters']:
                if len(s['position'])!=3 or len(s['rgb'])!=3 or s['emission'] not in ('fire','candle','energy'):
                    raise ValueError('World emitter vectors/type')
                if not all(math.isfinite(x) for x in (*s['position'],*s['rgb'],s['intensity'],s['start'],s['end'])):
                    raise ValueError('World emitter nonfinite value')
                if max(map(abs,s['position']))>100 or not all(0<=x<=1 for x in s['rgb']) or max(s['rgb'])<=0:
                    raise ValueError('World emitter position/colour bounds')
                if not 0<s['intensity']<=4 or not 0<=s['start']<s['end']<=32:
                    raise ValueError('World emitter energy/range bounds')

    def get(self, path, blob):
        value = self.models.get(path)
        if value is None:
            return None
        if hashlib.sha256(blob).hexdigest() != value['asset_sha256']:
            raise ValueError('World emitter MPQ source hash changed: '+path)
        return value
