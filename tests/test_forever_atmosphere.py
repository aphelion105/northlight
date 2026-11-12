# northlight-test: requires=cxx
"""Portable CPU tests; optionally validate the locally imported NFA1 dataset."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

with tempfile.TemporaryDirectory(prefix='forever-atmosphere-', dir=fp.out()) as temp:
    exe = Path(temp) / ('test.exe' if os.name == 'nt' else 'test')
    compiler = [str(fp.zig()), 'c++', '-target', 'x86-windows-gnu'] if os.name == 'nt' else ['clang++']
    subprocess.run([*compiler, '-std=c++17', '-O2', '-UNDEBUG', *fp.test_include_flags(),
                    str(fp.TESTS/'test_forever_atmosphere.cpp'), '-o', str(exe)], env=fp.zig_env() if os.name == 'nt' else None, check=True)
    subprocess.run([str(exe), *sys.argv[1:]], check=True)
