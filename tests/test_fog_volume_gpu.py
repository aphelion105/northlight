#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client
"""Production volume atlas on native D3D9 and DXVK, without a game process."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
sys.path[:0] = [str(Path(__file__).resolve().parents[1]), str(Path(__file__).resolve().parents[1] / 'scripts')]
import northlight_paths as fp
fp.use_source_modules()
from windows_gpu_guard import processes

out = fp.output_dir()
exe = out / 'fog-volume-gpu.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                *fp.test_include_flags(), str(fp.TESTS / 'fog_volume_gpu.cpp'), '-luser32', '-o', str(exe)], env=fp.zig_env(), check=True)
results = []
for backend in (Path(os.environ['WINDIR']) / 'SysWOW64/d3d9.dll', fp.client_root() / 'renderer-backends/dxvk/dxvk_d3d9.dll'):
    processes(allow_game=False if os.environ.get('NORTHLIGHT_FOG_VOLUME_BENCHMARK') else None)
    result = subprocess.run([str(exe), str(backend)], cwd=out, capture_output=True, text=True, timeout=180)
    (out / (backend.parent.name + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
    print(result.stdout + result.stderr, flush=True)
    result.check_returncode()
    assert 'FOG_VOLUME_GPU_PASS' in result.stdout
    results.append({'backend': str(backend), 'sha256': hashlib.sha256(backend.read_bytes()).hexdigest(), 'stdout': result.stdout})
(out / 'validation.json').write_text(json.dumps({'pass': True, 'game_launched': False, 'results': results}, indent=2) + '\n')
