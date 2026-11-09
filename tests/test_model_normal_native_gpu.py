# northlight-test: requires=windows-gpu,zig,client manual
"""Production native normal MRT, replay rollback, material/light composition."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
import northlight_paths as fp
fp.use_source_modules()
from world_scene_builder import Assets
from extract_signatures import variants
from extract_m2_shader_families import collect
import windows_gpu_guard

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--fixture',type=Path,required=True,help='Prepared stock human-0.mesh receiver fixture')
parser.add_argument('--backend',type=Path,action='append',required=True)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--timing-draws',type=int,action='append',default=[],help='Additionally measure complete 4K capture/submission paths (resident geometry)')
parser.add_argument('--timing-copies',type=int,default=1,help='Repeat stock geometry within each timing draw (1..32)')
parser.add_argument('--timing-local',action='store_true',help='Measure the actual three-local-light factory with MRT')
parser.add_argument('--timing-blend',action='store_true',help='Include a source-alpha blended depth writer before the commit')
args=parser.parse_args()
out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
fixture=out/'fixture';fixture.mkdir()
shutil.copyfile(args.fixture/'human-0.mesh',fixture/'human-0.mesh')
assets=Assets(archives='stock')
try:
    programs={i:code for i,_,code in variants(assets.read('shaders\\vertex\\vs_3_0\\diffuse_t1.bls'))}
    for name,index in [('rigid',1),('skinned',11),('weighted',21),('weighted4',51)]:
        (fixture/('model-vs3-'+name+'.bin')).write_bytes(programs[index])
    (fixture/'ModelAlphaPS.bin').write_bytes(next(variants(assets.read('shaders\\pixel\\ps_3_0\\combiners_mod.bls')))[2])
finally:assets.close()
local=collect(fp.client_root())[0][0xe8d455338d8ae2ff][1]
(fixture/'model-vs3-local.bin').write_bytes(local)
env={**fp.zig_env(),'DXVK_LOG_PATH':str(out),'DISABLE_VULKAN_OBS_CAPTURE':'1'}

def run(tag,command):
    log=out/(tag+'.log')
    with log.open('w') as f:r=subprocess.run(list(map(str,command)),cwd=ROOT,env=env,stdout=f,stderr=subprocess.STDOUT)
    text=log.read_text(errors='replace')
    if r.returncode:print(text[-3000:],flush=True)
    r.check_returncode();return text

exe=out/'native-models.exe'
run('compile',[fp.zig(),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-Wno-microsoft-exception-spec','-Wno-return-type',*fp.test_include_flags(),ROOT/'tests/model_normal_native_gpu.cpp',fp.src('world_gi.cpp'),'-luser32','-o',exe])
data=bytearray(exe.read_bytes());at=struct.unpack_from('<I',data,0x3c)[0]+22;struct.pack_into('<H',data,at,struct.unpack_from('<H',data,at)[0]|0x20);exe.write_bytes(data)
backends=[]
for i,path in enumerate(args.backend):
    windows_gpu_guard.processes();path=path.resolve();text=run('gpu-'+str(i),[exe,path,str(fixture)+'/'])
    assert 'MODEL_NORMAL_NATIVE_GPU_PASS' in text
    backends.append({'path':str(path),'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'log':'gpu-'+str(i)+'.log'})
    print(path.name,next(line for line in text.splitlines() if line.startswith('MODEL_NORMAL_NATIVE_GPU_PASS')),flush=True)
    for draws in args.timing_draws:
        assert 0<draws<=512
        assert 1<=args.timing_copies<=32
        mode='blend-local' if args.timing_local and args.timing_blend else 'local' if args.timing_local else 'blend' if args.timing_blend else 'plain'
        text=run('timing-'+str(i)+'-'+str(draws),[exe,path,str(fixture)+'/',draws,args.timing_copies,mode])
        print('\n'.join(line for line in text.splitlines() if line.startswith('MODEL_NORMAL_NATIVE_TIMING')),flush=True)
inputs={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in fp.SRC.rglob('*') if p.is_file()}
report={'pass':True,'sources':inputs,'fixtures':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in fixture.iterdir()},'backends':backends,'scope':'Exact native color/model normals, native-depth occluder ownership, whole-queue rollback, shader/RT state restoration. No game FPS claim.'}
report['tests']={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [Path(__file__),ROOT/'tests/model_normal_native_gpu.cpp',ROOT/'tests/model_normals_gpu.cpp']}
report['timing']={'draws':args.timing_draws,'copies':args.timing_copies,'resolution':[3840,2160],'includes':'native submission, immutable queue capture, resident replay or MRT commit; timestamp interval may include command starvation','excludes':'remaining game/extension pipeline'}
report['timing']['local']=args.timing_local
report['timing']['blended_depth']=args.timing_blend
(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
