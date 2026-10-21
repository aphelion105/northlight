# northlight-test: requires=windows-gpu,zig,client
"""Paired production-bytecode regression and timestamp benchmark; no game."""
from pathlib import Path
import json
import os
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

out = fp.out() / 'five-path-perf'
out.mkdir(parents=True, exist_ok=True)
base=out/'baseline';base.mkdir(exist_ok=True)
for name in ('WorldFog','FogClouds','WorldComposite'):
    (base/(name+'.bin')).write_bytes((fp.TESTS/'fixtures/performance-before'/('perf-before-'+name+'.bin')).read_bytes())
exe = out / 'five-path-perf.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                '-Wno-return-type', str(fp.TESTS/'five_path_perf_gpu.cpp'), '-luser32', '-o', str(exe)],
               env=fp.zig_env(), check=True)
backend = os.environ.get('NORTHLIGHT_TEST_D3D9', str(fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll'))
subprocess.run([str(exe), str(base), str(fp.COMPILED), backend, str(out/'gpu.json')],
               cwd=out, check=True, timeout=180)
rows = json.loads((out/'gpu.json').read_text())
assert len(rows) == 12 and not any(row['failures'] for row in rows)
print('PASS 12 paired fog/composite GPU cases; pixel error <= 1e-5; timestamp medians saved')
