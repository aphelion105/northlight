#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client manual
"""Check selective displacement sampler writes and restoration after device failures."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

files = [p for p in fp.SRC.rglob('*') if p.is_file()]
files += [Path(__file__), fp.TESTS / 'displacement_sampler_reuse.cpp']
inputs = {str(p): sha(p) for p in files}
exe = out / 'displacement-sampler.exe'
command = list(map(str, [fp.zig(), 'c++', '-target', 'x86-windows-gnu', '-O2',
                       '-UNDEBUG', '-std=c++17', *fp.test_include_flags(),
                       fp.TESTS / 'displacement_sampler_reuse.cpp', '-luser32', '-o', exe]))
with (out / 'compile.log').open('w', encoding='utf-8') as log:
    subprocess.run(command, env=fp.zig_env(), stdout=log, stderr=subprocess.STDOUT, check=True)

backends = [Path(os.environ['WINDIR']) / 'SysWOW64/d3d9.dll',
            fp.client_root() / 'renderer-backends/dxvk2/dxvk2_d3d9.dll',
            fp.client_root() / 'renderer-backends/dxvk/dxvk_d3d9.dll']
runs = []
for index, dll in enumerate(backends):
    run = out / str(index)
    run.mkdir()
    env = {**fp.zig_env(), 'DXVK_LOG_PATH': str(run), 'DISABLE_VULKAN_OBS_CAPTURE': '1'}
    result = subprocess.run([str(exe), str(dll)], cwd=run, env=env,
                            capture_output=True, text=True, timeout=90)
    (run / 'gpu.log').write_text(result.stdout + result.stderr, encoding='utf-8', newline='\n')
    print(index, result.stdout + result.stderr, flush=True)
    result.check_returncode()
    assert 'DISPLACEMENT_SAMPLER_PASS cases=320 masks=64 normalSetters=384 ' in result.stdout
    runs.append({'backend': str(dll), 'backend_sha256': sha(dll), 'stdout': result.stdout})

assert all(sha(Path(p)) == digest for p, digest in inputs.items())
(out / 'validation.json').write_text(json.dumps({
    'pass': True, 'inputs': inputs, 'compile_command': command,
    'exe_sha256': sha(exe), 'runs': runs, 'game_launched': False,
    'scope': 'Isolated device state correctness and counted sampler writes; no gameplay FPS claim.'
}, indent=2) + '\n', encoding='utf-8', newline='\n')
