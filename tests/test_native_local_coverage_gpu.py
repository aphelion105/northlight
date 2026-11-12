#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client manual
"""Native local shadows compose through the actual two-MRT coverage factory."""
import argparse,hashlib,json,os,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
files=[*[p for p in fp.SRC.rglob('*') if p.is_file()],Path(__file__),fp.TESTS/'native_local_light_gpu.cpp',fp.TESTS/'native_local_coverage_checks.h']
inputs={str(p):sha(p) for p in files};exe=out/'native-coverage.exe';cmd=[fp.zig(),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-DNORTHLIGHT_TEST_COVERED_NATIVE',*fp.test_include_flags(),fp.TESTS/'native_local_light_gpu.cpp','-luser32','-o',exe]
with (out/'compile.log').open('w') as log:subprocess.run(list(map(str,cmd)),env=fp.zig_env(),stdout=log,stderr=subprocess.STDOUT,check=True)
backends=[Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll',fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll',fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll'];runs=[]
for i,dll in enumerate(backends):
 for family in ('m2','wmo'):
  fixture=fp.REPO/f'out/event-rootcause-20261107-r14/lamp/native-regression/{family}';run=out/f'{i}-{family}';run.mkdir();vs=fixture/'native-vs.bin';ps=fixture/'native-ps.bin';inputs[str(vs)]=sha(vs);inputs[str(ps)]=sha(ps)
  env={**fp.zig_env(),'DXVK_LOG_PATH':str(run),'DISABLE_VULKAN_OBS_CAPTURE':'1','NORTHLIGHT_TEST_CORRECTNESS_ONLY':'1'}
  r=subprocess.run(list(map(str,[exe,dll,vs,ps,run,family,fp.COMPILED])),cwd=run,env=env,capture_output=True,text=True,timeout=90);(run/'gpu.log').write_text(r.stdout+r.stderr,encoding='utf-8');print(i,family,r.stdout+r.stderr,flush=True);r.check_returncode();assert 'NATIVE_COVERAGE_PASS ' in r.stdout
  runs.append({'backend':str(dll),'backend_sha256':sha(dll),'family':family,'log':str(run/'gpu.log')})
assert all(sha(Path(p))==h for p,h in inputs.items())
(out/'validation.json').write_text(json.dumps({'pass':True,'inputs':inputs,'runs':runs,'game_launched':False,'scope':'Isolated real native shaders and actual coverage factory; no gameplay FPS or complete flicker claim.'},indent=2)+'\n',encoding='utf-8',newline='\n')
