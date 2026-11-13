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
from extract_m2_shader_families import collect
from extract_signatures import variants,fnv1a64
from world_scene_builder import Assets

out=fp.output_dir();out.mkdir(parents=True,exist_ok=True)
shaders=collect(fp.client_root())[0];vertex=shaders[0xe8d455338d8ae2ff][1]
with_assets=Assets()
try:
 pixel=next(variants(with_assets.read(r'shaders\pixel\ps_3_0\combiners_mod.bls')))[2]
 wmo=next(code for _,_,code in variants(with_assets.read(r'shaders\vertex\vs_3_0\mapobjdiffuse_t1.bls')) if fnv1a64(code)==0x22d86896259e7cb9)
 wmo_pixel=next(variants(with_assets.read(r'shaders\pixel\ps_3_0\mapobjdiffuse.bls')))[2]
 wmo_specular=next(code for i,_,code in variants(with_assets.read(r'shaders\vertex\vs_3_0\mapobjspecular_t1.bls')) if i==9)
 wmo_specular_pixel=next(variants(with_assets.read(r'shaders\pixel\ps_3_0\mapobjspecular.bls')))[2]
 terrain=next(code for i,_,code in variants(with_assets.read(r'shaders\vertex\vs_3_0\terrain.bls')) if i==64)
 legacy_terrain=[next(code for i,_,code in variants(with_assets.read('shaders/vertex/'+model+'/terrain.bls')) if i==64) for model in ('vs_1_1','vs_2_0')]
 terrain_pixel=next(variants(with_assets.read(r'shaders\pixel\ps_3_0\terrain0.bls')))[2]
finally:with_assets.close()
exe=out/'native-local-light.exe'
world=fp.src('world_renderer.h').read_text()
begin=world.index('        auto ownedSubmit=[&]()->HRESULT{')
end=world.index('        for(unsigned k=0;k<4;++k)if((ownerMask',begin)
(out/'native_owner_submit.h').write_text('''struct NativeOwnerSubmitTest {
IDirect3DDevice9* d;bool failed=false,nativeExtraOwnership=false;unsigned nativeOwnedDraws=0,nativeOwnedSlots=0,nativeTerrainOwnedDraws=0;
explicit NativeOwnerSubmitTest(IDirect3DDevice9* device):d(device){}
template<class Draw> HRESULT draw(unsigned ownerMask,const float* ownedRGB,const float* originalRGB,Draw submit,bool terrainReceiver=false,const float* terrainOriginal=nullptr,const float* terrainOwned=nullptr,bool extraCanOwn=false){
'''+world[begin:end]+'''return ownedSubmit();}
};
''')
cmd=[fp.zig(),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-DNORTHLIGHT_TEST_NATIVE_OWNER','-I',out,'-Wall','-Wextra',*fp.test_include_flags(),fp.TESTS/'native_local_light_gpu.cpp','-luser32','-o',exe]
with (out/'compile.log').open('w') as log:subprocess.run(list(map(str,cmd)),env=fp.zig_env(),stdout=log,stderr=subprocess.STDOUT,check=True,timeout=180)
for backend in (Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll',fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll'):
 for family,vs,ps in [('m2',vertex,pixel),('wmo',wmo,wmo_pixel),('wmo-specular',wmo_specular,wmo_specular_pixel),*[(name,terrain,terrain_pixel) for name in ('terrain','terrain-neutral','terrain-coverage')],*[(f'terrain-sm{i+1}',code,terrain_pixel) for i,code in enumerate(legacy_terrain)]]:
  run=out/(family if backend.name=='dxvk_d3d9.dll' else 'native-'+family);run.mkdir(exist_ok=True)
  (run/'native-vs.bin').write_bytes(vs);(run/'native-ps.bin').write_bytes(ps)
  env={**fp.zig_env(),'DXVK_LOG_PATH':str(run),'DISABLE_VULKAN_OBS_CAPTURE':'1'}
  result=subprocess.run(list(map(str,[exe,backend,run/'native-vs.bin',run/'native-ps.bin',run,'wmo' if family.startswith('wmo') else family,fp.COMPILED])),env=env,cwd=run,capture_output=True,text=True,timeout=180)
  (run/'gpu.log').write_text(result.stdout+result.stderr);print(str(backend),family,result.stdout+result.stderr);result.check_returncode()
