"""Compare actual multiframe WorldRenderer outputs with dedicated removal scratch."""
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
from windows_gpu_guard import processes

parser = argparse.ArgumentParser(__doc__)
parser.add_argument('--reference', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--allow-game-process', action='store_true')
parser.add_argument('--compare-only', action='store_true', help='Recheck existing outputs and their original input hashes without rerunning GPU work.')
args = parser.parse_args()
if not args.allow_game_process:
    processes()
out = args.output.resolve()
assert not fp.is_client(out)
out.mkdir(parents=True, exist_ok=True)
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
sources = ['world_gi.cpp','world_mesh_plan.cpp','static_shadow_scene.cpp','settings_overlay.cpp','settings_panel.cpp',
           'imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp','imgui_impl_dx9.cpp']
inputs = [fp.src('world_renderer.h'), fp.TESTS/'world_removal_alias_gpu.cpp', fp.TESTS/'world_screen_effects_gpu.cpp',
          args.reference.resolve(), fp.src('world_compiled_shaders.h'), *(fp.src(s) for s in sources)]
report = {'pass': False, 'inputs': {str(p): sha(p) for p in inputs}, 'runs': [], 'game_control': False, 'timings_measured': False}
if args.compare_only:
    report = json.loads((out/'validation.json').read_text())
    assert len(report['runs']) == 4 and all(run['exit_code'] == 0 for run in report['runs'])
env = dict(os.environ, DXVK_LOG_PATH=str(out))
try:
    for variant, header in ([] if args.compare_only else [('dedicated', args.reference.resolve()), ('alias', fp.src('world_renderer.h'))]):
        folder = out/variant
        folder.mkdir(exist_ok=True)
        include = folder/'include'
        include.mkdir(exist_ok=True)
        shutil.copyfile(header, include/'world_renderer.h')
        exe = folder/'world_removal_alias_gpu.exe'
        command = [str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG', '-std=c++17',
                   '-Wno-microsoft-exception-spec', '-DIMGUI_USER_CONFIG="northlight_imconfig.h"', '-I', str(include),
                   *fp.test_include_flags(), '-I'+str(fp.TESTS), str(fp.TESTS/'world_removal_alias_gpu.cpp'),
                   *(str(fp.src(s)) for s in sources), '-luser32', '-luuid', '-o', str(exe)]
        result = subprocess.run(command, env=fp.zig_env(), capture_output=True, text=True, timeout=240)
        (folder/'compile.log').write_text(result.stdout+result.stderr, encoding='utf-8')
        assert result.returncode == 0, result.stderr[-5000:]
        print('compiled', variant, flush=True)
        for backend, name in [('dxvk', 'dxvk_d3d9.dll'), ('dxvk2', 'dxvk2_d3d9.dll')]:
            run = folder/backend
            run.mkdir(exist_ok=True)
            dll = fp.client_root()/'renderer-backends'/backend/name
            result = subprocess.run([str(exe), str(dll), str(run), variant], cwd=run, env=env, capture_output=True, text=True, timeout=180)
            (run/'run.log').write_text(result.stdout+result.stderr, encoding='utf-8')
            report['runs'].append({'variant': variant, 'backend': backend, 'exit_code': result.returncode,
                                   'backend_sha256': sha(dll), 'exe_sha256': sha(exe), 'tail': result.stdout[-1000:]+result.stderr[-1000:]})
            print(variant, backend, result.returncode, result.stdout[-500:]+result.stderr[-500:], flush=True)
            assert result.returncode == 0
    report['comparisons'] = []
    for backend in ('dxvk', 'dxvk2'):
        reference = out/'dedicated'/backend
        candidate = out/'alias'/backend
        # Diagnostic session directory names contain process/time identities.
        # Product names and frame numbers are the stable comparison contract.
        def images(folder):
            paths = list(folder.rglob('*.fgr'))
            result = {('diagnostics/' if 'northlight-diagnostics' in p.parts else '')+p.name: sha(p) for p in paths}
            assert len(result) == len(paths), 'duplicate diagnostic product'
            return result
        a, b = images(reference), images(candidate)
        assert len(a) >= 200, len(a)
        differences = [p for p in a.keys()|b.keys() if a.get(p) != b.get(p)]
        report['comparisons'].append({'backend': backend, 'images': len(a), 'differences': differences})
        assert not differences, differences[:20]
    report['inputs_unchanged'] = all(sha(Path(p)) == value for p, value in report['inputs'].items())
    assert report['inputs_unchanged']
    report['pass'] = True
finally:
    (out/'validation.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
print('WORLD_REMOVAL_ALIAS_BYTE_EXACT_PASS', report['comparisons'])
