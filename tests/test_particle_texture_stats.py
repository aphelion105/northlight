#!/usr/bin/env python3
# northlight-test: requires=zig
"""Actual CPU-upload texture samples and strict bounded cache behavior."""
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

with tempfile.TemporaryDirectory(prefix='northlight-particle-texture-') as tmp:
    for flags in (['-O0'], ['-O2']):
        exe = Path(tmp)/('test.exe' if sys.platform == 'win32' else 'test')
        subprocess.run([str(fp.zig()), 'c++', '-std=c++17', *flags, '-Wall', '-Wextra', '-Werror',
                        *fp.test_include_flags(), str(fp.TESTS/'test_particle_texture_stats.cpp'), '-o', str(exe)],
                       check=True, env=fp.zig_env())
        subprocess.run([str(exe)], check=True)
