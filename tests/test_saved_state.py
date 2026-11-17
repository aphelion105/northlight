# northlight-test: requires=zig,windows-exe
"""Production pooled/unpooled state restoration, including incomplete initial capture."""
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

out = fp.output_dir()
out.mkdir(parents=True, exist_ok=True)
exe = out / 'saved-state.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG',
                '-std=c++17', *fp.test_include_flags(), str(fp.TESTS/'test_saved_state.cpp'),
                '-o', str(exe)], env=fp.zig_env(), check=True)
subprocess.run([str(exe)], check=True)
