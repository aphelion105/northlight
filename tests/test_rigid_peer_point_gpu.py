# northlight-test: requires=windows-gpu,zig,client,stormlib manual
"""Opaque Westguard barracks faces match fail-open replay after peer readiness."""
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
fp.use_source_modules()
from world_scene_builder import Assets, Builder

p = argparse.ArgumentParser(__doc__)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--backend', type=Path, action='append', required=True)
a = p.parse_args()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=False)
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
inputs = {str(q): sha(q) for q in [Path(__file__).resolve(), fp.TESTS/'rigid_peer_point_gpu.cpp',
          fp.src('replay_bounds.h'), fp.src('replay_envelopes.inl'), fp.src('position_bounds_scan.h'),
          fp.src('point_light_shadow.h'), fp.src('local_light_compiled_shaders.h')]}
path = r'world\wmo\northrend\buildings\human\nd_human_barracks\nd_human_barracks.wmo'
assets = Assets(archives='stock')
try:
    vertices, triangles, materials, _, _ = Builder(assets).wmo(path)
    opaque = [triangle for triangle in triangles if materials[triangle[3]][1] == 0]
    data = b''.join(struct.pack('<5f', *vertices[index][:3], *vertices[index][6:8])
                    for triangle in opaque for index in triangle[:3])
    source_hash = hashlib.sha256(assets.read(path)).hexdigest()
finally:
    assets.close()
fixture = out / 'barracks.bin'
fixture.write_bytes(struct.pack('<I', len(data)//20) + data)
env = fp.zig_env()
env.update(DXVK_LOG_PATH=str(out), DISABLE_VULKAN_OBS_CAPTURE='1')
exe = out / 'rigid-peer-point.exe'
command = [str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2',
           '-UNDEBUG', *fp.test_include_flags(), str(fp.TESTS/'rigid_peer_point_gpu.cpp'),
           '-luser32', '-o', str(exe)]
subprocess.run(command, env=env, check=True)
processes = subprocess.check_output(['powershell', '-NoProfile', '-Command',
    "Get-Process Wow,Wow-64 -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Id; exit 0"], text=True).split()
report = dict(source=path, source_sha256=source_hash, opaque_triangles=len(opaque),
              excluded_nonopaque_triangles=len(triangles)-len(opaque),
              fixture_sha256=hashlib.sha256(fixture.read_bytes()).hexdigest(),
              game_concurrent=bool(processes), game_pids=processes, timing_valid=False, inputs=inputs,
              limitation='Authored lights and opaque stock WMO geometry; no exact video, complete scene, game FPS or GPU timing claim.', cases=[])
for index, backend in enumerate(a.backend):
    run = subprocess.run([str(exe), str(backend.resolve()), str(fixture)], env=env,
                         cwd=out, capture_output=True, text=True, timeout=120)
    (out/f'gpu-{index}.log').write_text(run.stdout+run.stderr, encoding='utf-8')
    print(run.stdout, flush=True)
    assert run.returncode == 0 and 'RIGID_PEER_POINT_GPU_PASS' in run.stdout, run.stderr
    report['cases'].append(dict(backend=str(backend.resolve()), backend_sha256=hashlib.sha256(backend.read_bytes()).hexdigest(), result=run.stdout.strip()))
report['inputs_unchanged'] = all(sha(Path(path)) == digest for path, digest in inputs.items())
assert report['inputs_unchanged']
report['pass'] = True
(out/'validation.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
