# northlight-test: requires=zig
"""Authored planet/ring geometry and continent-aware celestial size policy."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

with tempfile.TemporaryDirectory(prefix='sky-planets-', dir=fp.output_dir()) as temp:
    exe = Path(temp)/('test.exe' if os.name == 'nt' else 'test')
    compiler = [str(fp.zig()), 'c++'] + (['-target', 'x86-windows-gnu'] if os.name == 'nt' else [])
    subprocess.run([*compiler, '-std=c++17', '-O2', '-UNDEBUG', *fp.test_include_flags(),
                    str(Path(__file__).with_suffix('.cpp')), '-o', str(exe)], env=fp.zig_env(), check=True)
    subprocess.run([str(exe)], check=True)
