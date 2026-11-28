#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig
"""Real Device target changes while the rain mask holds render target 1: the game's
SetRenderTarget and SetDepthStencilSurface unbind the mask first, and a render target 1
the game binds itself survives the next draw's unbind. Runs the system D3D9 and, when a
client is configured, its DXVK backends."""
import argparse,os,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]));import northlight_paths as fp
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,default=fp.output_dir()/'rain-mrt-target-change-gpu');p.add_argument('--backend',type=Path,action='append');a=p.parse_args()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);fp.use_source_modules()
sources=['world_gi.cpp','world_mesh_plan.cpp','static_shadow_scene.cpp','settings_overlay.cpp','settings_panel.cpp','imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp','imgui_impl_dx9.cpp'];exe=out/'rain-mrt-target-change.exe'
command=[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-DIMGUI_USER_CONFIG="northlight_imconfig.h"',*fp.test_include_flags(),fp.TESTS/'rain_mrt_target_change_gpu.cpp',*(fp.src(s) for s in sources),'-luser32','-luuid','-o',exe]
with (out/'compile.log').open('w') as f:subprocess.run(list(map(str,command)),env=fp.zig_env(),stdout=f,stderr=subprocess.STDOUT,check=True,timeout=600)
backends=a.backend
if not backends:
 backends=[Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll']
 try:client=fp.client_root()
 except fp.Missing:client=None
 if client:backends+=[b for b in (client/'renderer-backends/dxvk2/dxvk2_d3d9.dll',client/'renderer-backends/dxvk/dxvk_d3d9.dll') if b.is_file()]
failed=[]
for i,backend in enumerate(backends):
 run=out/str(i);run.mkdir(exist_ok=True);(run/'northlight-quality.ini').write_text('[Quality]\nDiagnostics=0\nEffects=0\nHDR=0\n')
 env={**os.environ,'NORTHLIGHT_QUALITY_FILE':str(run/'northlight-quality.ini'),'DXVK_LOG_PATH':str(run),'DISABLE_VULKAN_OBS_CAPTURE':'1'}
 r=subprocess.run([str(exe),str(backend.resolve()),str(run)+os.sep],cwd=run,env=env,capture_output=True,text=True,timeout=180);(run/'gpu.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,flush=True)
 ok=r.returncode==0 and ('RAIN_MRT_TARGET_CHANGE_PASS' in r.stdout or 'RAIN_MRT_TARGET_CHANGE_SKIP' in r.stdout)
 print(('PASS ' if ok else 'FAIL ')+f'{backend}: game target changes unbind the rain mask from render target 1',flush=True)
 if not ok:failed.append(str(backend))
sys.exit(1 if failed else 0)
