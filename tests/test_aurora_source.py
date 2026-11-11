# northlight-test: requires=zig
"""Aurora directional source contract; no game or device."""
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
out = fp.output_dir()
out.mkdir(parents=True, exist_ok=True)
exe = out / 'aurora-source.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17',
                '-O2', '-UNDEBUG', '-Wall', '-Wextra', '-Werror', *fp.test_include_flags(),
                str(fp.TESTS / 'test_aurora_source.cpp'), '-o', str(exe)], env=fp.zig_env(), check=True)
subprocess.run([str(exe)], check=True)
