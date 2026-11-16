#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client
"""Real mutable VB/IB capture, shared consumers, bounds and independent raster output."""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys
import zipfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

parser = argparse.ArgumentParser(__doc__)
parser.add_argument('--baseline-ref')
parser.add_argument('--pairs', type=int, default=5)
args = parser.parse_args()
assert 1 <= args.pairs <= 12
out = fp.output_dir()/'range-capture';out.mkdir(parents=True, exist_ok=True)
roots = {'current': fp.REPO}
if args.baseline_ref:
    baseline = out/'baseline';baseline.mkdir(exist_ok=True)
    archive = subprocess.check_output(['git', 'archive', '--format=zip', args.baseline_ref, 'src'], cwd=fp.REPO)
    with zipfile.ZipFile(io.BytesIO(archive)) as files:
        for item in files.infolist():
            path = baseline/item.filename
            assert path.resolve().is_relative_to(baseline.resolve())
            if not item.is_dir():
                path.parent.mkdir(parents=True, exist_ok=True);path.write_bytes(files.read(item))
    roots['baseline'] = baseline
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
watched = [p for p in fp.SRC.rglob('*') if p.is_file()]+[Path(__file__), fp.TESTS/'range_capture_gpu.cpp']
inputs = {str(p): sha(p) for p in watched}
backend = Path(os.environ.get('NORTHLIGHT_TEST_D3D9', fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll'))
env = fp.zig_env();env['DXVK_LOG_PATH'] = str(out)
executables = {}
patterns = ('sparse', 'immutable', 'dense', 'discard')
for name, root in roots.items():
    exe = out/(name+'.exe')
    includes = [flag for folder in sorted((root/'src').iterdir()) if folder.is_dir() for flag in ('-I', str(folder))]
    command = [str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG', '-Wno-microsoft-exception-spec',
               *(['-DNORTHLIGHT_RANGE_BASELINE=1'] if name == 'baseline' else []), *includes,
               str(fp.TESTS/'range_capture_gpu.cpp'), '-luser32', '-o', str(exe)]
    subprocess.run(command, env=env, check=True)
    for pattern in patterns:
        result = subprocess.run([str(exe), str(backend), 'verify', pattern], cwd=out, env=env, text=True, capture_output=True, timeout=180)
        (out/(name+'-'+pattern+'.log')).write_text(result.stdout+result.stderr)
        print(result.stdout+result.stderr, flush=True);result.check_returncode()
        assert 'RANGE_CAPTURE_PASS' in result.stdout and 'imagePairs=8 ' in result.stdout and 'RANGE_TERRAIN_PASS' in result.stdout
    executables[name] = exe
rows = []
if args.baseline_ref:
    for pattern in patterns:
        for pair in range(args.pairs+1):
            for name in (['baseline', 'current'] if pair % 2 == 0 else ['current', 'baseline']):
                result = subprocess.check_output([str(executables[name]), str(backend), 'benchmark', pattern], cwd=out, env=env, text=True)
                row = dict(re.findall(r'(\w+)=([^\s]+)', result));row.update(name=name, pair=pair);rows.append(row)
        assert len({row['digest'] for row in rows if row['pattern'] == pattern}) == 1
summary = {pattern: {name: statistics.median(float(row['cpuMs']) for row in rows if row['name'] == name and row['pair'] and row['pattern'] == pattern) for name in roots} for pattern in patterns} if rows else {}
assert all(sha(Path(p)) == digest for p, digest in inputs.items()), 'Inputs changed during validation'
(out/'validation.json').write_text(json.dumps({'pass': True, 'inputs': inputs, 'backend_sha256': sha(backend),
    'rows': rows, 'median_cpu_ms': summary, 'game_launched': False,
    'scope': 'CPU snapshot and position-bounds capture for 96 mutable-buffer draws and three shared consumers; includes source writes; excludes shader admission, poses and game FPS.'}, indent=2)+'\n')
print(json.dumps(summary, indent=2))
