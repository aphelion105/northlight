#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig manual timing
"""RemovalSmooth exact confidence/edge controls and warm GPU timestamp comparison."""
import argparse, hashlib, json, os, struct, subprocess, sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
from validate_removal_distance_gpu import fixture,image
from validate_native_scene_trace import processes
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--baseline',type=Path,required=True);p.add_argument('--candidate',type=Path,required=True);a=p.parse_args()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
inputs={str(p):sha(p) for p in [Path(__file__),fp.TESTS/'removal_distance_gpu.cpp',fp.TESTS/'validate_removal_distance_gpu.py',a.baseline,a.candidate,fp.COMPILED/'WorldNormals.bin']}
initial=processes();contended=any(p['name'].lower() in ('wow.exe','wow64.exe') for p in initial)
exe=out/'removal.exe';cmd=[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17',fp.TESTS/'removal_distance_gpu.cpp','-luser32','-o',exe]
with (out/'compile.log').open('w') as log:subprocess.run(list(map(str,cmd)),env=fp.zig_env(),stdout=log,stderr=subprocess.STDOUT,check=True)
cases=[]
for name,confidence in [('flat',0.),('trusted',1.),('threshold',.5),('above',.5009765625),('mixed',None),('edges',None),('sky',0.),('no-source',0.),('fog',None),('dense-fog',None),('odd',None)]:
    width,height=(257,193) if name=='odd' else (640,360);prefix=fixture(out,name,width,height)
    w,h=width//2,height//2
    image(Path(prefix+'normals.fgr'),w,h,113,'4e',((0.,1. if (x//7+y//11)%2 else 0.,0. if (x//7+y//11)%2 else 1.,confidence if confidence is not None else float((x//5+y//7)%2)) for y in range(h) for x in range(w)))
    cases.append((name,prefix))
backends=[Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll',fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll',fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll']
runs=[]
for i,dll in enumerate(backends):
    run=out/str(i);run.mkdir();env={**fp.zig_env(),'DXVK_LOG_PATH':str(run),'DISABLE_VULKAN_OBS_CAPTURE':'1','NORTHLIGHT_REMOVAL_DIRECT_NORMALS':'1','NORTHLIGHT_REMOVAL_EXACT':'1','NORTHLIGHT_REMOVAL_BENCH_DRAWS':'512'}
    config=run/'dxvk.conf';config.write_text('dxvk.enableStateCache = False\nd3d9.presentInterval = 0\n');env['DXVK_CONFIG_FILE']=str(config)
    for name,prefix in cases:
        command=[exe,prefix,a.baseline.resolve(),a.candidate.resolve(),fp.COMPILED/'WorldNormals.bin',fp.COMPILED/'WorldNormals.bin',name,dll]
        r=subprocess.run(list(map(str,command)),cwd=run,env=env,capture_output=True,text=True,timeout=90);(run/(name+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8')
        if r.returncode:print(r.stdout+r.stderr,flush=True)
        r.check_returncode();rows=[json.loads(s) for s in r.stdout.splitlines() if s.startswith('{')];value=next(s for s in rows if 'old_ms' in s);assert value['changed']==0 and value['nonfinite']==0
        print(i,name,'exact',value['changed'],'baselineMs',value['old_ms'],'candidateMs',value['native_ms'],flush=True)
        runs.append({'backend':str(dll),'backend_sha256':sha(dll),'case':name,**value})
assert all(sha(Path(p))==h for p,h in inputs.items())
(out/'validation.json').write_text(json.dumps({'pass':True,'inputs':inputs,'runs':runs,'game_running':contended,'timing_uncontended':not contended,'scope':'Authored confidence/edge controls; warm GPU timestamps, no gameplay FPS claim.'},indent=2)+'\n',encoding='utf-8',newline='\n')
