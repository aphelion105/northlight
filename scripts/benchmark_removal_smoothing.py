"""Paired shader timings on frozen inputs, with exact image and A/A controls."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT/'tests')]
import northlight_paths as fp
from validate_native_scene_trace import processes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--candidate', type=Path, action='append', required=True)
    parser.add_argument('--capture-prefix', required=True)
    parser.add_argument('--backend', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--rounds', type=int, default=5)
    parser.add_argument('--draws', type=int, default=64)
    args = parser.parse_args()
    assert args.rounds >= 3 and 16 < args.draws <= 512
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    initial = processes()
    prefix = str(Path(args.capture_prefix).resolve())
    baseline, backend = args.baseline.resolve(), args.backend.resolve()
    candidates = [baseline, *(p.resolve() for p in args.candidate)]
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    paths = [Path(__file__).resolve(), fp.TESTS/'removal_distance_gpu.cpp',
             fp.COMPILED/'WorldNormals.bin', backend, *candidates]
    paths += [Path(prefix+s) for s in ('constants.f32', 'scene.fgr', 'light.fgr',
                                      'baseline.fgr', 'normals.fgr', 'distance.fgr')]
    paths += [Path(prefix+s) for s in ('depth.fgr', 'ao.fgr') if Path(prefix+s).is_file()]
    hashes = {str(p): sha(p) for p in paths}
    exe = out/'removal.exe'
    command = [fp.zig(), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG',
               '-std=c++17', fp.TESTS/'removal_distance_gpu.cpp', '-luser32', '-o', exe]
    with (out/'compile.log').open('w') as log:
        subprocess.run(list(map(str, command)), env=fp.zig_env(), stdout=log,
                       stderr=subprocess.STDOUT, check=True)
    config = out/'dxvk.conf'
    config.write_text('dxvk.enableStateCache = False\nd3d9.presentInterval = 0\n')
    env = {**fp.zig_env(), 'DXVK_CONFIG_FILE': str(config), 'DXVK_LOG_PATH': str(out),
           'NORTHLIGHT_REMOVAL_DIRECT_NORMALS': '1', 'NORTHLIGHT_REMOVAL_EXACT': '1',
           'NORTHLIGHT_REMOVAL_BENCH_DRAWS': str(args.draws)}
    runs = []
    report = dict(passed=False, inputs=hashes, processes_before=initial, runs=runs,
                  scope='Paired isolated RemovalSmooth pass on frozen inputs; partial capture, not gameplay FPS.')
    try:
        for round_index in range(args.rounds):
            order = list(enumerate(candidates))
            if round_index % 2:
                order.reverse()
                env['NORTHLIGHT_REMOVAL_CANDIDATE_FIRST'] = '1'
            else:
                env.pop('NORTHLIGHT_REMOVAL_CANDIDATE_FIRST', None)
            for index, candidate in order:
                name = f'round-{round_index}-candidate-{index}'
                processes()
                command = [exe, prefix, baseline, candidate, fp.COMPILED/'WorldNormals.bin',
                           fp.COMPILED/'WorldNormals.bin', name, backend]
                result = subprocess.run(list(map(str, command)), cwd=out, env=env,
                                        capture_output=True, text=True, timeout=120)
                (out/(name+'.log')).write_text(result.stdout+result.stderr, encoding='utf-8')
                result.check_returncode()
                rows = [json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
                comparison = next(row for row in rows if 'old_ms' in row)
                assert comparison['changed'] == 0 and comparison['nonfinite'] == 0
                samples = [row for row in rows if 'block' in row]
                assert len(samples) == 8
                # Store candidate/baseline; adjacent pairs counterbalance AB/BA order.
                pairs = [dict((row['variant'], row['ms']) for row in samples[i:i+2])
                         for i in range(0,8,2)]
                ratios = [pair[1]/pair[0] for pair in pairs]
                runs.append(dict(round=round_index, candidate=str(candidate), control=index==0,
                                 comparison=comparison, samples=samples, paired_ratios=ratios))
                print(name, candidate.name, 'ratio', round(statistics.median(ratios), 4),
                      'changed', comparison['changed'], flush=True)
        assert all(sha(Path(path)) == digest for path, digest in hashes.items())
        report['processes_after'] = processes()
        report['summary'] = {str(candidate): dict(
            median_ratio=statistics.median(ratio for run in runs if run['candidate']==str(candidate)
                                          for ratio in run['paired_ratios']),
            run_ratios=[statistics.median(run['paired_ratios']) for run in runs
                        if run['candidate']==str(candidate)]) for candidate in candidates}
        report['passed'] = True
    finally:
        (out/'validation.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')


if __name__ == '__main__':
    main()
