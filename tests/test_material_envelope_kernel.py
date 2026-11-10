#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig,client,stormlib
"""Skin-bound equivalence, invalid inputs and shared prepared-key lifetime."""
import argparse,hashlib,json,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
import client_fixtures
p=argparse.ArgumentParser(__doc__);p.add_argument('--output',type=Path);p.add_argument('--reference',type=Path);p.add_argument('--kernel-only',action='store_true',help='Compare the skin evaluator without the older prepared-key allocation counterfactual');a=p.parse_args()
out=(a.output or fp.output_dir()/'material-envelope-kernel').resolve();out.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
inputs={str(q):sha(q) for q in [Path(__file__),fp.TESTS/'skin_envelope_kernel.cpp',fp.TESTS/'envelope_key_sharing.cpp',fp.src('replay_skin_envelope.inl'),fp.src('replay_envelopes.inl')]}
reference=[]
if a.reference:
 folder=a.reference.resolve();reference=['-DNORTHLIGHT_SKIN_REFERENCE="'+str(folder/'replay_bounds.h').replace('\\','/')+'"']
 for name in ['replay_bounds.h','replay_skin_envelope.inl','replay_envelopes.inl']:inputs[str(folder/name)]=sha(folder/name)
fixture=client_fixtures.four_bone_vs3();runs=[]
for tag,flags in [('O2',['-O2']),('ASan-UBSan',['-O2','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all'])]:
 for name in (['skin_envelope_kernel'] if a.kernel_only else ['skin_envelope_kernel','envelope_key_sharing']):
  exe=out/(name+'-'+tag+'.exe');command=[str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-UNDEBUG',*flags,*reference,*fp.test_include_flags(),str(fp.TESTS/(name+'.cpp')),'-o',str(exe)]
  print('COMPILE',name,tag,flush=True);subprocess.run(command,env=fp.zig_env(),check=True)
  args=[str(exe)]+([str(fixture)] if name=='envelope_key_sharing' else [])
  result=subprocess.run(args,capture_output=True,text=True,timeout=180);log=out/(name+'-'+tag+'.log');log.write_text(result.stdout+result.stderr,encoding='utf-8');print(result.stdout+result.stderr,flush=True);result.check_returncode()
  assert ('SKIN_KERNEL_EXACT_PASS' if name=='skin_envelope_kernel' else 'ENVELOPE_SHARED_KEY_PASS') in result.stdout
  runs.append({'name':name,'tag':tag,'command':command,'log':str(log),'sha256':sha(log)})
assert all(sha(q)==h for q,h in inputs.items())
(out/'validation.json').write_text(json.dumps({'pass':True,'inputs':inputs,'runs':runs,'GPU_used':False,'timing_scope':'Component only; frozen envelope clock isolates exact-work comparison from budget scheduling.'},indent=2)+'\n',encoding='utf-8')
