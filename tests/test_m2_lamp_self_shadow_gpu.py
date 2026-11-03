#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client,stormlib
"""Production cube tests with real stock and active-mod lamps on D3D9 and DXVK."""
import os
from pathlib import Path
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
from test_m2_lamp_self_shadow import fixtures

def compile_gpu(out):
    exe=out/'lamp-gpu.exe'
    sources=['world_gi.cpp','world_mesh_plan.cpp','static_shadow_scene.cpp','settings_overlay.cpp','settings_panel.cpp','imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp','imgui_impl_dx9.cpp']
    cmd=[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-DIMGUI_USER_CONFIG="northlight_imconfig.h"',*fp.test_include_flags(),fp.TESTS/'m2_lamp_self_shadow_gpu.cpp',*(fp.src(s) for s in sources),'-luser32','-luuid','-o',exe]
    with (out/'compile.log').open('w') as log:subprocess.run(list(map(str,cmd)),check=True,env=fp.zig_env(),stdout=log,stderr=subprocess.STDOUT,timeout=300)
    return exe

def main():
    out=fp.output_dir();cases=fixtures(out/'fixtures');exe=compile_gpu(out)
    for i,backend in enumerate([Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll',fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll']):
        settings=out/f'settings-{i}';settings.mkdir(exist_ok=True)
        for case in cases:
            result=subprocess.run([str(exe),str(backend),str(settings)+os.sep,str(case),'fixture',str(out)],capture_output=True,text=True,env={**os.environ,'DXVK_LOG_PATH':str(out),'DISABLE_VULKAN_OBS_CAPTURE':'1'},timeout=180,cwd=out)
            (out/f'gpu-{i}-{case.name}.log').write_text(result.stdout+result.stderr)
            print(result.stdout+result.stderr,flush=True);assert result.returncode==0

if __name__=='__main__':main()
