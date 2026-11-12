#!/usr/bin/env python3
# northlight-test: requires=client,stormlib,zig,windows-gpu manual
"""Exact real-client native single-light endpoints; no WoW or client writes."""
from pathlib import Path
import json
import os
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()

out=fp.output_dir();out.mkdir(parents=True,exist_ok=True)
exe=out/'native-local-displacement.exe'
cmd=[fp.zig(),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-Wall','-Wextra',*fp.test_include_flags(),fp.TESTS/'native_local_displacement_gpu.cpp','-luser32','-o',exe]
with (out/'compile.log').open('w') as log:subprocess.run(list(map(str,cmd)),env=fp.zig_env(),stdout=log,stderr=subprocess.STDOUT,check=True,timeout=180)
capture=Path(os.environ.get('NORTHLIGHT_TEST_CAPTURE_DIR','D:/Games/335_engb_mod/northlight-diagnostics/session-80560-393668750-0'))
for family,vs,ps in [('captured-m2',(capture/'capture-3-native-draw-1-original-vs.bin').read_bytes(),(capture/'capture-3-native-draw-1-ps.bin').read_bytes())]:
 run=out/family;run.mkdir(exist_ok=True)
 (run/'native-vs.bin').write_bytes(vs);(run/'native-ps.bin').write_bytes(ps)
 env={**fp.zig_env(),'DXVK_LOG_PATH':str(run),'DISABLE_VULKAN_OBS_CAPTURE':'1'}
 result=subprocess.run(list(map(str,[exe,fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll',run/'native-vs.bin',run/'native-ps.bin',run,family,fp.COMPILED])),env=env,cwd=run,capture_output=True,text=True,timeout=180)
 (run/'gpu.log').write_text(result.stdout+result.stderr);print(family,result.stdout+result.stderr);result.check_returncode()
