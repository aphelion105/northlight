# northlight-test: requires=windows-gpu,zig,client manual timing
"""Actual stock M2 captures: fixed warm workload across pressure/recovery, paired GPU oracle."""
from pathlib import Path
import argparse,hashlib,json,os,re,shutil,subprocess,sys
ROOT=Path(__file__).resolve().parents[1];sys.path[:0]=[str(ROOT),str(ROOT/'tests')]
import northlight_paths as fp
fp.use_source_modules()
import client_fixtures
from world_scene_builder import Assets
from extract_signatures import variants
from windows_gpu_guard import processes
sys.path.insert(0,str(ROOT/'scripts/shaders'))
from shader_compiler import Compiler,validate_assembly
p=argparse.ArgumentParser(__doc__)
p.add_argument('--output',type=Path,required=True);p.add_argument('--backend',type=Path,action='append',required=True);p.add_argument('--benchmark',action='store_true');p.add_argument('--negative-control',action='store_true')
a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);overlay=out/'overlay';overlay.mkdir(exist_ok=True)
source=fp.src('auto_bump_renderer.h').read_text(encoding='utf-8');needle='const bool equalDepth=equalDepthBatchAllowed&&zfunc==D3DCMP_EQUAL;';assert source.count(needle)==1
source=source.replace('class NorthlightAutoBump {','extern unsigned northlightReadOnlyPolicy;\nclass NorthlightAutoBump {',1).replace(needle,'const bool equalDepth=equalDepthBatchAllowed&&(northlightReadOnlyPolicy==1||(northlightReadOnlyPolicy==2&&zfunc==D3DCMP_EQUAL));',1)
(overlay/'auto_bump_renderer.h').write_bytes(source.encode())
assets=Assets()
try:
 (out/'rigid.bin').write_bytes(next(code for i,_,code in variants(assets.read('shaders\\vertex\\vs_3_0\\diffuse_t1.bls')) if i==1))
 (out/'alpha.bin').write_bytes(next(code for i,_,code in variants(assets.read('shaders\\pixel\\ps_3_0\\combiners_mod.bls')) if i==0))
finally:assets.close()
shutil.copyfile(client_fixtures.four_bone_body_vs3(),out/'skin.bin')
shutil.copyfile(fp.SHADERS/'auto_bump_normal.hlsl',out/'auto_bump_normal.hlsl')
(out/'consumer.hlsl').write_text('''sampler2D Scene:register(s0);sampler2D BumpMaterial:register(s9);float4 BumpControl:register(c73);
#include "auto_bump_normal.hlsl"
float4 Main(float2 uv:TEXCOORD0):COLOR0 {
 float3 n=autoBumpNormal(uv,float3(0,0,3),normalize(float3(.4,.2,.89)),3,float3(1,0,0),float3(0,1,0),float3(0,0,1));
 return float4(tex2D(Scene,uv).rgb*(.2+.8*saturate(dot(n,normalize(float3(.4,.5,1))))),1);
}
''',encoding='utf-8',newline='\n')
compiler=Compiler(allow_compatible_binary=True);compiler.compile(out/'consumer.hlsl','Main','ps_3_0',out/'consumer.bin')
consumer_stats=validate_assembly(out/'consumer.bin.asm','ps_3_0')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
inputs={str(f):sha(f) for f in [*fp.SRC.rglob('*.h'),*fp.SRC.rglob('*.inl'),Path(__file__),ROOT/'tests/material_equal_depth_gpu.cpp',ROOT/'tests/model_normals_gpu.cpp',overlay/'auto_bump_renderer.h',fp.SHADERS/'auto_bump_normal.hlsl',out/'consumer.hlsl',out/'consumer.bin',out/'rigid.bin',out/'skin.bin',out/'alpha.bin']}
exe=out/'material-equal.exe';cmd=[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-I'+str(overlay),*fp.test_include_flags(),'-I'+str(ROOT/'tests'),ROOT/'tests/material_equal_depth_gpu.cpp',fp.src('world_gi.cpp'),'-luser32','-luuid','-o',exe]
with (out/'compile.log').open('w') as log:subprocess.run(list(map(str,cmd)),env=fp.zig_env(),stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300)
games=processes();results=[]
for index,backend in enumerate(a.backend):
 for negative in ([False,True] if a.negative_control else [False]):
  processes();args=[exe,backend.resolve(),out/'rigid.bin',out/'skin.bin',out/'alpha.bin',out/'consumer.bin'];args+=['negative'] if negative else ['benchmark'] if a.benchmark else []
  env={**os.environ,'DISABLE_VULKAN_OBS_CAPTURE':'1','DXVK_LOG_PATH':str(out),'DXVK_CONFIG':'d3d9.customVendorId = 1002; d3d9.supportDFFormats = True'}
  r=subprocess.run(list(map(str,args)),env=env,capture_output=True,timeout=300);name=f'gpu-{index}'+('-negative' if negative else '')+'.log';(out/name).write_bytes(r.stdout+r.stderr);text=(r.stdout+r.stderr).decode(errors='replace');print(name,r.returncode,text[-1000:],flush=True)
  unsupported=r.returncode==77 and 'MATERIAL_EQUAL_DEPTH_UNSUPPORTED_DF24:' in text
  if unsupported:assert 'dxvk' not in str(backend).lower(),'Packaged DXVK must exercise the optimized path'
  elif negative:assert r.returncode and 'MATERIAL_EQUAL_MISMATCH' in text,'Unsafe non-EQUAL control must be rejected'
  else:r.check_returncode();assert 'MATERIAL_EQUAL_DEPTH_PASS' in text
  results.append(dict(backend=str(backend.resolve()),backend_sha256=sha(backend),log=name,sha256=sha(out/name),negative=negative,exit_code=r.returncode,unsupported_df24=unsupported))
assert all(sha(Path(f))==h for f,h in inputs.items()),'Source changed during validation'
(out/'validation.json').write_text(json.dumps(dict(pass_=True,benchmark=a.benchmark,consumer_shader=consumer_stats,compiler=compiler.provenance,inputs=inputs,results=results,concurrent_games=games,scope='Actual capture/native/flush, fixed stock M2 workload with identical normal maps, pressure/recovery. Whole timeline includes CPU supply and shared GPU. Not gameplay FPS.',command=list(map(str,cmd))),indent=2)+'\n',encoding='utf-8',newline='\n')
