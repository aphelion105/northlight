# northlight-test: requires=windows-gpu,zig,client
"""Actual pass selection, AO image parity and godray/medium separation; no game FPS claim."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
from windows_gpu_guard import processes

out = fp.output_dir() / 'celestial-controls'
out.mkdir(parents=True, exist_ok=False)
backend = Path(os.environ.get('NORTHLIGHT_TEST_D3D9', fp.client_root() / 'renderer-backends/dxvk/dxvk_d3d9.dll'))
sources = ['world_gi.cpp', 'world_mesh_plan.cpp', 'static_shadow_scene.cpp', 'settings_overlay.cpp', 'settings_panel.cpp',
           'imgui.cpp', 'imgui_draw.cpp', 'imgui_tables.cpp', 'imgui_widgets.cpp', 'imgui_impl_dx9.cpp']
env = {**fp.zig_env(), 'DISABLE_VULKAN_OBS_CAPTURE': '1', 'DXVK_LOG_PATH': str(out),
       'DXVK_CONFIG': 'd3d9.customVendorId = 1002; dxvk.allowFse = False; d3d9.presentInterval = 0'}
report = {'pass': False, 'game_fps_measured': False, 'runs': []}
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
inputs = {str(p): sha(p) for p in [*fp.SRC.rglob('*.h'), *fp.SRC.rglob('*.cpp'), *fp.SRC.rglob('*.inl'),
          fp.TESTS/'celestial_controls_gpu.cpp', fp.TESTS/'world_screen_effects_gpu.cpp', Path(__file__), backend]}

def run(label, command, cwd=out, extra=None):
    processes()
    result = subprocess.run(list(map(str, command)), cwd=cwd, env={**env, **(extra or {})}, capture_output=True, text=True, timeout=300)
    (out/(label+'.log')).write_text(result.stdout+result.stderr, encoding='utf-8')
    report['runs'].append({'label': label, 'exit_code': result.returncode})
    print(label, result.returncode, (result.stdout+result.stderr)[-2500:], flush=True)
    result.check_returncode()
    return result.stdout

try:
    exe = out/'celestial-controls.exe'
    run('compile', [fp.zig(), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG', '-std=c++17',
                   '-Wno-microsoft-exception-spec', '-DIMGUI_USER_CONFIG="northlight_imconfig.h"',
                   *fp.test_include_flags(), fp.TESTS/'celestial_controls_gpu.cpp',
                   *(fp.src(s) for s in sources), '-luser32', '-luuid', '-o', exe])
    for msaa in [0, 2]:
        settings = out/f'settings-{msaa}'
        settings.mkdir()
        text = run(f'gpu-{msaa}', [exe, backend, settings], settings, {'NORTHLIGHT_TEST_MSAA': str(msaa)})
        assert 'AO_BASE_PARITY_PASS' in text and 'GODRAYS_PASS' in text
    report['pass'] = True
finally:
    report['inputs_unchanged'] = all(sha(Path(p)) == value for p, value in inputs.items())
    report['pass'] &= report['inputs_unchanged']
    (out/'validation.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
assert report['pass']
