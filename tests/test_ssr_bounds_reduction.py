# northlight-test: requires=windows-gpu,zig,client
"""Exact old/new R32 min/max comparison, including a saved 4K game depth."""
from pathlib import Path
import json
import os
import struct
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

out = fp.out()/'bounds-ab'
out.mkdir(exist_ok=True)
exe = out/'bounds-ab.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG',
                '-Wno-return-type', str(fp.TESTS/'composite_distance_gpu.cpp'), '-luser32', '-o', str(exe)],
               env=fp.zig_env(), check=True)
base = fp.REPO.parent/'five-path-perf-20261021/shaders/compiled'
backend = os.environ.get('NORTHLIGHT_TEST_D3D9', str(fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll'))


def image(path, w, h, fmt, pixel):
    path.write_bytes(struct.pack('<5I', 0x31524746, w, h, fmt, len(pixel))+pixel*(w*h))


rows = []
for scenario in ('near', 'far', 'sky', 'partition', 'mip', 'real'):
    prefix = str(out/(scenario+'-'))
    w, h = (3840, 2160) if scenario == 'real' else (513, 257)
    bw, bh = (2048, 2048) if scenario == 'real' else (256, 128)
    image(Path(prefix+'scene.fgr'), w, h, 22, bytes((32, 64, 96, 255)))
    image(Path(prefix+'light.fgr'), bw, bh, 113, struct.pack('<4e', 20, 200, 0, 1))
    image(Path(prefix+'distance.fgr'), bw, bh, 114, struct.pack('<f', 100))
    for suffix in ('normals', 'baseline'):
        image(Path(prefix+suffix+'.fgr'), 1, 1, 113, struct.pack('<4e', 0, 0, 1, 1))
    if scenario == 'real':
        actual = fp.client_root()/'northlight-diagnostics/water-session-83636-54129109/capture-1-water-completed-depth.fgr'
        Path(prefix+'depth.fgr').write_bytes(actual.read_bytes())
    else:
        values = []
        for y in range(h):
            for x in range(w):
                d = 1 if scenario == 'sky' else .9999 if scenario == 'far' else 0 if scenario == 'near' else ((x*37+y*17)%1001)/1000
                values.append(d)
        Path(prefix+'depth.fgr').write_bytes(struct.pack('<5I', 0x31524746, w, h, 114, 4)+struct.pack('<'+'f'*len(values), *values))
    c = [[0.]*4 for _ in range(68)]
    c[0] = [1/w, 1/h, .2, 1277]
    c[1] = [1.104, 1.9626, 1, 0]
    c[2] = [.1 if scenario == 'partition' else 0, 1/.8 if scenario == 'partition' else 1/.94, 0, 0]
    c[24] = [1/w, 1/h, 0, 0 if scenario == 'mip' else 1]
    c[33] = [w, h, bw, bh]
    for i in range(4):
        c[3+i][i] = 1
    Path(prefix+'constants.f32').write_bytes(struct.pack('<272f', *sum(c, [])))
    for repeat in range(3 if scenario == 'real' else 1):
        run = subprocess.run([str(exe), prefix, str(base/'SSRDepthBounds.bin'), str(fp.COMPILED/'SSRDepthBounds.bin'),
                              'bounds-'+scenario, str(fp.COMPILED/'WorldNormals.bin'), backend],
                             cwd=out, capture_output=True, text=True, timeout=90)
        (out/f'{scenario}-{repeat}.txt').write_text(run.stdout+run.stderr)
        print(run.stdout, end='', flush=True)
        assert run.returncode == 0, run.stdout+run.stderr
        row = json.loads(next(l for l in run.stdout.splitlines() if l.startswith('{')))
        assert row['changed'] == row['nonfinite'] == 0, row
        rows.append(row)
(out/'validation.json').write_text(json.dumps(rows, indent=2)+'\n')
print('PASS exact R32 interval results: near/far/sky, partitioned viewport, mip reduction and real 4K depth')
