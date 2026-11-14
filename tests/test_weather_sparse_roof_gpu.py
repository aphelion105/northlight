#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client timing
"""Experimental sparse roof queries: exact images and 4K shader timings; not enabled."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT/'scripts/shaders')]
import northlight_paths as fp
from shader_compiler import Compiler
from windows_gpu_guard import processes


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--backend', type=Path, action='append')
    args = parser.parse_args()
    running = processes()
    games = any(p['name'].lower() in ('wow.exe', 'wow64.exe', 'wowclient64.exe') for p in running)
    if games and os.environ.get('NORTHLIGHT_TEST_ALLOW_RUNNING_GAME') != '1':
        raise SystemExit('Set NORTHLIGHT_TEST_ALLOW_RUNNING_GAME=1 for contended component checks.')
    out = fp.output_dir()/'sparse-roof';out.mkdir(parents=True, exist_ok=True)
    inputs = [fp.TESTS/'fixtures/precipitation_roof_baseline.hlsl', fp.TESTS/'fixtures/precipitation_sparse_roof_experiment.hlsl',
              fp.src('surface_compiled_shaders.h'), fp.TESTS/'weather_sparse_roof_gpu.cpp', Path(__file__)]
    hashes = lambda: {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
    before = hashes()
    compiler = Compiler(allow_compatible_binary=True)
    for entry in ('RainQuality', 'SnowQuality', 'RainPerformance', 'SnowPerformance'):
        compiler.compile(inputs[0], entry, 'ps_3_0', out/(entry+'.bin'))
        compiler.compile(inputs[1], entry, 'ps_3_0', out/('candidate-'+entry+'.bin'))
    exe = out/'sparse-roof.exe'
    with (out/'compile.log').open('w') as sink:
        subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                        '-Wno-return-type', *fp.test_include_flags(), str(inputs[3]), '-luser32', '-o', str(exe)],
                       env=fp.zig_env(), stdout=sink, stderr=subprocess.STDOUT, check=True, timeout=180)
    report = {'inputs':before, 'uncontended_timing_valid':not games,
              'scope':'Unshipped shader experiment. Images match the frozen production shader exactly. Not game FPS or runtime acceptance.', 'backends':[]}
    backends = args.backend or [Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll',
                               fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll',
                               fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll']
    for i, backend in enumerate(backends):
        run = out/str(i);run.mkdir(exist_ok=True)
        (run/'dxvk.conf').write_text('d3d9.presentInterval = 0\ndxvk.enableStateCache = False\n')
        env = dict(os.environ, DXVK_CONFIG_FILE=str(run/'dxvk.conf'), DXVK_LOG_PATH=str(run), DISABLE_VULKAN_OBS_CAPTURE='1')
        result = subprocess.run([str(exe), str(backend.resolve()), str(out.resolve())], cwd=run, env=env,
                                capture_output=True, text=True, timeout=240, creationflags=subprocess.CREATE_NO_WINDOW)
        text = result.stdout+result.stderr;(run/'gpu.log').write_text(text)
        assert result.returncode == 0 and 'SPARSE_ROOF_PASS cases=720' in text, text[-4000:]
        rows = [dict(re.findall(r'(\w+)=([^ ]+)', line)) for line in text.splitlines() if line.startswith('TIMING ')]
        timings = []
        for scene in range(5):
            for entry in range(2):
                record = {'scene':scene, 'entry':entry}
                for mode in range(2):
                    values = [float(r['ms']) for r in rows if (int(r['scene']), int(r['entry']), int(r['mode'])) == (scene, entry, mode)]
                    assert len(values) == 8 and all(v > 0 for v in values)
                    label = 'candidate' if mode else 'baseline'
                    record[label+'_ms'] = statistics.median(values);record[label+'_samples'] = values
                timings.append(record)
        report['backends'].append({'backend':str(backend), 'image_cases':720, 'timings':timings})
        print(str(backend)+': 720 exact image comparisons passed', flush=True)
        for row in timings:
            print('scene={scene} entry={entry}: {baseline_ms:.4f} -> {candidate_ms:.4f} ms'.format(**row), flush=True)
    assert hashes() == before, 'Inputs changed during GPU checks'
    (out/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print('PASS sparse roof queries; uncontended_timing_valid='+str(not games), flush=True)


if __name__ == '__main__':
    main()
