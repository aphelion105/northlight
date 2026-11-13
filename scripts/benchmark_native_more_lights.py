#!/usr/bin/env python3
"""Paired original/optimized native-light draws; image oracles run in both builds."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-header', type=Path, required=True, help='saved pre-optimization native_more_light_renderer.h')
    parser.add_argument('--only-baseline', action='store_true', help='measure before editing production code')
    args = parser.parse_args()
    baseline = args.baseline_header.resolve(strict=True)
    out = fp.output_dir()
    out.mkdir(parents=True, exist_ok=True)
    fixture = out / 'fixture'
    fixture.mkdir(exist_ok=True)
    env = {**fp.zig_env(), 'NORTHLIGHT_TEST_OUTPUT_DIR': str(fixture), 'DISABLE_VULKAN_OBS_CAPTURE': '1'}
    with (out / 'fixture.log').open('w') as log:
        subprocess.run([sys.executable, str(fp.TESTS / 'test_native_more_lights_gpu.py')], env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=240)
    records, cache, digests = [], [], []
    backends = [('d3d9', Path(os.environ['WINDIR']) / 'SysWOW64/d3d9.dll'),
                ('dxvk', fp.client_root() / 'renderer-backends/dxvk/dxvk_d3d9.dll')]
    labels = ['baseline'] if args.only_baseline else ['baseline', 'current']
    for label in labels:
        exe = out / (label + '.exe')
        define = [f'-DNORTHLIGHT_MORE_LIGHTS_BASELINE_HEADER="{baseline.as_posix()}"'] if label == 'baseline' else ['-DNORTHLIGHT_MORE_LIGHTS_OPTIMIZED']
        command = [fp.zig(), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG', '-Wno-microsoft-exception-spec',
                   '-DNORTHLIGHT_MORE_LIGHTS_BENCHMARK', *define, *fp.test_include_flags(), fp.TESTS / 'native_more_lights_gpu.cpp', '-luser32', '-o', exe]
        with (out / (label + '-compile.log')).open('w') as log:
            subprocess.run(list(map(str, command)), env=fp.zig_env(), stdout=log, stderr=subprocess.STDOUT, check=True, timeout=180)
    # ABBA order on each backend, avoiding concurrent GPU runs.
    for backend, dll in backends:
        for repeat, label in enumerate(labels + list(reversed(labels))):
            run = out / f'{backend}-{repeat}-{label}'
            run.mkdir(exist_ok=True)
            result = subprocess.run(list(map(str, [out / (label + '.exe'), fixture / 'client-variants.bin', dll])),
                                    cwd=run, env={**env, 'DXVK_LOG_PATH': str(run)}, capture_output=True, text=True, timeout=240)
            (run / 'benchmark.log').write_text(result.stdout + result.stderr)
            result.check_returncode()
            digest = next(line.split()[1] for line in result.stdout.splitlines() if line.startswith('MORE_LIGHTS_PROGRAM_DIGEST '))
            digests.append(digest)
            rows = [json.loads(line.removeprefix('MORE_LIGHTS_BENCH ')) for line in result.stdout.splitlines() if line.startswith('MORE_LIGHTS_BENCH ')]
            assert len(rows) == 48 and all(row['warmCreates'] == 0 and row['imageError'] < .0004 for row in rows)
            records.extend(dict(row, backend=backend, build=label, repeat=repeat) for row in rows)
            cache.extend(dict(json.loads(line.removeprefix('MORE_LIGHTS_CACHE ')), backend=backend, build=label, repeat=repeat)
                         for line in result.stdout.splitlines() if line.startswith('MORE_LIGHTS_CACHE '))
            print(f'{backend} {label}: {len(rows)} image-checked timing cases', flush=True)
    assert len(set(digests)) == 1, 'shader corpus output changed between builds/backends'
    report = {'scope': 'component draw submission and GPU timestamp interval; not live game FPS', 'program_digest': digests[0],
              'baseline_header_sha256': hashlib.sha256(baseline.read_bytes()).hexdigest(),
              'current_header_sha256': hashlib.sha256(fp.src('native_more_light_renderer.h').read_bytes()).hexdigest(),
              'cases': records, 'cache_pressure': cache}
    path = out / 'benchmark.json'
    path.write_text(json.dumps(report, indent=2) + '\n')
    print(path)


if __name__ == '__main__':
    main()
