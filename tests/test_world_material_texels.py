# northlight-test: requires=windows-exe,zig
"""Repeated lamp textures cannot reject an otherwise valid local shadow scene."""
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

out = fp.output_dir()
out.mkdir(parents=True, exist_ok=True)
exe = out/'world-material-texels.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17',
                '-O2', '-UNDEBUG', *fp.test_include_flags(),
                str(fp.TESTS/'test_world_material_texels.cpp'), str(fp.src('world_gi.cpp')),
                '-o', str(exe)], env=fp.zig_env(), check=True)
subprocess.run([str(exe), str(out/'fixture')], check=True)
