#!/usr/bin/env python3
# northlight-test: requires=zig
"""Compile the actual frame_requirements.h contract; no device, client or GPU."""
import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

output = fp.output_dir()
compiler = [str(fp.zig()), 'c++', '-target', 'x86-windows-gnu'] if os.name == 'nt' else [os.environ.get('CXX', 'clang++')]
for mode in ('O0', 'O2'):
    executable = output / ('frame-requirements-' + mode + ('.exe' if os.name == 'nt' else ''))
    subprocess.run([*compiler, '-' + mode, '-std=c++17', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                    *fp.test_include_flags(), str(Path(__file__).with_suffix('.cpp')), '-o', str(executable)],
                   env=fp.zig_env(), check=True)
    subprocess.run([str(executable)], check=True)
