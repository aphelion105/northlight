# northlight-test: requires=windows-gpu,zig manual
"""Actual Device HDR/AO-neutral producer, late fallback and borrowed bloom scratch."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,sys
ROOT=Path(__file__).resolve().parents[1];sys.path[:0]=[str(ROOT),str(ROOT/'tests')]
import northlight_paths as fp
from windows_gpu_guard import processes
p=argparse.ArgumentParser(__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--backend',type=Path,action='append',required=True);a=p.parse_args()
processes();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);overlay=out/'overlay';overlay.mkdir(exist_ok=True)
source=fp.src('renderer.cpp').read_text();marker='} else if(linearHdr&&fold)neutralAOPending=true;';assert source.count(marker)==1
inputs={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in [*fp.SRC.rglob('*.h'),*fp.SRC.rglob('*.inl'),*fp.SRC.rglob('*.cpp'),ROOT/'tests/neutral_ao_gpu.cpp',ROOT/'tests/world_screen_effects_gpu.cpp',Path(__file__).resolve()]}
source=source.replace('class Device final :','extern bool northlightForceNeutralAO;\nclass Device final :',1).replace(marker,'} else if(linearHdr&&fold&&!northlightForceNeutralAO)neutralAOPending=true;')
(overlay/'renderer.cpp').write_bytes(source.encode())
exe=out/'neutral-ao.exe';sources=['world_gi.cpp','world_mesh_plan.cpp','static_shadow_scene.cpp','settings_overlay.cpp','settings_panel.cpp','imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp','imgui_impl_dx9.cpp']
command=[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-DIMGUI_USER_CONFIG="northlight_imconfig.h"','-I',overlay,*fp.test_include_flags(),ROOT/'tests/neutral_ao_gpu.cpp',*[fp.src(s) for s in sources],'-luser32','-luuid','-o',exe]
env={**fp.zig_env(),'PYTHONUTF8':'1','DISABLE_VULKAN_OBS_CAPTURE':'1','DXVK_LOG_PATH':str(out)}
with (out/'compile.log').open('w') as log:subprocess.run(list(map(str,command)),env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=360)
results=[]
for i,backend in enumerate(a.backend):
    processes();folder=out/f'backend-{i}';folder.mkdir(exist_ok=True);(folder/'northlight-renderer.ini').write_bytes(b'[Renderer]\nSettingsOverlay=0\nExperimentalHDR=0\n')
    run=subprocess.run([str(exe),str(backend.resolve()),str(folder)],env=env,cwd=folder,capture_output=True,timeout=180)
    (folder/'gpu.log').write_bytes(run.stdout+run.stderr);print((run.stdout+run.stderr).decode(errors='replace')[-2500:],flush=True);run.check_returncode()
    assert b'NEUTRAL_AO_GPU_PASS' in run.stdout;results.append({'backend':str(backend.resolve()),'sha256':hashlib.sha256(backend.read_bytes()).hexdigest()})
assert all(hashlib.sha256(Path(f).read_bytes()).hexdigest()==h for f,h in inputs.items()),'Source changed during the oracle'
(out/'validation.json').write_text(json.dumps({'pass':True,'actual_device':True,'game_launched':False,'inputs':inputs,'command':list(map(str,command)),'results':results},indent=2),encoding='utf-8')
