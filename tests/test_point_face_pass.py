#!/usr/bin/env python3
# northlight-test: requires=zig,windows-exe
"""Verify point face commands on a recording D3D9 device. No GPU or game."""
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

out = fp.output_dir()
exe = out / 'point-face-pass.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                '-Wall', '-Wextra', '-Werror', *fp.test_include_flags(), str(fp.TESTS / 'test_point_face_pass.cpp'),
                '-o', str(exe)], env=fp.zig_env(), check=True)
run = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
assert 'POINT_FACE_PASS ' in run.stdout
assert 'POINT_OPAQUE_FACE_PASS ' in run.stdout
print(run.stdout, end='')
