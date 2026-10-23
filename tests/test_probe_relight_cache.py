#!/usr/bin/env python3
# northlight-test: requires=cxx
"""ProbeRelightCache release/configure (memory trim and map change); native CPU only, no game."""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
import subprocess
import tempfile

with tempfile.TemporaryDirectory(prefix='northlight-relight-cache-') as tmp:
    binary=Path(tmp)/'test'
    subprocess.run(['clang++','-std=c++17','-O1','-g','-UNDEBUG','-fsanitize=address,undefined','-fno-sanitize-recover=all',
                    *fp.test_include_flags(),str(Path(__file__).with_suffix('.cpp')),str(fp.src('world_gi.cpp')),
                    '-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
