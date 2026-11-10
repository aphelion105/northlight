# northlight-test: requires=zig
"""Aurora orbital policy and production GI transport, without launching the game."""
from pathlib import Path
import os
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

out = fp.output_dir()
out.mkdir(parents=True, exist_ok=True)
exe = out / 'aurora-illumination.exe'
compiler = [str(fp.zig()), 'c++', '-target', 'x86-windows-gnu'] if os.name == 'nt' else ['clang++']
subprocess.run([*compiler, '-std=c++17',
                '-O2', '-UNDEBUG', '-Wall', '-Wextra', '-Werror', *fp.test_include_flags(),
                str(fp.TESTS / 'test_aurora_illumination.cpp'), str(fp.src('world_gi.cpp')),
                '-o', str(exe)], env=fp.zig_env(), check=True)
subprocess.run([str(exe)], check=True)
