# northlight-test: requires=windows-gpu,zig,client timing
"""Sparse imported fog: CPU interval oracle, complete GPU images and optional ABBA."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
from windows_gpu_guard import processes

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--benchmark', action='store_true')
p.add_argument('--baseline', type=Path, help='Frozen pre-change compiled shader directory')
p.add_argument('--backend', type=Path, action='append')
a = p.parse_args()
def inputs():
    paths = [p for root in (fp.SRC, fp.SHADERS) for p in root.rglob('*') if p.is_file()]
    paths += [Path(__file__), fp.TESTS / 'imported_fog_sparse_gpu.cpp', fp.TESTS / 'validate_fog_actor_interval_gpu.py', fp.TESTS / 'fog_actor_tiles_fixture.h']
    if a.baseline:
        paths += [a.baseline / (name + '.bin') for name in ('ForeverFogActor', 'ForeverFogActorNoNoise')]
    return {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}

initial_inputs = inputs()
out = fp.output_dir() / 'imported-fog-sparse' 
out.mkdir(parents=True, exist_ok=True)
processes(allow_game=False)
prefix = out / 'probe-'
c = [[0.] * 4 for _ in range(79)]
c[0], c[1], c[2] = [1/16, 1/16, .5, 1000], [1, 1, 1, 0], [1, 48, 192, 1/1024]
Path(str(prefix) + 'constants.f32').write_bytes(struct.pack('<316f', *(v for row in c for v in row)))
for name, fmt, stride, values in [('scene', 116, 16, [.5, .5, .5, 1]), ('depth', 114, 4, [.99])]:
    Path(str(prefix) + name + '.fgr').write_bytes(struct.pack('<5I', 0x31524746, 16, 16, fmt, stride) + struct.pack('<%df' % len(values), *values) * 256)
subprocess.run([sys.executable, str(fp.TESTS / 'validate_fog_actor_interval_gpu.py'), '--prefix', str(prefix), '--output', str(out / 'interval'), '--sparse'], check=True)
exe = out / 'imported-fog-sparse.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG', '-std=c++17',
                *fp.test_include_flags(), str(fp.TESTS / 'imported_fog_sparse_gpu.cpp'), '-luser32', '-o', str(exe)], env=fp.zig_env(), check=True)
report = {'pass': False, 'game_launched': False, 'synthetic_medium_and_casters': True, 'game_fps_claim': False, 'cases': []}
for backend in a.backend or (Path(os.environ['WINDIR']) / 'SysWOW64/d3d9.dll', fp.client_root() / 'renderer-backends/dxvk/dxvk_d3d9.dll'):
    processes(allow_game=False)
    env = fp.zig_env()
    if a.benchmark:
        env['NORTHLIGHT_SPARSE_FOG_BENCHMARK'] = '1'
    command = [str(exe), str(backend)] + ([str(a.baseline.resolve())] if a.baseline else [])
    run = subprocess.run(command, cwd=out, env=env, capture_output=True, text=True, timeout=180)
    (out / (backend.parent.name + '.log')).write_text(run.stdout + run.stderr, encoding='utf-8')
    print(run.stdout + run.stderr, flush=True)
    run.check_returncode()
    assert 'IMPORTED_FOG_SPARSE_PASS cases=24' in run.stdout
    report['cases'].append({'backend': str(backend), 'backend_sha256': hashlib.sha256(backend.read_bytes()).hexdigest(), 'command': command, 'stdout': run.stdout})
assert inputs() == initial_inputs, 'Inputs changed during the comparison'
report['inputs'] = initial_inputs
report['pass'] = True
(out / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
