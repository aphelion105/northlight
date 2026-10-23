#!/usr/bin/env python3
# northlight-test: requires=zig
"""Current-frame spell source lifecycle and CPU-only upload preservation."""
from pathlib import Path
import subprocess,sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
with tempfile.TemporaryDirectory(prefix='particle-lights-') as tmp:
    for mode in ('O0','O2'):
        exe=Path(tmp)/('test.exe' if sys.platform=='win32' else 'test')
        subprocess.run([str(fp.zig()),'c++','-std=c++17','-'+mode,'-UNDEBUG','-Wall','-Wextra','-Werror',
            *fp.test_include_flags(),str(fp.TESTS/'test_particle_lights.cpp'),'-o',str(exe)],check=True,env=fp.zig_env())
        subprocess.run([str(exe)],check=True)
