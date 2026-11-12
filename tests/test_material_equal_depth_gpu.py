# northlight-test: requires=windows-gpu,zig,client manual timing
"""Actual stock M2 captures: fixed warm workload across pressure/recovery, paired GPU oracle."""
from pathlib import Path
from itertools import product
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
p.add_argument('--headers',type=Path)
p.add_argument('--m2-admission',action='store_true',help='Pair original and reused M2 range work on buffered rigid, skin and unknown-pose draws')
p.add_argument('--texture-intern',action='store_true',help='Compare duplicate texture insertion, keeping EQUAL batching enabled in both policies')
p.add_argument('--draws',type=int,action='append',help='Packet benchmark draw counts; repeat for a matrix')
p.add_argument('--rounds',type=int,default=20,help='Paired rounds for the texture-intern benchmark')
a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);overlay=out/'overlay';overlay.mkdir(exist_ok=True)
allocation_test=a.texture_intern
assert not a.m2_admission or (a.benchmark and not allocation_test and not a.negative_control and not a.headers)
assert not allocation_test or not a.negative_control
assert not a.draws or allocation_test
assert all(2<=n<=512 for n in a.draws or [])
assert 1<=a.rounds<=100 and (allocation_test or a.rounds==20)
header=(a.headers or fp.src('auto_bump_renderer.h').parent)/'auto_bump_renderer.h'
source=header.read_text(encoding='utf-8');needle='const bool equalDepth=equalDepthBatchAllowed&&zfunc==D3DCMP_EQUAL;';assert source.count(needle)==1
source=source.replace('class NorthlightAutoBump {','extern unsigned northlightReadOnlyPolicy;\nclass NorthlightAutoBump {',1)
test_cpp=ROOT/'tests/material_equal_depth_gpu.cpp'
if allocation_test:
 old='deferredTextures.try_emplace(t.texture,t.bytes).second'
 if old not in source:old='deferredTextures.emplace(t.texture,t.bytes).second'
 assert source.count(old)==1
 source=source.replace(old,'(northlightReadOnlyPolicy==2?deferredTextures.try_emplace(t.texture,t.bytes):deferredTextures.emplace(t.texture,t.bytes)).second',1)
 cpp=test_cpp.read_text(encoding='utf-8');assert cpp.count('benchmark?128u:8u')==1
 cpp=cpp.replace('benchmark?128u:8u','testDraws',1)
 assert cpp.count('benchmark?20u:1u')==1;cpp=cpp.replace('benchmark?20u:1u','benchmark?testRounds:1u',1)
 cpp=cpp.replace('std::setvbuf(stdout,nullptr,_IONBF,0);','const auto* countText=std::getenv("NORTHLIGHT_MATERIAL_TEST_DRAWS");const unsigned testDraws=countText?unsigned(std::strtoul(countText,nullptr,10)):8u;const unsigned testRounds=unsigned(std::strtoul(std::getenv("NORTHLIGHT_MATERIAL_TEST_ROUNDS"),nullptr,10));assert(testDraws>=2&&testDraws<=512&&testRounds>=1&&testRounds<=100);std::setvbuf(stdout,nullptr,_IONBF,0);',1)
 cleanup='    }\n    for(auto* q:{begin,end,frequency,event,disjoint})'
 assert cpp.count(cleanup)==1
 cpp=cpp.replace(cleanup,'        if(frame==2){const auto clean=std::chrono::steady_clock::now();bump.endFrame();r.cpu+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-clean).count();}\n'+cleanup,1)
 test_cpp=out/'material-allocations-gpu.cpp';test_cpp.write_text('#include <cstdlib>\n'+cpp,encoding='utf-8',newline='\n')
else:source=source.replace(needle,'const bool equalDepth=equalDepthBatchAllowed&&(northlightReadOnlyPolicy==1||(northlightReadOnlyPolicy==2&&zfunc==D3DCMP_EQUAL));',1)
if a.m2_admission:
 # Both policies use the same non-EQUAL native draws, shaders and targets.
 # Only the redundant proof/source lookup is reenabled in the control.
 old='rangeAttempted=true;';assert source.count(old)==1
 source=source.replace(old,'rangeAttempted=northlightReadOnlyPolicy==2;',1)
 old='domain==4&&deferredAllowed?&geometrySource:nullptr';assert source.count(old)==1
 source=source.replace(old,'domain==4&&deferredAllowed&&northlightReadOnlyPolicy==2?&geometrySource:nullptr',1)
 cpp=test_cpp.read_text(encoding='utf-8')
 old='unsigned scenario=0;scenario<(benchmark||negative?1:25);++scenario';assert cpp.count(old)==1
 cpp=cpp.replace(old,'unsigned scenario: {6u,23u,24u}',1)
 test_cpp=out/'m2-admission-gpu.cpp';test_cpp.write_text(cpp,encoding='utf-8',newline='\n')
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
inputs={str(f):sha(f) for f in [*overlay.glob('*.h'),*fp.SRC.rglob('*.h'),*fp.SRC.rglob('*.inl'),Path(__file__),header,test_cpp,ROOT/'tests/material_equal_depth_gpu.cpp',ROOT/'tests/model_normals_gpu.cpp',overlay/'auto_bump_renderer.h',fp.SHADERS/'auto_bump_normal.hlsl',out/'consumer.hlsl',out/'consumer.bin',out/'rigid.bin',out/'skin.bin',out/'alpha.bin']}
exe=out/'material-equal.exe';cmd=[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-I'+str(overlay),*fp.test_include_flags(),'-I'+str(ROOT/'tests'),test_cpp,fp.src('world_gi.cpp'),'-luser32','-luuid','-o',exe]
with (out/'compile.log').open('w') as log:subprocess.run(list(map(str,cmd)),env=fp.zig_env(),stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300)
games=processes();results=[]
for index,backend in enumerate(a.backend):
 for count,negative in product(a.draws or [128 if a.benchmark else 8],[False,True] if a.negative_control else [False]):
  processes();args=[exe,backend.resolve(),out/'rigid.bin',out/'skin.bin',out/'alpha.bin',out/'consumer.bin'];args+=['negative'] if negative else ['benchmark'] if a.benchmark else []
  env={**os.environ,'DISABLE_VULKAN_OBS_CAPTURE':'1','DXVK_LOG_PATH':str(out),'DXVK_CONFIG':'d3d9.customVendorId = 1002; d3d9.supportDFFormats = True','NORTHLIGHT_MATERIAL_TEST_DRAWS':str(count),'NORTHLIGHT_MATERIAL_TEST_ROUNDS':str(a.rounds)}
  r=subprocess.run(list(map(str,args)),env=env,capture_output=True,timeout=300);name=f'gpu-{index}'+('-n'+str(count) if a.draws else '')+('-negative' if negative else '')+'.log';(out/name).write_bytes(r.stdout+r.stderr);text=(r.stdout+r.stderr).decode(errors='replace');print(name,r.returncode,text[-1000:],flush=True)
  unsupported=r.returncode==77 and 'MATERIAL_EQUAL_DEPTH_UNSUPPORTED_DF24:' in text
  if unsupported:assert 'dxvk' not in str(backend).lower(),'Packaged DXVK must exercise the optimized path'
  elif negative:assert r.returncode and 'MATERIAL_EQUAL_MISMATCH' in text,'Unsafe non-EQUAL control must be rejected'
  else:r.check_returncode();assert 'MATERIAL_EQUAL_DEPTH_PASS' in text
  results.append(dict(backend=str(backend.resolve()),backend_sha256=sha(backend),log=name,sha256=sha(out/name),negative=negative,exit_code=r.returncode,unsupported_df24=unsupported,test_draws=count))
if a.m2_admission:
 from statistics import median
 timings=[]
 for result in results:
  if result['unsupported_df24']:continue
  rows=[dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in (out/result['log']).read_text().splitlines() if line.startswith('EQUAL_FRAME ')]
  for scenario,phase in product([6,23,24],range(3)):
   pair=[[r for r in rows if int(r['scenario'])==scenario and int(r['phase'])==phase and int(r['current'])==current] for current in range(2)]
   assert all(len(r)==20 for r in pair)
   for before,after in zip(*pair):
    assert before['draws']==after['draws']
    if scenario==24:assert int(after['vertexReads'])<int(before['vertexReads']),(before,after)
   cpu=[median(float(r['cpuMs']) for r in records) for records in pair]
   gpu=[median(float(r['gpuMs']) for r in records) for records in pair]
   row=dict(backend=result['backend'],scenario=scenario,phase=phase,cpu_before_ms=cpu[0],cpu_after_ms=cpu[1],cpu_speedup=cpu[0]/cpu[1],gpu_before_ms=gpu[0],gpu_after_ms=gpu[1])
   timings.append(row);print('M2_ADMISSION_TIMING',json.dumps(row),flush=True)
  (out/'m2-timings.json').write_text(json.dumps(timings,indent=2)+'\n')
assert all(sha(Path(f))==h for f,h in inputs.items()),'Source changed during validation'
(out/'validation.json').write_text(json.dumps(dict(pass_=True,benchmark=a.benchmark,texture_intern=a.texture_intern,rounds=a.rounds,consumer_shader=consumer_stats,compiler=compiler.provenance,inputs=inputs,results=results,concurrent_games=games,scope='Actual capture/native/flush, fixed stock M2 workload with identical normal maps, pressure/recovery. Allocation-test CPU timing also includes endFrame cleanup. Whole timeline includes CPU supply and shared GPU. Not gameplay FPS.',command=list(map(str,cmd))),indent=2)+'\n',encoding='utf-8',newline='\n')
