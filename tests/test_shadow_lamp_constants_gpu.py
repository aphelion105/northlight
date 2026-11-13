# northlight-test: requires=windows-gpu,zig,client
"""Lamp batching must not change shadow strength in the production compositor."""
import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
import windows_gpu_guard

windows_gpu_guard.processes()
out = fp.output_dir()
out.mkdir(parents=True, exist_ok=True)
sources = ['world_gi.cpp', 'world_mesh_plan.cpp', 'static_shadow_scene.cpp',
           'settings_overlay.cpp', 'settings_panel.cpp', 'imgui.cpp', 'imgui_draw.cpp',
           'imgui_tables.cpp', 'imgui_widgets.cpp', 'imgui_impl_dx9.cpp']
exe = out/'lamp-constants.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG',
                '-std=c++17', '-Wno-microsoft-exception-spec', '-DIMGUI_USER_CONFIG="northlight_imconfig.h"',
                *fp.test_include_flags(), '-I'+str(fp.TESTS), str(fp.TESTS/'shadow_lamp_constants_gpu.cpp'),
                *(str(fp.src(s)) for s in sources), '-luser32', '-luuid', '-o', str(exe)],
               env=fp.zig_env(), check=True)
backend = Path(os.environ.get('NORTHLIGHT_TEST_D3D9', str(fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll')))
run = subprocess.run([str(exe), str(backend), str(out)], cwd=out, env=fp.zig_env(), capture_output=True, text=True)
(out/'gpu.log').write_text(run.stdout+run.stderr, encoding='utf-8')
print(run.stdout, end='')
run.check_returncode()
