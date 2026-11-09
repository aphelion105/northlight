#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Presentation history, percentile means, hitch accounting and consumer leases."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

with tempfile.TemporaryDirectory(prefix='northlight-monitor-') as tmp:
    compiler = [str(fp.zig()), 'c++', '-target', 'x86-windows-gnu'] if os.name == 'nt' else ['clang++']
    exe = Path(tmp) / 'monitor.exe'
    subprocess.run([*compiler, '-std=c++17', '-O2', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                    *fp.test_include_flags(), str(Path(__file__).with_suffix('.cpp')), '-o', str(exe)],
                   env=fp.zig_env(), check=True)
    subprocess.run([str(exe)], check=True)
