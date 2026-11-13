#!/usr/bin/env python3
"""Compare a saved scene builder with the current one on read-only client data.

Outputs and SHA-256 comparisons go under northlight_paths.output_dir(). No game
files are modified. Timings cover scene generation, not the whole installation.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import statistics
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
import world_scene_builder as current


def load(path):
    spec = importlib.util.spec_from_file_location('baseline_scene_builder', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def hashes(root):
    result = {}
    for path in sorted(root.rglob('*')):
        if path.is_file():
            digest = hashlib.sha256()
            with path.open('rb') as stream:
                for block in iter(lambda: stream.read(1 << 20), b''):
                    digest.update(block)
            result[path.relative_to(root).as_posix()] = digest.hexdigest()
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True, help='saved world_scene_builder.py from before the change')
    parser.add_argument('--map', default='Azeroth')
    parser.add_argument('--tile', nargs=2, type=int, action='append', help='repeat for multiple tiles; default: 32 48 and 44 30')
    parser.add_argument('--rounds', type=int, default=2)
    args = parser.parse_args()
    if args.rounds < 1:
        parser.error('--rounds must be positive')
    output = fp.output_dir() / f'installer-scene-{time.time_ns()}'
    output.mkdir(parents=True)
    baseline = load(args.baseline)
    tiles = args.tile or [(32, 48), (44, 30)]
    timings = {'before': [], 'after': []}
    started = time.perf_counter()
    assets = current.Assets(without='z')
    report = {'map': args.map, 'tiles': tiles, 'rounds': args.rounds,
              'archive_setup_seconds': time.perf_counter() - started,
              'archives': assets.fingerprint(),
              'sources': {label: hashlib.sha256(Path(module.__file__).read_bytes()).hexdigest()
                          for label, module in (('before', baseline), ('after', current))}}
    try:
        for run in range(args.rounds):
            order = [('before', baseline), ('after', current)]
            if run % 2:
                order.reverse()
            for label, module in order:
                builder = module.InstancedBuilder(assets, output / label)
                started = time.perf_counter()
                for x, y in tiles:
                    builder.build(args.map, x, y, output / label / args.map / f'{x}_{y}.fg3')
                seconds = time.perf_counter() - started
                timings[label].append(seconds)
                print(f'{label} round {run+1}: {seconds:.3f} s', flush=True)
                del builder
    finally:
        assets.close()
    before, after = hashes(output / 'before'), hashes(output / 'after')
    report.update(seconds=timings, median_seconds={label: statistics.median(values) for label, values in timings.items()},
                  identical=before == after, files=len(before), output_hashes={'before': before, 'after': after})
    report['speedup'] = report['median_seconds']['before'] / report['median_seconds']['after']
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({key: report[key] for key in ('median_seconds', 'speedup', 'identical', 'files')}))
    print(output / 'report.json')
    return 0 if report['identical'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
