#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client manual
"""Real production Reset: preserve bounded immutable captures and point shaders."""
import argparse,hashlib,json,os,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]));import northlight_paths as fp
sys.path.insert(0,str(fp.TESTS));import client_fixtures
p=argparse.ArgumentParser(__doc__);p.add_argument('--output',type=Path,default=fp.output_dir()/'capture-reset-gpu');p.add_argument('--backend',type=Path,action='append');a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
fp.use_source_modules();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
inputs={str(p):sha(p) for folder in (fp.SRC,fp.SHADERS) for p in folder.rglob('*') if p.is_file()};inputs.update({str(p):sha(p) for p in (Path(__file__),fp.TESTS/'capture_reset_gpu.cpp')})
path,index,digest,*_=client_fixtures.PROGRAMS['OneBoneVs3'];(out/'model.bin').write_bytes(client_fixtures._read([(path,index,digest)])[path,index])
sources=['world_gi.cpp','world_mesh_plan.cpp','static_shadow_scene.cpp','settings_overlay.cpp','settings_panel.cpp','imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp','imgui_impl_dx9.cpp'];exe=out/'capture-reset.exe'
command=[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-DIMGUI_USER_CONFIG="northlight_imconfig.h"',*fp.test_include_flags(),fp.TESTS/'capture_reset_gpu.cpp',*(fp.src(s) for s in sources),'-luser32','-luuid','-o',exe]
with (out/'compile.log').open('w') as f:subprocess.run(list(map(str,command)),env=fp.zig_env(),stdout=f,stderr=subprocess.STDOUT,check=True,timeout=300)
backends=a.backend or [Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll',fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll',fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll'];runs=[]
for i,backend in enumerate(backends):
 run=out/str(i);run.mkdir();(run/'northlight-quality.ini').write_text('[Quality]\nDiagnostics=0\nEffects=0\nHDR=0\n');env={**os.environ,'NORTHLIGHT_QUALITY_FILE':str(run/'northlight-quality.ini'),'DXVK_LOG_PATH':str(run),'DISABLE_VULKAN_OBS_CAPTURE':'1'}
 r=subprocess.run([str(exe),str(backend.resolve()),str(run)+os.sep,str(out/'model.bin')],cwd=run,env=env,capture_output=True,text=True,timeout=180);(run/'gpu.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,flush=True);assert r.returncode==0,r.returncode;assert all(f'CAPTURE_RESET_GPU_PASS preserve={mode}' in r.stdout for mode in (0,1));runs.append({'backend':str(backend),'stdout':r.stdout})
assert all(sha(Path(p))==h for p,h in inputs.items()),'Source changed during Reset oracle'
(out/'validation.json').write_text(json.dumps({'pass':True,'inputs':inputs,'runs':runs,'scope':'Production Device::Reset with actual D3D9 resources; correctness and call counts only, no gameplay timing.'},indent=2)+'\n')
