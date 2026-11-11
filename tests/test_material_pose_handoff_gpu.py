# northlight-test: requires=windows-gpu,zig,client manual timing
"""Actual material pose producer -> normal capture, old/new policy in one executable."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,sys
ROOT=Path(__file__).resolve().parents[1];sys.path[:0]=[str(ROOT),str(ROOT/'tests')]
import northlight_paths as fp
fp.use_source_modules()
import client_fixtures
from world_scene_builder import Assets
from extract_signatures import variants
from windows_gpu_guard import processes
p=argparse.ArgumentParser(__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--backend',type=Path,action='append',required=True);p.add_argument('--benchmark',action='store_true');p.add_argument('--range-shortcut',action='store_true',help='Compare r107 range publication/query with the shortcut; handoff stays enabled in both policies.');a=p.parse_args()
games=[p for p in processes() if p['name'].lower().startswith('wow')];out=a.output.resolve();out.mkdir(exist_ok=True,parents=True);overlay=out/'overlay';overlay.mkdir(exist_ok=True)
source=fp.src('auto_displacement_renderer.h').read_text();marker='if(!reusePose&&(view.low[2]>48.f||view.high[2]<-48.f))publishPose(view);';assert source.count(marker)==1
source=source.replace('class NorthlightAutoDisplacement {','extern bool northlightLocalPoseHandoff;\nclass NorthlightAutoDisplacement {',1).replace(marker,'if(northlightLocalPoseHandoff&&!reusePose&&(view.low[2]>48.f||view.high[2]<-48.f))publishPose(view);');(overlay/'auto_displacement_renderer.h').write_bytes(source.encode())
cpp=ROOT/'tests/material_pose_handoff_gpu.cpp'
if a.range_shortcut:
    header=fp.src('pose_bounds_handoff.h').read_text(encoding='utf-8');before=subprocess.check_output(['git','show','b2385af0:src/replay/pose_bounds_handoff.h'],cwd=ROOT).decode()
    signature='    void publish(';end='    bool outside('
    old=before[before.index(signature):before.index(end)].replace('void publish(','void publishBefore(',1)
    assert header.count(signature)==header.count('return lower>48.01;')==1
    header=header.replace('namespace NorthlightPoseBounds {','extern bool northlightRangeShortcut;\nnamespace NorthlightPoseBounds {',1).replace('void publish(','void publishCurrent(',1)
    wrapper='''    void publish(const NorthlightReplayBounds::Bounds& value,const float* bank,const NorthlightConstantEpoch::Stamp& current,const void* mesh,const void* vs,const void* decl){
        if(northlightRangeShortcut)publishCurrent(value,bank,current,mesh,vs,decl);
        else{publishBefore(value,bank,current,mesh,vs,decl);outsideRange=outside(mesh,vs,decl,current);}
    }
'''
    header=header.replace(end,old+wrapper+end,1).replace('return lower>48.01;','return northlightRangeShortcut?lower>48.01:std::nextafter(lower,-INFINITY)>48.01;',1)
    (overlay/'pose_bounds_handoff.h').write_text(header,encoding='utf-8',newline='\n')
    text=cpp.read_text(encoding='utf-8');assert text.count('northlightLocalPoseHandoff=current;')==text.count('expected+=current&&!anchor')==1
    text=text.replace('bool northlightLocalPoseHandoff=true;','bool northlightRangeShortcut=true;\nbool northlightLocalPoseHandoff=true;',1).replace('northlightLocalPoseHandoff=current;','northlightLocalPoseHandoff=true;northlightRangeShortcut=current;',1).replace('expected+=current&&!anchor','expected+=!anchor',1).replace('rounds=timing?24:2,Count=timing?128:16','rounds=timing?40:2,Count=timing?512:16',1)
    cpp=out/'range-shortcut-gpu.cpp';cpp.write_text(text,encoding='utf-8',newline='\n')
shader=out/'body-vs.bin';shutil.copyfile(client_fixtures.four_bone_body_vs3(),shader)
pixel=out/'native-ps.bin';assets=Assets()
try:pixel.write_bytes(next(code for index,_,code in variants(assets.read('shaders\\pixel\\ps_3_0\\combiners_opaque.bls')) if index==0))
finally:assets.close()
inputs={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in [*fp.SRC.rglob('*.h'),*fp.SRC.rglob('*.inl'),ROOT/'tests/model_normals_gpu.cpp',*sorted((ROOT/'tests').glob('model_normal_*.inc')),ROOT/'tests/material_pose_handoff_gpu.cpp',Path(__file__).resolve(),*overlay.glob('*.h'),cpp,shader,pixel]}
exe=out/'pose-handoff.exe';command=[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-return-type','-Wno-microsoft-exception-spec','-I',overlay,'-I'+str(ROOT/'tests'),*fp.test_include_flags(),cpp,fp.src('world_gi.cpp'),'-luser32','-luuid','-o',exe]
env={**fp.zig_env(),'DISABLE_VULKAN_OBS_CAPTURE':'1','DXVK_LOG_PATH':str(out),'PYTHONUTF8':'1'}
with (out/'compile.log').open('w') as log:subprocess.run(list(map(str,command)),stdout=log,stderr=subprocess.STDOUT,check=True,env=env,timeout=300)
results=[]
for index,backend in enumerate(a.backend):
    processes();r=subprocess.run([str(exe),str(backend.resolve()),str(shader),str(pixel),*(['timing'] if a.benchmark else [])],env=env,cwd=ROOT,capture_output=True,timeout=180)
    (out/f'gpu-{index}.log').write_bytes(r.stdout+r.stderr);print((r.stdout+r.stderr).decode(errors='replace')[-2000:],flush=True);r.check_returncode();assert b'LOCAL_POSE_GPU_PASS' in r.stdout
    results.append({'backend':str(backend.resolve()),'sha256':hashlib.sha256(backend.read_bytes()).hexdigest()})
assert all(hashlib.sha256(Path(f).read_bytes()).hexdigest()==h for f,h in inputs.items()),'Source changed during the oracle'
(out/'validation.json').write_text(json.dumps({'pass':True,'actual_producer_consumer':True,'timing':a.benchmark,'range_shortcut':a.range_shortcut,'concurrent_games':games,'gpu_timing_scope':'Full producer/capture/replay timestamp timeline, including CPU command-supply gaps and shared GPU contention.','inputs':inputs,'results':results,'command':list(map(str,command)),'scope':'Authored workload with forced budget refusal; not a gameplay performance measurement.'},indent=2),encoding='utf-8')
