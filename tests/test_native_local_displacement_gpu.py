#!/usr/bin/env python3
# northlight-test: requires=client,stormlib,zig,windows-gpu manual
"""Exact real-client native single-light endpoints; no WoW or client writes."""
from pathlib import Path
import os
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
from scripts.capture_files import read_bytes
fp.use_source_modules()

capture_root=os.environ.get('NORTHLIGHT_TEST_CAPTURE_DIR')
if capture_root:
    family='captured-m2'
    capture=Path(capture_root)
    vertex=capture/'capture-3-native-draw-1-original-vs.bin'
    pixel=capture/'capture-3-native-draw-1-ps.bin'
    for shader in (vertex,pixel):
        if not shader.is_file():
            raise SystemExit(f'Missing captured M2 shader: {shader}')
    vs,ps=read_bytes(vertex),read_bytes(pixel)
else:
    from extract_m2_shader_families import collect
    from extract_signatures import variants
    from world_scene_builder import Assets

    family='m2'
    vs=collect(fp.client_root())[0][0xe8d455338d8ae2ff][1]
    assets=Assets()
    try:
        ps=next(variants(assets.read(r'shaders\pixel\ps_3_0\combiners_mod.bls')))[2]
    finally:
        assets.close()

out=fp.output_dir();out.mkdir(parents=True,exist_ok=True)
exe=out/'native-local-displacement.exe'
cmd=[fp.zig(),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-Wall','-Wextra',*fp.test_include_flags(),fp.TESTS/'native_local_displacement_gpu.cpp','-luser32','-o',exe]
with (out/'compile.log').open('w') as log:subprocess.run(list(map(str,cmd)),env=fp.zig_env(),stdout=log,stderr=subprocess.STDOUT,check=True,timeout=180)
run=out/family;run.mkdir(exist_ok=True)
(run/'native-vs.bin').write_bytes(vs);(run/'native-ps.bin').write_bytes(ps)
env={**fp.zig_env(),'DXVK_LOG_PATH':str(run),'DISABLE_VULKAN_OBS_CAPTURE':'1'}
result=subprocess.run(list(map(str,[exe,fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll',run/'native-vs.bin',run/'native-ps.bin',run,family,fp.COMPILED])),env=env,cwd=run,capture_output=True,text=True,timeout=180)
(run/'gpu.log').write_text(result.stdout+result.stderr);print(family,result.stdout+result.stderr);result.check_returncode()
