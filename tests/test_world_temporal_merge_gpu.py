# northlight-test: requires=windows-gpu,zig,client
"""Compare actual WorldRenderer history, fog and final images with/without fusion."""
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
from windows_gpu_guard import processes

out = fp.output_dir() / 'world-temporal-merge'
out.mkdir(parents=True, exist_ok=True)
sources = ['world_gi.cpp', 'world_mesh_plan.cpp', 'static_shadow_scene.cpp',
           'settings_overlay.cpp', 'settings_panel.cpp', 'imgui.cpp', 'imgui_draw.cpp',
           'imgui_tables.cpp', 'imgui_widgets.cpp', 'imgui_impl_dx9.cpp']
backends = [('dxvk', fp.client_root() / 'renderer-backends/dxvk/dxvk_d3d9.dll'),
            ('dxvk2', fp.client_root() / 'renderer-backends/dxvk2/dxvk2_d3d9.dll'),
            ('native', Path(os.environ['WINDIR']) / 'SysWOW64/d3d9.dll')]
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
inputs = {str(p): sha(p) for p in [Path(__file__), fp.TESTS / 'world_temporal_merge_gpu.cpp',
          fp.TESTS / 'world_removal_alias_gpu.cpp', fp.TESTS / 'world_screen_effects_gpu.cpp',
          *(p for p in (fp.REPO / 'src').rglob('*') if p.suffix in ('.cpp', '.h', '.inl')),
          *(p for _, p in backends)]}
report = {'pass': False, 'inputs': inputs, 'runs': [], 'comparisons': [],
          'scope': 'Actual renderer, authored moving receivers; no gameplay or timing claim.'}


def run(label, command, cwd):
    processes()
    result = subprocess.run(list(map(str, command)), cwd=cwd,
                            env={**fp.zig_env(), 'DISABLE_VULKAN_OBS_CAPTURE': '1',
                                 'DXVK_LOG_PATH': str(cwd)}, capture_output=True, text=True, timeout=300)
    (cwd / (label + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
    assert result.returncode == 0, (label, (result.stdout + result.stderr)[-2500:])
    return result.stdout


try:
    exe = out / 'temporal.exe'
    run('compile', [fp.zig(), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG', '-std=c++17',
                   '-Wno-microsoft-exception-spec', '-DIMGUI_USER_CONFIG="northlight_imconfig.h"',
                   *fp.test_include_flags(), '-I' + str(fp.TESTS), fp.TESTS / 'world_temporal_merge_gpu.cpp',
                   *(fp.src(s) for s in sources), '-luser32', '-luuid', '-o', exe], out)
    for name, backend in backends:
        for mode in ('reference', 'merged', 'zero-weight'):
            folder = out / name / mode
            folder.mkdir(parents=True, exist_ok=True)
            text = run('gpu', [exe, backend, folder, mode], folder)
            result = next(line for line in text.splitlines() if line.startswith('WORLD_TEMPORAL_MERGE_PASS'))
            report['runs'].append({'backend': name, 'mode': mode, 'result': result})
            print(name, result, flush=True)

        def images(mode):
            folder = out / name / mode
            paths = [p for p in folder.rglob('*.fgr') if not p.name.endswith('-removal.fgr')]
            result = {('diagnostics/' if 'northlight-diagnostics' in p.parts else '') + p.name: p for p in paths}
            assert len(result) == len(paths), 'ambiguous image identity'
            return result

        merged_images, optimized_images = images('merged'), images('zero-weight')
        assert merged_images.keys() == optimized_images.keys()
        assert all(path.read_bytes() == optimized_images[key].read_bytes() for key, path in merged_images.items()), 'zero-weight image drift'
        before, after = images('reference'), images('merged')
        assert before.keys() == after.keys() and len(before) >= 180
        differences = []
        for key, path in before.items():
            a, b = path.read_bytes(), after[key].read_bytes()
            assert a[:20] == b[:20] and len(a) == len(b), key
            if a == b:
                continue
            assert struct.unpack('<5I', a[:20])[3:] == (113, 8), key
            x, y = list(struct.iter_unpack('<e', a[20:])), list(struct.iter_unpack('<e', b[20:]))
            maximum = 0.
            for i, ((old,), (new,)) in enumerate(zip(x, y)):
                delta = abs(old - new)
                assert delta <= .002 + .001 * abs(old), (key, i, old, new)
                if i % 4 == 3:
                    assert a[20 + i*2:22 + i*2] == b[20 + i*2:22 + i*2], (key, 'alpha')
                maximum = max(maximum, delta)
            differences.append({'image': key, 'max_abs': maximum})
        report['comparisons'].append({'backend': name, 'images': len(before), 'rgb_differences': differences})
    assert all(sha(Path(p)) == digest for p, digest in inputs.items()), 'source drift'
    report['pass'] = True
finally:
    (out / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
print('WORLD_TEMPORAL_MERGE_GPU_PASS')
