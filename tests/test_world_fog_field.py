#!/usr/bin/env python3
# northlight-test: requires=zig,windows-exe
"""Regional fog upload, bindings and Reset on a device that records commands."""

from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

exe = fp.output_dir() / 'world-fog-field.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                '-Wall', '-Wextra', '-Werror', '-Wno-microsoft-exception-spec',
                *fp.test_include_flags(), str(fp.TESTS / 'test_world_fog_field.cpp'),
                '-o', str(exe)], env=fp.zig_env(), check=True)
run = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
assert 'WORLD_FOG_FIELD_PASS ' in run.stdout
print(run.stdout, end='')
