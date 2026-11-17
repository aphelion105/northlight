#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client
"""Production volume atlas on native D3D9 and DXVK, without a game process."""
import hashlib
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
sys.path[:0] = [str(Path(__file__).resolve().parents[1]), str(Path(__file__).resolve().parents[1] / 'scripts')]
import northlight_paths as fp
fp.use_source_modules()
from windows_gpu_guard import processes

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--backend', type=Path, action='append', help='Explicit backend scope; default: native D3D9 and packaged DXVK')
parser.add_argument('--benchmark-size', nargs=2, type=int, metavar=('WIDTH', 'HEIGHT'))
args = parser.parse_args()
if args.benchmark_size and any(v < 64 or v > 4096 for v in args.benchmark_size):
    parser.error('Benchmark dimensions must be between 64 and 4096')
selected = args.backend or ([Path(os.environ['NORTHLIGHT_TEST_D3D9'])] if os.environ.get('NORTHLIGHT_TEST_D3D9') else None)
out = fp.output_dir()
out.mkdir(parents=True, exist_ok=True)
watched = [p for root in (fp.SRC, fp.SHADERS) for p in root.rglob('*') if p.is_file()]
watched += [Path(__file__), fp.TESTS/'fog_volume_gpu.cpp', fp.TESTS/'water_fog_endpoint_gpu.cpp']
inputs = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in watched}
exe = out / 'fog-volume-gpu.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                *fp.test_include_flags(), str(fp.TESTS / 'fog_volume_gpu.cpp'), '-luser32', '-o', str(exe)], env=fp.zig_env(), check=True)
results = []
for backend in selected or (Path(os.environ['WINDIR']) / 'SysWOW64/d3d9.dll', fp.client_root() / 'renderer-backends/dxvk/dxvk_d3d9.dll'):
    processes(allow_game=False if args.benchmark_size or os.environ.get('NORTHLIGHT_FOG_VOLUME_BENCHMARK') else None)
    command = [str(exe), str(backend)] + ([str(v) for v in args.benchmark_size] if args.benchmark_size else [])
    result = subprocess.run(command, cwd=out, capture_output=True, text=True, timeout=180)
    (out / (backend.parent.name + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
    print(result.stdout + result.stderr, flush=True)
    result.check_returncode()
    assert 'FOG_VOLUME_GPU_PASS' in result.stdout
    assert 'FOG_GLOBAL_MEDIUM_PASS' in result.stdout
    results.append({'backend': str(backend), 'sha256': hashlib.sha256(backend.read_bytes()).hexdigest(), 'command': command, 'stdout': result.stdout})
assert all(hashlib.sha256(Path(p).read_bytes()).hexdigest() == h for p,h in inputs.items()), 'Inputs changed during comparison'
(out / 'validation.json').write_text(json.dumps({'pass': True, 'game_launched': False, 'explicit_backend_scope': bool(selected),
    'inputs': inputs, 'benchmark_size': args.benchmark_size,
    'environment': {k: os.environ[k] for k in ('DXVK_CONFIG', 'DXVK_FILTER_DEVICE_NAME', 'DISABLE_VULKAN_OBS_CAPTURE') if k in os.environ},
    'results': results}, indent=2) + '\n')
