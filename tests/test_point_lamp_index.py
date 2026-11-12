#!/usr/bin/env python3
# northlight-test: requires=zig,windows-exe
"""Exact fixture ownership and bounded warm lookup work; CPU component timings only."""
import json
import re
import statistics
import subprocess
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

out=fp.output_dir();out.mkdir(parents=True,exist_ok=True)
exe=out/'point_lamp_index.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG',
                *fp.include_flags(),str(fp.TESTS/'point_lamp_index.cpp'),'-o',str(exe)],env=fp.zig_env(),check=True,timeout=180)
result=subprocess.run([str(exe)],capture_output=True,text=True,check=True,timeout=90)
print(result.stdout,end='');(out/'run.log').write_text(result.stdout)
assert 'POINT_LAMP_INDEX_PASS ' in result.stdout
samples=re.findall(r'linearMs=([\d.]+) indexedMs=([\d.]+) buildMs=([\d.]+)',result.stdout);assert len(samples)==6
linear,indexed,build=(statistics.median(float(row[i]) for row in samples) for i in (0,1,2))
(out/'timing.json').write_text(json.dumps({'linear_ms':linear,'indexed_ms':indexed,'build_ms':build,
    'scope':'Warm fixture ownership lookup for 64 lights and 16385 batches; excludes rendering and index construction.'},indent=2)+'\n')
print(f'PASS median lookup: {linear:.6f} -> {indexed:.6f} ms; index build: {build:.6f} ms; component only')
