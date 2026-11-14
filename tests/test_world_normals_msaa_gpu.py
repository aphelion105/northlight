# northlight-test: requires=windows-gpu,zig manual timing
"""Compare the existing WorldNormals image with fused MSAA input consumption."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
import northlight_paths as fp
import windows_gpu_guard

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--backend',type=Path,action='append',required=True)
parser.add_argument('--shader',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--timing',action='store_true')
parser.add_argument('--native-fixture',type=Path,help='Timing only: compare replay+WorldNormals with native MRT+fused WorldNormals')
parser.add_argument('--draws',type=int,default=192)
parser.add_argument('--native-mode',default='plain-local-msaa-near')
args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
env={**fp.zig_env(),'DXVK_LOG_PATH':str(out),'DISABLE_VULKAN_OBS_CAPTURE':'1'}
def identity():
    paths = [Path(__file__), fp.TESTS/'world_normals_msaa_gpu.cpp', fp.TESTS/'world_normals_msaa_fixture.h', fp.TESTS/'model_normal_native_gpu.cpp', fp.TESTS/'model_normals_gpu.cpp', ROOT/'scripts/shaders/compile_world_normals_msaa.py']
    paths += [p for folder in (fp.SRC, fp.SHADERS) for p in folder.rglob('*') if p.is_file()]
    return {str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}

def run(name,command):
    with (out/(name+'.log')).open('w',encoding='utf-8') as log:
        result=subprocess.run(list(map(str,command)),cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT)
    output=(out/(name+'.log')).read_text(encoding='utf-8',errors='replace')
    print(output[-5000:],flush=True)
    return {'log':name+'.log','exit_code':result.returncode,'output':output}

before=identity()
report={'scope':'native capture+draw+replay/MRT+scene/depth resolve+WorldNormals' if args.native_fixture else __doc__,
        'backends':[],'sources':before,'commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT).decode('utf-8').strip(),
        'environment':{k:env.get(k) for k in ('DXVK_CONFIG','DXVK_FILTER_DEVICE_NAME','NORTHLIGHT_TEST_ALLOW_RUNNING_GAME')}}
report['options']={k:str(v) if isinstance(v,Path) else [str(x) for x in v] if isinstance(v,list) else v for k,v in vars(args).items()}
report['shader']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in args.shader.iterdir() if p.is_file()}
if args.native_fixture:
    assert args.draws>0 and args.draws<=512 and 'msaa' in args.native_mode
    report['fixture']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in args.native_fixture.iterdir() if p.is_file()}
exe=out/'world-normals-msaa.exe'
options=['-DNORTHLIGHT_WORLD_NORMAL_MSAA_TEST'] if args.native_fixture else []
source=fp.TESTS/('model_normal_native_gpu.cpp' if args.native_fixture else 'world_normals_msaa_gpu.cpp')
compiled=run('compile',[fp.zig(),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-Wno-microsoft-exception-spec','-Wno-return-type',*options,'-I'+str(args.shader.resolve()),*fp.test_include_flags(),source,fp.src('world_gi.cpp'),'-luser32','-o',exe])
report['compile']=compiled
success=compiled['exit_code']==0
if success:
    data=bytearray(exe.read_bytes());at=struct.unpack_from('<I',data,0x3c)[0]+22;struct.pack_into('<H',data,at,struct.unpack_from('<H',data,at)[0]|32);exe.write_bytes(data)
    for i,backend in enumerate(args.backend):
        census=windows_gpu_guard.processes()
        if args.native_fixture:
            checks=[run('native-'+str(i),[exe,backend.resolve(),str(args.native_fixture.resolve())+'/',args.draws,1,args.native_mode])]
        else:
            checks=[run('gpu-'+str(i),[exe,backend.resolve()])]
            if args.timing:checks.append(run('timing-'+str(i),[exe,backend.resolve(),'timing']))
        success &= all(c['exit_code']==0 for c in checks)
        report['backends'].append({'path':str(backend.resolve()),'sha256':hashlib.sha256(backend.read_bytes()).hexdigest(),'processes':census,'checks':checks})
report['source_changed']=before!=identity()
success &= not report['source_changed']
report['run_succeeded']=success
report['image_acceptance']=False if args.native_fixture else success
report['performance_acceptance']=False
report['game_fps_claim']=False
(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8',newline='\n')
sys.exit(0 if success else 1)
