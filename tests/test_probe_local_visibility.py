# northlight-test: requires=zig,windows-exe
"""Geometry visibility prototype: sealed/thin/slanted walls, door, floor, cache and moving blockers."""
from pathlib import Path
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

out=fp.output_dir()/'probe-local-visibility'
out.mkdir(parents=True,exist_ok=True)
exe=out/'visibility.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG',
                *fp.test_include_flags(),str(Path(__file__).with_suffix('.cpp')),str(fp.src('world_gi.cpp')),
                '-o',str(exe)],env=fp.zig_env(),check=True)
subprocess.run([str(exe)],check=True)
