#!/usr/bin/env python3
# northlight-test: requires=zig
"""ProbeRelightCache release/configure (memory trim and map change); native CPU only, no game."""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
import subprocess
import os

out=fp.output_dir()/'probe-relight-cache';out.mkdir(parents=True,exist_ok=True)
binary=out/('test.exe' if os.name=='nt' else 'test')
compiler=[str(fp.zig()),'c++','-target','x86-windows-gnu'] if os.name=='nt' else ['clang++']
flags=['-O2'] if os.name=='nt' else ['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all']
subprocess.run([*compiler,'-std=c++17','-UNDEBUG',*flags,
                *fp.test_include_flags(),str(Path(__file__).with_suffix('.cpp')),str(fp.src('world_gi.cpp')),
                '-o',str(binary)],env=fp.zig_env(),check=True)
subprocess.run([str(binary)],check=True)
