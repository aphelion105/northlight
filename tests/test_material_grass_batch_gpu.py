# northlight-test: requires=windows-gpu,zig,client,stormlib timing
"""Stock grass cutouts: immediate versus guarded packets, exact GPU products."""
import json,os,re,shutil,struct,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
from world_scene_builder import Assets,decode_blp
from extract_signatures import variants
from windows_gpu_guard import processes
sys.path.insert(0,str(fp.REPO/'scripts/shaders'))
from shader_compiler import Compiler
out=fp.output_dir()/'grass-batch';out.mkdir(parents=True,exist_ok=True)
source=out/'source';source.mkdir(exist_ok=True)
assets=Assets(archives='stock')
try:
 for model in (2,3):
  code=dict((i,c) for i,_,c in variants(assets.read(f'shaders\\vertex\\vs_{model}_0\\detaildoodad.bls')))[0]
  (source/f'grass-vs-{model}.bin').write_bytes(code)
 code=dict((i,c) for i,_,c in variants(assets.read('shaders\\pixel\\ps_3_0\\detaildoodad.bls')))[0]
 (source/'terrain-ps.bin').write_bytes(code)
 texture=assets.read('world\\nodxt\\detail\\silvermoondetaildoodad01.blp')
finally:assets.close()
level=0
while True:
 w,h,rgba=decode_blp(texture,max(1,256>>level));bgra=bytearray(rgba);bgra[0::4]=rgba[2::4];bgra[2::4]=rgba[0::4]
 data=struct.pack('<5I',0x31524746,w,h,21,4)+bgra
 (source/f'source.fgr.{level}').write_bytes(data)
 if not level:(source/'source.fgr').write_bytes(data)
 if w==h==1:break
 level+=1
Compiler(allow_compatible_binary=True).compile(fp.SHADERS/'auto_normal_preview.hlsl','PreviewBase','ps_3_0',source/'PreviewBase.bin')
control=out/'control';control.mkdir(exist_ok=True)
for name in ('auto_bump_renderer.h','auto_displacement_renderer.h'):
 (control/name).write_bytes(subprocess.check_output(['git','show','af4a7c1c:src/water/'+name]))
executables={}
for name in ('control','current'):
 exe=out/(name+'.exe');executables[name]=exe
 subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-DNORTHLIGHT_MATERIAL_MIRROR','-DNORTHLIGHT_OBLIQUE_WIDTH=320','-DNORTHLIGHT_OBLIQUE_HEIGHT=200',*(['-I'+str(control)] if name=='control' else []),*fp.test_include_flags(),str(fp.TESTS/'auto_normal_oblique_gpu.cpp'),'-luser32','-luuid','-o',str(exe)],env=fp.zig_env(),check=True)
backend=fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll'
cases=[('warm',{}),('mutation',{'DEFERRED_MUTATION':'1'}),('texture',{'DEFERRED_TEXTURE_WRITE':'1'}),('pressure',{'PRESSURE':'1'}),('barrier',{'DEFERRED_BARRIER':'1'})]
records=[]
for model in (2,3):
 for samples in (0,2,4):
  for case,flags in cases:
   if samples==0 and case!='warm':continue
   runs=[]
   for batch in (0,1):
    folder=out/f'sm{model}-{samples}-{case}-{batch}';folder.mkdir(exist_ok=True)
    for p in source.iterdir():shutil.copy2(p,folder/p.name)
    shutil.copy2(source/f'grass-vs-{model}.bin',folder/'terrain-vs.bin')
    env={**fp.zig_env(),'DISABLE_VULKAN_OBS_CAPTURE':'1','DXVK_CONFIG':'d3d9.customVendorId = 1002; ','NORTHLIGHT_TEST_MSAA':str(samples),'NORTHLIGHT_TEST_TEXTURE_GUARD':'1','NORTHLIGHT_TEST_DEFERRED_RESIDENT':'1','NORTHLIGHT_TEST_MATERIAL_BATCH_TIMING':'1','NORTHLIGHT_TEST_MATERIAL_BATCH_VALIDATE':'1','NORTHLIGHT_TEST_CAPTURE_DRAWS':'64'}
    env.update({'NORTHLIGHT_TEST_'+k:v for k,v in flags.items()})
    if case=='mutation':env.pop('NORTHLIGHT_TEST_DEFERRED_RESIDENT',None)
    if batch:env['NORTHLIGHT_TEST_DEFERRED_CAPTURE']='1'
    processes()
    r=subprocess.run([str(executables['current' if batch else 'control']),str(backend),str(folder)+'/', '12','.7','grass'],env=env,cwd=folder,capture_output=True,text=True,timeout=180)
    (folder/'gpu.log').write_text(r.stdout+r.stderr,encoding='utf-8');r.check_returncode()
    runs.append((folder,r.stdout))
   products=list(runs[0][0].glob('batch-*.fgr'));assert len(products)>=2
   for p in products:assert p.read_bytes()==(runs[1][0]/p.name).read_bytes(),(model,samples,case,p.name)
   assert any(runs[0][0].joinpath('batch-normal.fgr').read_bytes()[20:]),'Empty normal oracle'
   record={'model':model,'msaa':samples,'case':case,'exact_products':len(products),'logs':[str(f/'gpu.log') for f,_ in runs]}
   switches=[dict((k,int(v)) for k,v in re.findall(r'(targets|depths|repeats)=(\d+)',re.search(r'MATERIAL_BATCH_SWITCHES[^\n]+',log)[0])) for _,log in runs]
   if samples and case in ('warm','pressure'):
    assert switches[1]['targets']<switches[0]['targets'] and switches[1]['depths']<switches[0]['depths'],switches
   record['switches']=switches
   records.append(record);print('GRASS_BATCH_PASS',model,samples,case,flush=True)
(out/'validation.json').write_text(json.dumps({'pass':True,'records':records,'game_fps_measured':False},indent=2)+'\n',encoding='utf-8')
