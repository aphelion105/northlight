#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Exercise the AMD64 Tideglass material and wake inputs with controlled samples."""
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

exe = fp.output_dir() / 'tideglass-x64-inputs.exe'
command = [fp.zig(), 'c++', '-target', 'x86_64-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
           '-I', fp.REPO / 'src/water', '-I', fp.REPO / 'tideglass/src',
           fp.TESTS / 'test_tideglass_x64_inputs.cpp', '-o', exe]
subprocess.run(list(map(str, command)), env=fp.zig_env(), check=True)
subprocess.run([str(exe)], check=True, timeout=30)
