#!/usr/bin/env python3
# northlight-test: requires=cxx,client,stormlib timing manual
"""QA first-person warm-heavy starvation: exact old/new current-pose bounds."""
import argparse, ast, hashlib, json, os, subprocess, sys, tempfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
sys.path.insert(0,str(fp.TESTS))
import client_fixtures
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--baseline',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
files=[*[p for p in fp.SRC.rglob('*') if p.is_file()],Path(__file__),fp.TESTS/'replay_single_bone_cheap.cpp']
headers=('replay_bounds.h','replay_bounds_job.h','replay_envelopes.inl','replay_skin_envelope.inl')
files.extend(a.baseline/h for h in headers);inputs={str(p):sha(p) for p in files}
def literal(path,name):return next(ast.literal_eval(n.value) for n in ast.parse(path.read_text()).body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id==name for t in n.targets))
runs=[]
with tempfile.TemporaryDirectory(prefix='single-bone-cheap-',dir=fp.REPO/'out') as tmp:
 root=Path(tmp);(root/'d3d9.h').write_text(literal(fp.TESTS/'test_terrain_snapshot.py','stub'));client_fixtures.actor_client_programs(root)
 for name,flags in [('O2',['-O2']),('sanitized',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all']),('reference',['-O2','-DNORTHLIGHT_CHEAP_REFERENCE'])]:
  folder=root/name;folder.mkdir()
  if name=='reference':
   for h in headers:(folder/h).write_bytes((a.baseline/h).read_bytes())
  binary=folder/'check';compiler=[fp.zig(),'c++','-target','x86_64-linux-musl'] if os.name=='nt' else ['clang++']
  subprocess.run(list(map(str,[*compiler,'-std=c++17','-Wall','-Wextra','-Werror',*flags,'-I',folder,'-I',root,*fp.test_include_flags(),fp.TESTS/'replay_single_bone_cheap.cpp','-o',binary])),check=True,env=fp.zig_env())
  cmd=['wsl','--exec','/mnt/host/'+binary.drive[0].lower()+'/'+binary.relative_to(binary.anchor).as_posix()] if os.name=='nt' else [str(binary)]
  r=subprocess.run(cmd,capture_output=True,text=True);print(name,r.stdout+r.stderr,flush=True);r.check_returncode();runs.append({'name':name,'stdout':r.stdout})
oracles=[[s for s in run['stdout'].splitlines() if s.startswith(('SINGLE_BONE_ORACLE ', 'GROUP_ORACLE ', 'GROUP_EXTRA_ORACLE ', 'WARM_GROUP_ORACLE '))] for run in runs];assert oracles[0]==oracles[1]==oracles[2]
assert all(sha(Path(p))==h for p,h in inputs.items())
a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps({'pass':True,'inputs':inputs,'runs':runs,'scope':'Authored QA-shaped wave with deterministic clock; exact old/current bounds, not gameplay FPS.'},indent=2)+'\n',encoding='utf-8',newline='\n')
