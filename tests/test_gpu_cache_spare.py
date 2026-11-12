#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Shared GPU cache pressure, exact bytes, quota boundaries and held references."""
import argparse,hashlib,json,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
import replay_gpu_fixture
p=argparse.ArgumentParser(__doc__)
p.add_argument('--output',type=Path);p.add_argument('--reference',type=Path);a=p.parse_args()
out=(a.output or fp.output_dir()/'gpu-cache-spare').resolve();out.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
stub=replay_gpu_fixture.stub;first=stub.index('using UINT');last=stub.index('constexpr unsigned D3DUSAGE')
stub=stub[:first]+'#include <windows.h>\nconstexpr HRESULT D3D_OK=0,D3DERR_INVALIDCALL=HRESULT(0x8876086cu);\n'+stub[last:]
(out/'d3d9.h').write_text(stub,encoding='utf-8');(out/'replay_fixture.h').write_text(replay_gpu_fixture.fixture,encoding='utf-8')
source=fp.TESTS/'test_gpu_cache_spare.cpp'
inputs={str(q):sha(q) for q in [Path(__file__),source,fp.TESTS/'test_shared_gpu_residency.cpp',fp.src('scene_native_store.h')]}
reference=[]
if a.reference:
 ref=a.reference.resolve();inputs[str(ref)]=sha(ref);reference=['-DNORTHLIGHT_GPU_SPARE_REFERENCE="'+str(ref).replace('\\','/')+'"']
runs=[];env=fp.zig_env()
for tag,flags in [('O2',['-O2']),('ASan-UBSan',['-O2','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all'])]:
 exe=out/(tag+'.exe')
 command=[str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-UNDEBUG','-Wall','-Wextra','-Werror',*flags,*reference,'-I',str(out),*fp.test_include_flags(),str(source),'-o',str(exe)]
 subprocess.run(command,env=env,check=True)
 result=subprocess.run([str(exe)],env=env,text=True,capture_output=True,timeout=180)
 (out/(tag+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8');print(result.stdout+result.stderr,flush=True);result.check_returncode()
 assert 'GPU_SPARE_CONTROLS_PASS' in result.stdout and 'GPU_SPARE_ENTRY_COUNT_PASS' in result.stdout and 'GPU_AGGREGATE_BUDGET_PASS' in result.stdout
 if reference:assert result.stdout.count('GPU_SPARE_COMPARE_PASS')==4
 runs.append({'tag':tag,'command':command,'output':result.stdout})
assert all(sha(q)==value for q,value in inputs.items()),'Inputs changed during tests'
(out/'validation.json').write_text(json.dumps({'pass':True,'inputs':inputs,'runs':runs,'controls':26,'counterfactual_cases':4 if reference else 0,'GPU_used':False},indent=2)+'\n',encoding='utf-8')
