# northlight-test: requires=windows-gpu,zig,client timing
"""Exact point-cell shadow submission with unchanged coarse directional batches."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
from windows_gpu_guard import processes

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--backend', type=Path, action='append')
p.add_argument('--benchmark', action='store_true')
p.add_argument('--real-scene', action='store_true')
p.add_argument('--map', default='Northrend')
p.add_argument('--center', nargs=3, type=float, default=[0, 0, 0])
p.add_argument('--light', nargs=4, type=float, action='append', metavar=('X', 'Y', 'Z', 'RANGE'))
a = p.parse_args()
if a.real_scene and not a.light:
    p.error('--real-scene requires --light; each sphere must be within 64 units of --center')
processes(allow_game=False)
out = fp.output_dir() / 'point-shadow-spatial'
out.mkdir(parents=True, exist_ok=True)
inputs = {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
          for folder in (fp.SRC, fp.SHADERS) for path in folder.rglob('*') if path.is_file()}
for path in (Path(__file__), fp.TESTS / 'point_shadow_spatial_gpu.cpp', fp.TESTS / 'water_fog_endpoint_gpu.cpp'):
    inputs[str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
point = fp.src('world_point_rendering.inl').read_text()
first = point.index('    Batch pointBatchRun(')
last = point.index('    // Content digests of the six faces', first)
(out / 'point_spatial_batch_method.inl').write_text(point[first:last], encoding='utf-8', newline='\n')
lights = a.light or [[0, 0, 4, 12], [20, -20, 5, 16], [33.3, 10, 6, 24]]
(out / 'lights.txt').write_text(''.join(' '.join(map(str, row)) + '\n' for row in lights))
exe = out / 'point-shadow-spatial.exe'
command = [str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
           '-Wno-return-type', '-I', str(out), *fp.test_include_flags(), str(fp.TESTS / 'point_shadow_spatial_gpu.cpp'),
           str(fp.src('world_gi.cpp')), str(fp.src('world_mesh_plan.cpp')), '-luser32', '-o', str(exe)]
with (out / 'compile.log').open('w') as log:
    run = subprocess.run(command, env=fp.zig_env(), stdout=log, stderr=subprocess.STDOUT)
if run.returncode:
    print((out / 'compile.log').read_text()[-5000:])
    run.check_returncode()
data = bytearray(exe.read_bytes())
offset = struct.unpack_from('<I', data, 0x3c)[0] + 22
struct.pack_into('<H', data, offset, struct.unpack_from('<H', data, offset)[0] | 0x20)
exe.write_bytes(data)
report = {'pass': False, 'scope': 'Complete static cube submission, production pages/shaders/merger; no actor replay, cache reuse, or game FPS claim',
          'real_scene': a.real_scene, 'map': a.map, 'center': a.center, 'lights': lights, 'inputs': inputs,
          'command': command, 'exe_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(), 'runs': []}
for backend in a.backend or [Path(os.environ['WINDIR']) / 'SysWOW64/d3d9.dll', fp.client_root() / 'renderer-backends/dxvk/dxvk_d3d9.dll']:
    processes(allow_game=False)
    command = [str(exe), str(backend), str(fp.world_cache()) if a.real_scene else '-', *map(str, a.center), str(out / 'lights.txt'), a.map, str(int(a.benchmark))]
    run = subprocess.run(command, cwd=out, env={**fp.zig_env(), 'DISABLE_VULKAN_OBS_CAPTURE': '1', 'DXVK_LOG_PATH': str(out)}, capture_output=True, text=True, timeout=180)
    (out / (backend.parent.name + '.log')).write_text(run.stdout + run.stderr, encoding='utf-8')
    print(run.stdout + run.stderr, flush=True)
    run.check_returncode()
    assert f'POINT_SPATIAL_GPU_PASS lights={len(lights)}' in run.stdout
    rows = [dict(re.findall(r'(\w+)=([^ ]+)', line)) for line in run.stdout.splitlines() if line.startswith('SPATIAL_CUBE ')]
    directional = [dict(re.findall(r'(\w+)=([^ ]+)', line)) for line in run.stdout.splitlines() if line.startswith('SPATIAL_DIRECTIONAL ')]
    assert len(rows) == len(lights) and len(directional) == 4
    broadphase = [dict(re.findall(r'(\w+)=([^ ]+)', line)) for line in run.stdout.splitlines() if line.startswith('POINT_TREE_SCENE ')]
    assert len(broadphase) == len(lights)
    batched = [dict(re.findall(r'(\w+)=([^ ]+)', line)) for line in run.stdout.splitlines() if line.startswith('DIRECTIONAL_BATCH ')]
    assert len(batched) == 4 and all(row['depthExact'] == '1' for row in batched)
    report['runs'].append({'broadphase': broadphase, 'backend': str(backend), 'backend_sha256': hashlib.sha256(backend.read_bytes()).hexdigest(), 'command': command, 'rows': rows, 'directional': directional, 'directional_batches': batched})
assert all(hashlib.sha256(Path(path).read_bytes()).hexdigest() == digest for path, digest in inputs.items()), 'Inputs changed during comparison'
report['pass'] = True
(out / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
