#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig
"""Read-only 32-bit sensor smoke check on the system's actual D3D9Ex adapter."""
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

with tempfile.TemporaryDirectory(prefix='northlight-hardware-') as tmp:
    exe = Path(tmp) / 'hardware.exe'
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2',
                    '-UNDEBUG', '-Wall', '-Wextra', '-Werror', *fp.test_include_flags(),
                    str(fp.TESTS / 'performance_hardware.cpp'), '-luser32', '-luuid', '-o', str(exe)],
                   env=fp.zig_env(), check=True)
    subprocess.run([str(exe)], check=True, timeout=15)
