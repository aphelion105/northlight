#!/usr/bin/env python3
# northlight-test: requires=client,zig,windows-gpu manual
"""Compile current combined native source; guard actual dependencies through GPU parity."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,required=True);ap.add_argument('--baseline-receipt',type=Path,required=True);ap.add_argument('--allow-game-process',action='store_true');ap.add_argument('--sampler-bulk-mode',type=int,choices=range(6));args=ap.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
files={Path(__file__).resolve(),fp.TESTS/'test_native_local_vertex_reuse_gpu.py'};commands=[];exes={}
for kind in ['light','pom','displacement']:
 source=fp.TESTS/f'native_local_{kind}_gpu.cpp';dep=out/f'{kind}-before.d';cmd=list(map(str,[fp.zig(),'c++','-target','x86-windows-gnu','-std=c++17','-MM','-MF',dep,*fp.test_include_flags(),source]));r=subprocess.run(cmd,env=fp.zig_env(),capture_output=True,text=True,timeout=180);assert r.returncode==0,r.stderr
 for line in dep.read_text().splitlines()[1:]:
  name=line.strip().removesuffix('\\').strip();path=Path(name)
  if path.is_file():files.add(path.resolve())
guard={str(p):sha(p) for p in files}
for kind in ['light','pom','displacement']:
 exe=out/f'{kind}-candidate.exe';dep=out/f'{kind}-compiled.d';cmd=list(map(str,[fp.zig(),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-MMD','-MF',dep,*fp.test_include_flags(),fp.TESTS/f'native_local_{kind}_gpu.cpp','-luser32','-o',exe]));commands.append(cmd)
 with (out/f'{kind}-compile.log').open('w') as log:subprocess.run(cmd,env=fp.zig_env(),stdout=log,stderr=subprocess.STDOUT,check=True,timeout=180)
 for line in dep.read_text().splitlines()[1:]:
  name=line.strip().removesuffix('\\').strip();path=Path(name)
  if path.is_file():assert str(path.resolve()) in guard,f'Unexpected compiled dependency {path}'
 exes[str(exe)]=sha(exe)
assert all(sha(p)==digest for p,digest in guard.items()),'Actual source dependency changed during compile'
receipt=out/'candidate-build.json';receipt.write_text(json.dumps({'source_sha256':guard,'executables_sha256':exes,'commands':commands,'dependency_files':[str(out/f'{kind}-compiled.d') for kind in ['light','pom','displacement']]},indent=2)+'\n')
cmd=[sys.executable,str(fp.TESTS/'test_native_local_vertex_reuse_gpu.py'),'--candidate-receipt',str(receipt),'--baseline-receipt',str(args.baseline_receipt.resolve()),'--output',str(out/'gpu')]
if args.allow_game_process:cmd.append('--allow-game-process')
if args.sampler_bulk_mode is not None:cmd+=['--sampler-bulk-mode',str(args.sampler_bulk_mode)]
subprocess.run(cmd,check=True)
assert all(sha(p)==digest for p,digest in guard.items()),'Actual source dependency changed during GPU verification'
(out/'validation.json').write_text(json.dumps({'pass':True,'source_sha256':guard,'actual_dependency_count':len(guard),'candidate_receipt_sha256':sha(receipt),'gpu_receipt_sha256':sha(out/'gpu/validation.json'),'baseline_receipt_sha256':sha(args.baseline_receipt),'timing_claim':False},indent=2)+'\n');print('NATIVE_JOINT_REGRESSION_PASS',len(guard),flush=True)
