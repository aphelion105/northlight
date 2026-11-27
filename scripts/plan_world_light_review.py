"""Index the installed world models and their observed placements for visual review."""
import argparse
from collections import Counter, defaultdict
from hashlib import sha256
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'renderer')]
import northlight_paths as fp
from world_lights_builder import INSTANCE


def placements(cache, maps, paths):
    by_key = {sha256(('FGS3-v1:' + path).encode()).digest(): path for path in paths}
    results = defaultdict(dict)
    for name in maps:
        seen = set()
        for tile in sorted((cache / name).glob('*.fg3')):
            data = tile.read_bytes()
            magic, version, count = struct.unpack_from('<4sII', data)
            if magic != b'FGS3' or version != 3 or len(data) != 12 + count * INSTANCE.size:
                raise ValueError(f'Invalid scene tile {tile}')
            for item in INSTANCE.iter_unpack(memoryview(data)[12:]):
                key, uid, kind = item[:3]
                if key not in by_key or item[:3] in seen:
                    continue
                seen.add(item[:3])
                samples = results[by_key[key]].setdefault(name, [])
                if len(samples) < 4:
                    samples.append(dict(uid=uid, kind=kind, bounds=[item[3:6],item[6:9]],
                                        transform=item[9:], tile=tile.name))
    return results


def plan(census, cache):
    maps = census['maps']
    candidates = set(census['models']) | set(census['placed_wmo_native_lights'])
    mesh_places = placements(cache, maps, candidates)
    inventory = {}
    for path in sorted(candidates):
        model = census['models'].get(path)
        evidence = model if model is not None else census['placed_wmo_native_lights'][path]
        samples = mesh_places[path]
        placed = evidence.get('placements', {})
        ordered = sorted(samples, key=lambda name: (maps[name]['instance_type'] not in (1, 2), name))
        if ordered:
            state = 'needs_game_view'
        elif placed:
            state = 'needs_placement_probe'
        else:
            state = 'needs_spawn_or_asset_probe'
        inventory[path] = {'asset_sha256': evidence.get('asset_sha256'),
                           'offline_light_status': evidence.get('status', 'wmo'),
                           'authored_map_counts': placed, 'scene_samples': {name: samples[name] for name in ordered},
                           'review_state': state}
    return {'format': 1, 'archive_fingerprint': census['archive_fingerprint'],
            'maps': {name: {k: entry[k] for k in ('id', 'instance_type', 'status')} for name,entry in maps.items()},
            'counts': dict(Counter(row['review_state'] for row in inventory.values())), 'objects': inventory}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('census', type=Path)
    parser.add_argument('--cache', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    cache = args.cache or fp.client_root() / 'world-cache'
    output = args.output or fp.output_dir() / 'world-light-review-plan.json'
    result = plan(json.loads(args.census.read_text(encoding='utf-8')), cache)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, separators=(',', ':'))
        stream.write('\n')
    print(json.dumps(result['counts']), flush=True)


if __name__ == '__main__':
    main()
