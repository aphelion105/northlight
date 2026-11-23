#!/usr/bin/env python3
# northlight-test: requires=client,stormlib,zig,windows-gpu manual
"""Exact real-client native single-light endpoints; no WoW or client writes."""
import argparse
from pathlib import Path
import os
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
import windows_gpu_guard
fp.use_source_modules()
from extract_m2_shader_families import collect
from extract_signatures import variants,fnv1a64
from world_scene_builder import Assets

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--backend',type=Path,action='append',help='D3D9 backend DLL. Defaults to system D3D9, DXVK2 and DXVK3.')
parser.add_argument('--family',action='append',help='Run the named shader fixture only.')
args=parser.parse_args()
if os.environ.get('NORTHLIGHT_LIGHTING_BENCH'):windows_gpu_guard.processes(allow_game=False)
out=fp.output_dir();out.mkdir(parents=True,exist_ok=True)
shaders=collect(fp.client_root())[0];vertex=shaders[0xe8d455338d8ae2ff][1]
with_assets=Assets()
try:
 pixel=next(variants(with_assets.read(r'shaders\pixel\ps_3_0\combiners_mod.bls')))[2]
 wmo=next(code for _,_,code in variants(with_assets.read(r'shaders\vertex\vs_3_0\mapobjdiffuse_t1.bls')) if fnv1a64(code)==0x22d86896259e7cb9)
 wmo_layered=next(code for _,_,code in variants(with_assets.read(r'shaders\vertex\vs_3_0\mapobjdiffuse_t1.bls')) if fnv1a64(code)==0x17f1cc42031600f9)
 wmo_pixel=next(variants(with_assets.read(r'shaders\pixel\ps_3_0\mapobjdiffuse.bls')))[2]
 wmo_specular=next(code for i,_,code in variants(with_assets.read(r'shaders\vertex\vs_3_0\mapobjspecular_t1.bls')) if i==9)
 wmo_specular_pixel=next(variants(with_assets.read(r'shaders\pixel\ps_3_0\mapobjspecular.bls')))[2]
 terrain=next(code for i,_,code in variants(with_assets.read(r'shaders\vertex\vs_3_0\terrain.bls')) if i==64)
 legacy_terrain=[next(code for i,_,code in variants(with_assets.read('shaders/vertex/'+model+'/terrain.bls')) if i==64) for model in ('vs_1_1','vs_2_0')]
 terrain_pixel=next(variants(with_assets.read(r'shaders\pixel\ps_3_0\terrain0.bls')))[2]
finally:with_assets.close()
exe=out/'native-local-light.exe'
cmd=[fp.zig(),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-DNORTHLIGHT_TEST_NATIVE_OWNER','-Wall','-Wextra',*fp.test_include_flags(),fp.TESTS/'native_local_light_gpu.cpp','-luser32','-o',exe]
with (out/'compile.log').open('w') as log:subprocess.run(list(map(str,cmd)),env=fp.zig_env(),stdout=log,stderr=subprocess.STDOUT,check=True,timeout=180)
backends=args.backend or (Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll',fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll',fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll')
failures=[]
for backend in backends:
 backend=backend.resolve()
 families=[('m2',vertex,pixel),('wmo',wmo,wmo_pixel),('wmo-layered',wmo_layered,wmo_pixel),('wmo-specular',wmo_specular,wmo_specular_pixel),*[(name,terrain,terrain_pixel) for name in ('terrain','terrain-neutral','terrain-coverage')],*[(f'terrain-sm{i+1}',code,terrain_pixel) for i,code in enumerate(legacy_terrain)]]
 if os.environ.get('NORTHLIGHT_CAPTURE_VERTEX'):
  families.append(('wmo-layered-foreign',Path(os.environ['NORTHLIGHT_CAPTURE_VERTEX']).read_bytes(),Path(os.environ['NORTHLIGHT_CAPTURE_PIXEL']).read_bytes()))
 if args.family:
  unknown=set(args.family)-{name for name,*_ in families}
  if unknown:parser.error('unknown shader fixtures: '+', '.join(sorted(unknown)))
 for family,vs,ps in families:
  if args.family and family not in args.family:continue
  if os.environ.get('NORTHLIGHT_NATIVE_LAYERED_ONLY') and not family.startswith('wmo-layered'):continue
  run=out/(backend.parent.name+'-'+family);run.mkdir(exist_ok=True)
  (run/'native-vs.bin').write_bytes(vs);(run/'native-ps.bin').write_bytes(ps)
  env={**fp.zig_env(),'DXVK_LOG_PATH':str(run),'DISABLE_VULKAN_OBS_CAPTURE':'1'}
  result=subprocess.run(list(map(str,[exe,backend,run/'native-vs.bin',run/'native-ps.bin',run,'wmo-layered' if family.startswith('wmo-layered') else 'wmo' if family.startswith('wmo') else family,fp.COMPILED])),env=env,cwd=run,capture_output=True,text=True,timeout=180)
  (run/'gpu.log').write_text(result.stdout+result.stderr);print(str(backend),family,result.stdout+result.stderr)
  if result.returncode:failures.append(f'{backend.parent.name}/{family}: {result.returncode}')
  elif family=='terrain' and 'TERRAIN_SHADOW_PROGRAMS_GPU_PASS' not in result.stdout:
   failures.append(f'{backend.parent.name}/{family}: shader owner check did not run')
if failures:sys.exit('Native light fixture failures: '+', '.join(failures))
