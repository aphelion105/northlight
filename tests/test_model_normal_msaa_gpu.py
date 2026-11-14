# northlight-test: requires=windows-gpu,zig,client manual
"""DXVK sample-zero normal resolve. Does not run the game or claim game FPS."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import northlight_paths as fp
import windows_gpu_guard

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--backend', type=Path, action='append', required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
out = args.output.resolve(); out.mkdir(parents=True, exist_ok=False)
env = {**fp.zig_env(), 'DXVK_LOG_PATH': str(out), 'DISABLE_VULKAN_OBS_CAPTURE': '1'}
census = windows_gpu_guard.processes()

def run(name, command):
    result = subprocess.run(list(map(str, command)), cwd=out, env=env, capture_output=True, text=True, errors='replace', timeout=300)
    (out / (name + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
    if result.returncode: print(result.stdout[-2000:] + result.stderr[-2000:], flush=True)
    result.check_returncode()
    return result.stdout

exe = out / 'msaa-normals.exe'
run('compile', [fp.zig(), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG', '-Wno-microsoft-exception-spec', '-Wno-return-type', *fp.include_flags(), fp.TESTS / 'model_normal_msaa_gpu.cpp', fp.src('world_gi.cpp'), '-luser32', '-o', exe])
report = {'scope': __doc__, 'processes': census, 'backends': []}
for i, backend in enumerate(args.backend):
    backend = backend.resolve(); output = run('gpu-' + str(i), [exe, backend])
    assert 'MODEL_NORMAL_MSAA_RESOLVE_PASS' in output
    print(output, flush=True)
    report['backends'].append({'path': str(backend), 'sha256': hashlib.sha256(backend.read_bytes()).hexdigest()})
report['pass'] = True
report['inputs'] = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in [Path(__file__), fp.TESTS / 'model_normal_msaa_gpu.cpp', fp.TESTS / 'model_normals_gpu.cpp', *[p for p in fp.SRC.rglob('*') if p.is_file()]]}
report['commit'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
report['environment'] = {key: env.get(key) for key in ('DXVK_CONFIG', 'DXVK_FILTER_DEVICE_NAME', 'NORTHLIGHT_TEST_ALLOW_RUNNING_GAME')}
(out / 'validation.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
