#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig,windows-gpu manual
"""Production fog bytecode against analytic transport; hidden device, no game launch."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path[:0] = [str(Path(__file__).resolve().parents[1]), str(Path(__file__).resolve().parents[1] / 'scripts')]
import northlight_paths as fp
from windows_gpu_guard import processes

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--compile-only', action='store_true')
args = parser.parse_args()
if not args.compile_only: processes()
backend = Path(os.environ.get('NORTHLIGHT_DXVK_D3D9', fp.client_root() / 'renderer-backends/dxvk/dxvk_d3d9.dll'))
assert backend.is_file(), backend
with tempfile.TemporaryDirectory(prefix='atmospheric-fog-', dir=fp.out()) as temp:
    root = Path(temp)
    exe = root / 'test.exe'
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG', '-std=c++17',
                    *fp.test_include_flags(), str(fp.TESTS / 'atmospheric_fog_gpu.cpp'), '-luser32', '-o', str(exe)], env=fp.zig_env(), check=True)
    if args.compile_only:
        print('PASS atmospheric fog GPU harness compiled; GPU execution deferred')
    else:
        processes()
        subprocess.run([str(exe), str(fp.COMPILED), str(backend)], cwd=root, check=True, timeout=60)
