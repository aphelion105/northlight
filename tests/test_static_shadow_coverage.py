#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Exact cached-volume decisions and production worker WMO/M2 cull flags."""
import subprocess
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
out=fp.output_dir();out.mkdir(parents=True,exist_ok=True)
world=fp.src('world_renderer.h').read_text();start=world.index('    void replayCullWork(ReplayCull& job){');end=world.index('    void replayCullKickSlot(',start)
(out/'replay_cull_work.inc').write_text(world[start:end])
for mode in ['O2','O0']:
    exe=out/(mode+'.exe')
    command=[str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-'+mode,'-UNDEBUG','-DNORTHLIGHT_CULL_WORK_INCLUDE="replay_cull_work.inc"','-I'+str(out),*fp.test_include_flags(),str(fp.TESTS/'test_static_shadow_coverage.cpp'),str(fp.src('world_gi.cpp')),'-o',str(exe)]
    subprocess.run(command,env=fp.zig_env(),check=True)
    r=subprocess.run([str(exe)]+(['--bench'] if mode=='O2' else []),capture_output=True,text=True)
    (out/(mode+'.log')).write_text(r.stdout+r.stderr)
    assert r.returncode==0 and 'STATIC_COVERAGE_EXACT_PASS' in r.stdout and 'STATIC_COVERAGE_WORKER_PASS' in r.stdout,r.stdout+r.stderr
    print(mode,r.stdout.strip(),flush=True)
