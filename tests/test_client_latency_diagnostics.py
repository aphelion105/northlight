#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Read-only client signature gates, invalid pointers and mismatched CVar state."""
from pathlib import Path
import subprocess,sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
with tempfile.TemporaryDirectory(prefix='client-latency-') as temp:
    exe=Path(temp)/'test'
    subprocess.run(['clang++','-std=c++17','-O2','-Wall','-Wextra','-Werror',
                    *fp.test_include_flags(),str(Path(__file__).with_suffix('.cpp')),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
