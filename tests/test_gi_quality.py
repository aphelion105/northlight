#!/usr/bin/env python3
# northlight-test: requires=zig slow
"""Default probe identity/threads: Windows x86 O2; Unix O2 and ASan/UBSan/TSan."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
from pathlib import Path
import subprocess,os
HERE=Path(__file__).resolve().parent
out=fp.output_dir()/'gi-quality';out.mkdir(parents=True,exist_ok=True)
compiler=[str(fp.zig()),'c++','-target','x86-windows-gnu'] if os.name=='nt' else ['clang++']
configurations=[('O2',['-O2'])] if os.name=='nt' else [('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined']),('tsan',['-O1','-g','-fsanitize=thread'])]
for label,flags in configurations:
    exe=out/('test-'+label+('.exe' if os.name=='nt' else ''))
    subprocess.run([*compiler,'-std=c++17','-UNDEBUG','-Wall','-Wextra','-Werror',*flags,*fp.test_include_flags(),str(HERE/'test_gi_quality.cpp'),str(fp.src('world_gi.cpp')),'-o',str(exe)],env=fp.zig_env(),check=True)
    print(label,subprocess.check_output([str(exe)],cwd=HERE,text=True),end='',flush=True)
