# northlight-test: requires=windows-gpu,zig,client manual timing
"""Paired production terrain upload and pixel oracle, with a frozen pre-change header."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT),str(ROOT/'tests')]
import northlight_paths as fp
from windows_gpu_guard import processes

p=argparse.ArgumentParser(__doc__)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--baseline-header',type=Path,required=True)
p.add_argument('--backend',type=Path,action='append',required=True)
p.add_argument('--timing',action='store_true')
a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
inputs={str(f):sha(f) for f in [*fp.SRC.rglob('*.h'),*fp.SRC.rglob('*.cpp'),*fp.SRC.rglob('*.inl'),Path(__file__),ROOT/'tests/live_terrain_order_gpu.cpp',a.baseline_header.resolve(),*a.backend]}
sources=['world_gi.cpp','world_mesh_plan.cpp','static_shadow_scene.cpp','settings_overlay.cpp','settings_panel.cpp','imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp','imgui_impl_dx9.cpp']
env={**fp.zig_env(),'DISABLE_VULKAN_OBS_CAPTURE':'1','DXVK_LOG_PATH':str(out)}
games=processes();executables={};results=[]
for label in ['baseline','current']:
    exe=out/(label+'.exe');includes=['-I'+str(a.baseline_header.resolve().parent)] if label=='baseline' else []
    command=[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-DIMGUI_USER_CONFIG="northlight_imconfig.h"',*includes,*fp.test_include_flags(),ROOT/'tests/live_terrain_order_gpu.cpp',*(fp.src(s) for s in sources),'-luser32','-luuid','-o',exe]
    with (out/(label+'-compile.log')).open('w') as log:subprocess.run(list(map(str,command)),env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300)
    executables[label]=exe
for index,backend in enumerate(a.backend):
    images={}
    for label,exe in executables.items():
        name=f'{index}-{label}';settings=out/name;settings.mkdir();image=out/(name+'.bin');processes()
        r=subprocess.run([str(exe),str(backend.resolve()),str(settings)+os.sep,str(image),'correctness'],capture_output=True,env=env,timeout=180)
        (out/(name+'.log')).write_bytes(r.stdout+r.stderr);print(name,(r.stdout+r.stderr).decode(errors='replace')[-600:],flush=True);r.check_returncode()
        assert b'TERRAIN_ORDER_GPU_PASS comparisons=224' in r.stdout
        images[label]=image
    assert images['baseline'].read_bytes()==images['current'].read_bytes(),'Terrain depth or point bounds differ'
    result=dict(backend=str(backend.resolve()),backend_sha256=sha(backend),pixel_comparisons=224,pixel_sha256=sha(images['current']))
    if a.timing:
        result['timing']=[]
        for repeat in range(2):
            for label in (['baseline','current'] if repeat==0 else ['current','baseline']):
                name=f'{index}-{label}-timing-{repeat}';settings=out/name;settings.mkdir();processes()
                r=subprocess.run([str(executables[label]),str(backend.resolve()),str(settings)+os.sep,str(out/'unused.bin'),'timing'],capture_output=True,env=env,timeout=120)
                (out/(name+'.log')).write_bytes(r.stdout+r.stderr);print(name,(r.stdout+r.stderr).decode(errors='replace')[-450:],flush=True);r.check_returncode()
                lines=[x for x in r.stdout.decode().splitlines() if x.startswith('TERRAIN_ORDER_BENCH')];assert len(lines)==3
                for line in lines:
                    result['timing'].append(dict(label=label,repeat=repeat,**dict(re.findall(r'(\w+)=([\d.]+)',line))))
                    if label=='current' and 'mode=2' not in line:assert 'uploadBytes=0 reused=72' in line
    results.append(result)
assert all(sha(Path(f))==h for f,h in inputs.items()),'Sources changed during oracle'
(out/'validation.json').write_bytes(json.dumps(dict(pass_=True,inputs=inputs,concurrent_games=games,results=results,scope='Production live terrain capture+upload CPU only; no gameplay FPS or whole-frame GPU timing claim.'),indent=2).encode())
