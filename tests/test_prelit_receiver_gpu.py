# northlight-test: requires=windows-gpu,zig,client,stormlib
"""Edited interior shaders retain their mask and colour under directional removal."""
import json
import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
import windows_gpu_guard

fp.use_source_modules()
from world_scene_builder import Assets
from extract_signatures import variants

windows_gpu_guard.processes(allow_game=True)  # Image assertions only, no timing claims.
out = fp.output_dir()
assets = Assets(archives='stock')
try:
    programs = {i: code for i, _, code in variants(assets.read('shaders/vertex/vs_3_0/MapObjDiffuse_T1.bls'))}
finally:
    assets.close()
for name, index in [('prelit', 0), ('lit', 1)]:
    (out/(name+'.bin')).write_bytes(programs[index])
sources = ['world_gi.cpp', 'world_mesh_plan.cpp', 'static_shadow_scene.cpp',
           'settings_overlay.cpp', 'settings_panel.cpp', 'imgui.cpp', 'imgui_draw.cpp',
           'imgui_tables.cpp', 'imgui_widgets.cpp', 'imgui_impl_dx9.cpp']
exe = out/'prelit-receiver.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG',
                '-std=c++17', '-Wno-microsoft-exception-spec', '-DIMGUI_USER_CONFIG="northlight_imconfig.h"',
                *fp.test_include_flags(), '-I'+str(fp.TESTS), str(fp.TESTS/'prelit_receiver_gpu.cpp'),
                *(str(fp.src(s)) for s in sources), '-luser32', '-luuid', '-o', str(exe)],
               env=fp.zig_env(), check=True)
runs = []
for index, backend in enumerate([Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll',
                                fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll',
                                fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll']):
    folder = out/str(index); folder.mkdir(exist_ok=True)
    (folder/'northlight-renderer.ini').write_text('[Renderer]\nSettingsOverlay=0\nExperimentalHDR=0\n')
    run = subprocess.run([str(exe), str(backend), str(folder), str(out)], cwd=folder,
                         env={**fp.zig_env(), 'DISABLE_VULKAN_OBS_CAPTURE': '1', 'DXVK_LOG_PATH': str(folder)},
                         capture_output=True, text=True, timeout=180)
    (folder/'gpu.log').write_text(run.stdout+run.stderr)
    print(run.stdout+run.stderr, flush=True); run.check_returncode()
    assert 'PRELIT_RECEIVER_GPU_PASS' in run.stdout
    runs.append(dict(backend=str(backend), output=run.stdout))
(out/'validation.json').write_text(json.dumps(dict(pass_=True, runs=runs,
    scope='Real shader registration and authored colour/shadow endpoints through WorldRenderer; not a replay of the supplied screenshots.'), indent=2)+'\n')
