#!/usr/bin/env python3
# northlight-test: requires=zig,windows-exe
"""Continuous indoor environment plus existing celestial and WMO decode contracts."""
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

out=fp.output_dir()/'world-environment'
out.mkdir(parents=True,exist_ok=True)
for source in ('test_world_environment.cpp','test_celestial_context.cpp','test_wmo_context.cpp'):
    exe=out/(Path(source).stem+'.exe')
    subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG',
                    *fp.test_include_flags(),str(fp.TESTS/source),'-o',str(exe)],env=fp.zig_env(),check=True)
    subprocess.run([str(exe)],check=True)
