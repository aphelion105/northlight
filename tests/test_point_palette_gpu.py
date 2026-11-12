# northlight-test: requires=windows-gpu,zig,client manual timing
"""Production point-cube palette instancing against individual shader replay."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1];sys.path[:0]=[str(ROOT),str(ROOT/'tests')]
import northlight_paths as fp
import client_fixtures
from windows_gpu_guard import processes
p=argparse.ArgumentParser(__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--backend',type=Path,action='append',required=True);p.add_argument('--timing',action='store_true');a=p.parse_args()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);client_fixtures.actor_client_programs(out)
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
inputs={str(f):sha(f) for f in [*fp.SRC.rglob('*.h'),*fp.SRC.rglob('*.cpp'),*fp.SRC.rglob('*.inl'),ROOT/'tests/point_palette_gpu.cpp',Path(__file__),out/'actor_client_programs.h',*a.backend]}
env={**fp.zig_env(),'DXVK_LOG_PATH':str(out),'DISABLE_VULKAN_OBS_CAPTURE':'1'}
sources=['world_gi.cpp','world_mesh_plan.cpp','static_shadow_scene.cpp','settings_overlay.cpp','settings_panel.cpp','imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp','imgui_impl_dx9.cpp'];exe=out/'palette.exe'
command=[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-DIMGUI_USER_CONFIG="northlight_imconfig.h"','-I'+str(out),*fp.test_include_flags(),ROOT/'tests/point_palette_gpu.cpp',*(fp.src(s) for s in sources),'-luser32','-luuid','-o',exe]
with (out/'compile.log').open('w') as log:subprocess.run(list(map(str,command)),env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300)
results=[]
for index,backend in enumerate(a.backend):
    settings=out/str(index);settings.mkdir();games=[g for g in processes() if g['name'].lower().startswith('wow')]
    r=subprocess.run([str(exe),str(backend.resolve()),str(settings)+os.sep,*(['timing'] if a.timing else [])],env=env,capture_output=True,timeout=240)
    log=out/f'gpu-{index}.log';log.write_bytes(r.stdout+r.stderr);print(index,(r.stdout+r.stderr).decode(errors='replace')[-2400:],flush=True);r.check_returncode();assert b'POINT_PALETTE_GPU_PASS' in r.stdout
    rows=[dict(re.findall(r'(\w+)=([\d.]+)',line)) for line in r.stdout.decode().splitlines() if line.startswith('POINT_PALETTE_TIMING ')]
    results.append(dict(backend=str(backend.resolve()),sha256=sha(backend),concurrent_games=games,log=log.name,timing=rows))
assert all(sha(Path(f))==h for f,h in inputs.items()),'Source changed during oracle'
(out/'validation.json').write_bytes(json.dumps(dict(pass_=True,inputs=inputs,results=results,scope='Actual production renderPointShadow, including adjacent-run admission and palette packing, original capture packets and source-envelope proof. Component CPU submission/GPU timestamps/serialized completion only; no gameplay FPS or whole-renderer budget claim.'),indent=2).encode())
