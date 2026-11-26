# northlight-test: requires=windows-gpu,zig
"""Run the Windows import/ABI oracle for x86 and x64 without launching WoW."""
from pathlib import Path
import subprocess
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

for arch in ('x86','x86_64'):
    exe=fp.output_dir()/f'dxvk_engine_name_test-{arch}.exe'
    subprocess.run([str(fp.zig()),'c++','-target',f'{arch}-windows-gnu','-std=c++17','-O2',
                    *fp.include_flags(),str(fp.TESTS/'test_dxvk_engine_name.cpp'),
                    '-o',str(exe)],env=fp.zig_env(),check=True)
    subprocess.run([str(exe)],check=True,timeout=30)
