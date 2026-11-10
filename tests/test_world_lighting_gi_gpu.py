# northlight-test: requires=windows-gpu,zig,client timing
"""Compare merged unshadowed lighting/GI with separate production shaders."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import northlight_paths as fp

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, default=fp.output_dir() / 'world-lighting-gi')
parser.add_argument('--backend', type=Path, action='append')
parser.add_argument('--reference', type=Path, default=fp.COMPILED)
parser.add_argument('--timing', action='store_true')
args = parser.parse_args()
out = args.output.resolve();out.mkdir(parents=True, exist_ok=False)
backends = args.backend or [Path(os.environ['WINDIR']) / 'SysWOW64/d3d9.dll',
    fp.client_root() / 'renderer-backends/dxvk2/dxvk2_d3d9.dll',
    fp.client_root() / 'renderer-backends/dxvk/dxvk_d3d9.dll']
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
inputs = [Path(__file__), fp.TESTS / 'world_lighting_gi_gpu.cpp', fp.TESTS / 'composite_distance_gpu.cpp',
          args.reference / 'WorldLightingUnshadowed.bin', args.reference / 'WorldGI.bin',
          fp.COMPILED / 'WorldLightingUnshadowedGI.bin', *backends]
hashes = {str(p): sha(p) for p in inputs}
exe = out / 'lighting-gi.exe'
command = [fp.zig(), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG', '-std=c++17',
           *fp.test_include_flags(), fp.TESTS / 'world_lighting_gi_gpu.cpp', '-luser32', '-o', exe]
with (out / 'compile.log').open('w') as log:
    subprocess.run(list(map(str, command)), env=fp.zig_env(), stdout=log, stderr=subprocess.STDOUT, check=True)
for index, backend in enumerate(backends):
    for timing in ([0, 1] if args.timing else [0]):
        run = out / f'{index}-{timing}';run.mkdir()
        result = subprocess.run([str(exe), str(backend.resolve()), str(args.reference.resolve()), str(fp.COMPILED), str(timing)],
            cwd=run, env={**os.environ, 'DXVK_LOG_PATH': str(run), 'DISABLE_VULKAN_OBS_CAPTURE': '1'},
            capture_output=True, text=True, timeout=120)
        (run / 'run.log').write_text(result.stdout + result.stderr)
        print(index, timing, result.returncode, result.stdout[-1800:], flush=True)
        result.check_returncode();assert 'FUSED_GI_PASS' in result.stdout
assert all(sha(Path(p)) == value for p, value in hashes.items())
(out / 'validation.json').write_text(json.dumps({'pass': True, 'inputs': hashes,
    'scope': 'Separate vs merged production shaders; authored FP16 targets; not total Northlight/game FPS.',
    'timing': args.timing}, indent=2) + '\n')
