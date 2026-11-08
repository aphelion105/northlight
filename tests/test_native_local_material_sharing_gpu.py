#!/usr/bin/env python3
# northlight-test: requires=client,zig,windows-gpu manual
"""Native material sharing: exact captured readbacks, aliases and device fallback."""
from pathlib import Path
import argparse, hashlib, json, os, shutil, subprocess, sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,required=True);ap.add_argument('--baseline',type=Path,required=True);ap.add_argument('--allow-game-process',action='store_true');args=ap.parse_args()
if not args.allow_game_process:
    processes=subprocess.check_output(['tasklist','/FO','CSV'],text=True).lower()
    assert 'wow.exe' not in processes,'Use --allow-game-process for isolated correctness while the client is open'
out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
baseline=args.baseline.resolve();override=out/'duplicate-headers';override.mkdir(exist_ok=True)
for name in ['native_local_light_patch','native_local_light_renderer']:shutil.copyfile(baseline/f'{name}.before.h',override/f'{name}.h')
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
files=[fp.src('native_local_light_patch.h'),fp.src('native_local_light_renderer.h'),Path(__file__),fp.TESTS/'native_local_sharing_checks.h',*[fp.TESTS/f'native_local_{kind}_gpu.cpp' for kind in ['light','pom','displacement','material_sharing']]]
# Include all production headers in the guard; actual composition depends on POM/displacement helpers.
files+=list((fp.REPO/'src').rglob('*.h'))
guard={str(p):sha(p) for p in set(files)}
backends={'native':Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll',**{name:fp.client_root()/f'renderer-backends/{name}/{name}_d3d9.dll' for name in ['dxvk','dxvk2']}}
executables={};commands=[]
for kind in ['light','pom','displacement','material_sharing']:
 for variant in (['candidate'] if kind=='material_sharing' else ['candidate','duplicate']):
  exe=out/f'{kind}-{variant}.exe';extra=['-I',str(override),'-DNORTHLIGHT_NATIVE_DUPLICATE_BASELINE'] if variant=='duplicate' else []
  cmd=list(map(str,[fp.zig(),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-MMD','-MF',exe.with_suffix('.d'),*extra,*fp.test_include_flags(),fp.TESTS/f'native_local_{kind}_gpu.cpp','-luser32','-o',exe]));commands.append(cmd)
  with (out/f'{kind}-{variant}-compile.log').open('w') as log:subprocess.run(cmd,env=fp.zig_env(),stdout=log,stderr=subprocess.STDOUT,check=True,timeout=180)
  executables[kind,variant]=exe
results=[]
for backend,dll in backends.items():
 for kind in ['material_sharing','light','pom','displacement']:
  families=['synthetic'] if kind=='material_sharing' else ['captured-m2'] if kind=='displacement' else ['m2','wmo']
  for family in families:
   modes=['gpu','cpu'] if kind=='displacement' else ['default']
   for mode in modes:
    digests=[]
    for variant in (['candidate'] if kind=='material_sharing' else ['candidate','duplicate']):
     run=out/backend/kind/family/mode/variant;run.mkdir(parents=True,exist_ok=True);digest=run/'readbacks.txt';digest.write_bytes(b'')
     env={**fp.zig_env(),'DXVK_LOG_PATH':str(run),'DISABLE_VULKAN_OBS_CAPTURE':'1','NORTHLIGHT_TEST_CORRECTNESS_ONLY':'1','NORTHLIGHT_TEST_SHARING_FAULTS':'1','NORTHLIGHT_NATIVE_IMAGE_DIGEST':str(digest)}
     if mode=='cpu':env['NORTHLIGHT_TEST_DISPLACEMENT_CPU']='1'
     cmd=[str(executables[kind,variant]),str(dll)]
     if kind!='material_sharing':
      fixture=fp.REPO/'out/event-rootcause-20261107-r14/lamp'/('current/captured-m2' if kind=='displacement' else f'native-regression/{family}')
      cmd+=list(map(str,[fixture/'native-vs.bin',fixture/'native-ps.bin',run,family,fp.COMPILED]))
     r=subprocess.run(cmd,cwd=run,env=env,capture_output=True,text=True,timeout=180);(run/'gpu.log').write_text(r.stdout+r.stderr);print(backend,kind,family,mode,variant,r.returncode,flush=True);r.check_returncode()
     digests.append(digest.read_bytes());results.append({'backend':backend,'kind':kind,'family':family,'mode':mode,'variant':variant,'readbacks':len(digests[-1].splitlines()),'digest_sha256':sha(digest),'log':str(run/'gpu.log')})
    if len(digests)==2:assert digests[0]==digests[1],f'Full-readback mismatch {backend}/{kind}/{family}/{mode}'
dependencies=set()
for exe in executables.values():
 text=exe.with_suffix('.d').read_text().replace('\\\n',' ')
 for path in guard:
  if path.replace('\\','/') in text.replace('\\','/'):dependencies.add(path)
assert str(fp.src('native_local_light_patch.h')) in dependencies
exceptions={p:{'before':s,'after':sha(p),'included_by_test':p in dependencies} for p,s in guard.items() if sha(p)!=s}
assert not any(item['included_by_test'] for item in exceptions.values()),'A compiled dependency changed during final matrix'

(out/'validation.json').write_text(json.dumps({'pass':True,'source_sha256':{p:s for p,s in guard.items() if p in dependencies or p in map(str,files[:8])},'unrelated_header_changes':exceptions,'dependency_files':[str(p.with_suffix('.d')) for p in executables.values()],'baseline_sha256':{str(p):sha(p) for p in override.glob('*.h')},'executables_sha256':{str(p):sha(p) for p in executables.values()},'backends_sha256':{str(p):sha(p) for p in backends.values()},'commands':commands,'results':results,'full_readbacks_bitexact':True,'timing_claim':False,'game_launched':False},indent=2)+'\n')
print('NATIVE_MATERIAL_SHARING_MATRIX_PASS',len(results),flush=True)
