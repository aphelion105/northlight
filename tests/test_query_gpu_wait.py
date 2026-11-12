#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Native x86 optional GPU wait lifetime, timeout, error and queue-lock protocol."""
from pathlib import Path
import subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]));import northlight_paths as fp
out=fp.REPO/'out/query-gpu-wait-unit';out.mkdir(parents=True,exist_ok=True)
for opt in ('-O2','-O0'):
 exe=out/('test'+opt+'.exe')
 subprocess.run(list(map(str,[fp.zig(),'c++','-target','x86-windows-gnu','-std=c++17',opt,'-Wall','-Wextra','-Werror',*fp.test_include_flags(),fp.TESTS/'test_query_gpu_wait.cpp','-o',exe])),env=fp.zig_env(),check=True)
 subprocess.run([str(exe)],check=True)
