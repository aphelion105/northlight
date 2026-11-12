#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client timing
"""Init-only point pipelines: real state/faults and exact 42-face cold/warm control.

Private snapshots avoid holding production headers open during compilation.
Timing/cycles describe this isolated workload, never concurrent-game FPS.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

parser = argparse.ArgumentParser(__doc__)
parser.add_argument('--output', type=Path, default=fp.output_dir() / 'point-pipeline-init')
parser.add_argument('--backend', type=Path, default=fp.client_root() / 'renderer-backends/dxvk/dxvk_d3d9.dll')
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
snapshot = out / 'frozen'
root = Path(__file__).resolve().parents[1]
for name in ['src', 'shaders']:
    shutil.copytree(root / name, snapshot / name)
(snapshot / 'tests').mkdir()
for name in ['point_pipeline_init_gpu.cpp', 'native_thread_probe.h']:
    shutil.copyfile(root / 'tests' / name, snapshot / 'tests' / name)
digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
inputs = {str(p.relative_to(snapshot)): digest(p) for p in snapshot.rglob('*') if p.is_file()}
assert all(digest(root / name) == value for name, value in inputs.items()), 'Source changed during snapshot'

def source(name):
    matches = list((snapshot / 'src').rglob(name))
    assert len(matches) == 1
    return str(matches[0])

units = ['world_gi.cpp', 'world_mesh_plan.cpp', 'static_shadow_scene.cpp', 'settings_overlay.cpp',
         'settings_panel.cpp', 'imgui.cpp', 'imgui_draw.cpp', 'imgui_tables.cpp', 'imgui_widgets.cpp', 'imgui_impl_dx9.cpp']
includes = sorted({p.parent for p in (snapshot / 'src').rglob('*.h')})
exe = out / 'point-init.exe'
command = [str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG', '-std=c++17',
           '-Wno-microsoft-exception-spec', '-DIMGUI_USER_CONFIG="northlight_imconfig.h"',
           *(arg for directory in includes for arg in ['-I', str(directory)]),
           str(snapshot / 'tests/point_pipeline_init_gpu.cpp'), *(source(name) for name in units),
           '-luser32', '-luuid', '-o', str(exe)]
with (out / 'compile.log').open('w') as log:
    subprocess.run(command, env=fp.zig_env(), stdout=log, stderr=subprocess.STDOUT, check=True, timeout=300)
for mode in [9, 0, 1]:
    run = out / str(mode)
    run.mkdir()
    env = os.environ.copy()
    env.update(DXVK_LOG_PATH=str(run), DISABLE_VULKAN_OBS_CAPTURE='1')
    for key, name in [('DXVK_SHADER_CACHE_PATH', 'shader-cache'), ('DXVK_STATE_CACHE_PATH', 'state-cache'),
                      ('__GL_SHADER_DISK_CACHE_PATH', 'driver-cache')]:
        directory = run / name
        directory.mkdir()
        env[key] = str(directory)
    result = subprocess.run([str(exe), str(args.backend.resolve()), str(run) + os.sep, str(mode), str(run / 'faces.bin')],
                            cwd=run, env=env, capture_output=True, text=True, timeout=90)
    text = result.stdout + result.stderr
    (run / 'run.log').write_text(text)
    print(mode, result.returncode, text[-3500:], flush=True)
    result.check_returncode()
    assert ('POINT_INIT_STATE_FAULTS_PASS' if mode == 9 else 'POINT_PIPELINE_PROTOTYPE') in text
assert (out / '0/faces.bin').read_bytes() == (out / '1/faces.bin').read_bytes()
assert all(digest(snapshot / name) == value for name, value in inputs.items())
(out / 'validation.json').write_text(json.dumps({'pass': True, 'inputs': inputs, 'command': command,
    'device_flags': '0x56', 'faces': 42, 'pixels_sha256': digest(out / '0/faces.bin'),
    'scope': 'Actual init helper; state/fault controls; recreated resources; exact first/repeat faces. No gameplay FPS claim.'}, indent=2))
print('PASS', out)
