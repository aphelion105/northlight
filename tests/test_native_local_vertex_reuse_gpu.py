#!/usr/bin/env python3
# northlight-test: requires=client,windows-gpu manual
"""Compare frozen native vertex-reuse executables with a validated prior build."""
from pathlib import Path
import argparse, hashlib, json, os, subprocess, sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
ap=argparse.ArgumentParser();ap.add_argument('--candidate-receipt',type=Path,required=True);ap.add_argument('--baseline-receipt',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);ap.add_argument('--allow-game-process',action='store_true');ap.add_argument('--sampler-bulk-mode',type=int,choices=range(6));args=ap.parse_args()
if not args.allow_game_process:assert 'wow.exe' not in subprocess.check_output(['tasklist','/FO','CSV'],text=True).lower()
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
receipts={name:json.loads(path.read_text()) for name,path in [('candidate',args.candidate_receipt),('baseline',args.baseline_receipt)]}
exes={}
for variant,receipt in receipts.items():
 for kind in ['light','pom','displacement']:
  matches=[Path(p) for p in receipt['executables_sha256'] if Path(p).name==f'{kind}-candidate.exe'];assert len(matches)==1;exe=matches[0];assert sha(exe)==receipt['executables_sha256'][str(exe)];exes[variant,kind]=exe
out=args.output.resolve();out.mkdir(parents=True,exist_ok=True);results=[];fgrPairs=0
backends={'native':Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll',**{name:fp.client_root()/f'renderer-backends/{name}/{name}_d3d9.dll' for name in ['dxvk','dxvk2']}}
for backend,dll in backends.items():
 for kind in ['light','pom','displacement']:
  for family in (['captured-m2'] if kind=='displacement' else ['m2','wmo']):
   for mode in (['gpu','cpu'] if kind=='displacement' else ['default']):
    digests=[];runs=[]
    for variant in ['candidate','baseline']:
     run=out/backend/kind/family/mode/variant;run.mkdir(parents=True,exist_ok=True);runs.append(run);digest=run/'readbacks.txt';digest.write_bytes(b'')
     fixture=fp.REPO/'out/event-rootcause-20261107-r14/lamp'/('current/captured-m2' if kind=='displacement' else f'native-regression/{family}')
     alternate=fp.REPO/'out/event-rootcause-20261107-r14/lamp'/('native-regression/m2/native-ps.bin' if kind=='displacement' else 'current/captured-m2/native-ps.bin')
     env={**fp.zig_env(),'DXVK_LOG_PATH':str(run),'DISABLE_VULKAN_OBS_CAPTURE':'1','NORTHLIGHT_TEST_CORRECTNESS_ONLY':'1','NORTHLIGHT_TEST_SHARING_FAULTS':'1','NORTHLIGHT_TEST_PIXEL_REUSE':'1','NORTHLIGHT_TEST_VERTEX_REUSE':'1','NORTHLIGHT_TEST_VERTEX_ALTERNATE_PS':str(alternate),'NORTHLIGHT_NATIVE_IMAGE_DIGEST':str(digest)}
     env.pop('NORTHLIGHT_TEST_SAMPLER_BULK_MODE',None)
     if variant=='candidate' and args.sampler_bulk_mode is not None:env['NORTHLIGHT_TEST_SAMPLER_BULK_MODE']=str(args.sampler_bulk_mode)
     if mode=='cpu':env['NORTHLIGHT_TEST_DISPLACEMENT_CPU']='1'
     cmd=list(map(str,[exes[variant,kind],dll,fixture/'native-vs.bin',fixture/'native-ps.bin',run,family,fp.COMPILED]));r=subprocess.run(cmd,cwd=run,env=env,capture_output=True,text=True,timeout=180);(run/'gpu.log').write_text(r.stdout+r.stderr);print(backend,kind,family,mode,variant,r.returncode,flush=True);r.check_returncode()
     if variant=='candidate' and family!='wmo':assert 'NATIVE_VERTEX_REUSE_PASS' in r.stdout
     if variant=='candidate' and args.sampler_bulk_mode is not None:assert 'NATIVE_SAMPLER_READ_PASS' in r.stdout
     digests.append(digest.read_bytes());results.append({'backend':backend,'kind':kind,'family':family,'mode':mode,'variant':variant,'readbacks':len(digests[-1].splitlines()),'digest_sha256':sha(digest),'reuse_controls':r.stdout.count('NATIVE_VERTEX_REUSE_PASS'),'pixel_reuse_controls':r.stdout.count('NATIVE_PIXEL_REUSE_PASS'),'log':str(run/'gpu.log')})
    assert digests[0]==digests[1],f'Image mismatch: {backend}/{kind}/{family}/{mode}'
    for p in runs[0].glob('*.fgr'):assert p.read_bytes()==(runs[1]/p.name).read_bytes();fgrPairs+=1
for (variant,_),exe in exes.items():assert sha(exe)==receipts[variant]['executables_sha256'][str(exe)]
(out/'validation.json').write_text(json.dumps({'pass':True,'candidate_receipt_sha256':sha(args.candidate_receipt),'baseline_receipt_sha256':sha(args.baseline_receipt),'candidate_receipt':str(args.candidate_receipt.resolve()),'baseline_receipt':str(args.baseline_receipt.resolve()),'runner_sha256':sha(__file__),'backends_sha256':{str(p):sha(p) for p in backends.values()},'results':results,'raw_fgr_pairs':fgrPairs,'full_readbacks_bitexact':True,'frozen_executables_not_future_source_head':True,'game_launched':False,'timing_claim':False},indent=2)+'\n');print('NATIVE_VERTEX_REUSE_MATRIX_PASS',len(results),fgrPairs,flush=True)
