#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Conservative projected pose bounds, clip boundaries and current-epoch ownership."""
import subprocess
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
out=fp.output_dir();out.mkdir(parents=True,exist_ok=True)
for mode in ['O2','O0']:
    exe=out/(mode+'.exe')
    subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-'+mode,'-UNDEBUG',*fp.test_include_flags(),str(fp.TESTS/'pose_viewport_culling.cpp'),'-o',str(exe)],env=fp.zig_env(),check=True)
    result=subprocess.run([str(exe)],capture_output=True,text=True)
    (out/(mode+'.log')).write_text(result.stdout+result.stderr)
    assert result.returncode==0 and 'POSE_VIEWPORT_CULL_PASS' in result.stdout,result.stdout+result.stderr
    print(mode,result.stdout.strip(),flush=True)
