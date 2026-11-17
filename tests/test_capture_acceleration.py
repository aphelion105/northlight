# northlight-test: requires=windows-exe,zig timing
"""Exact subdivision GPU leases and same-draw range-bank handoff; optional frozen-header A/B."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import statistics
import subprocess
import sys
import tarfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

p=argparse.ArgumentParser(__doc__);p.add_argument('--baseline-ref');p.add_argument('--pairs',type=int,default=7);args=p.parse_args()
out=fp.output_dir()/'capture-acceleration';out.mkdir(parents=True,exist_ok=True)
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
watched=[*fp.SRC.rglob('*.h'),*fp.SRC.rglob('*.inl'),Path(__file__),fp.TESTS/'capture_acceleration.cpp',fp.TESTS/'displacement_state_reuse.cpp',fp.TESTS/'displacement_subdivision.cpp',fp.TESTS/'test_replay_capture_constants.cpp']
inputs={str(f):sha(f) for f in watched};variants=[('current',None)];executables={};runs=[]
if args.baseline_ref:
    frozen=out/'baseline';frozen.mkdir(exist_ok=True)
    with tarfile.open(fileobj=io.BytesIO(subprocess.check_output(['git','archive',args.baseline_ref,'src']))) as archive:
        for member in archive:
            if member.isfile() and Path(member.name).suffix in ('.h','.inl'):(frozen/Path(member.name).name).write_bytes(archive.extractfile(member).read())
    variants.insert(0,('baseline',frozen))
for name,folder in variants:
    for opt in (['O2','O0'] if name=='current' else ['O2']):
        exe=out/(name+'-'+opt+'.exe');cmd=[str(fp.zig()),'c++','-target','x86-windows-gnu','-'+opt,'-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec']
        if folder:cmd+=['-I',str(folder)]
        if name=='current':cmd+=['-DNORTHLIGHT_CAPTURE_ACCELERATION=1']
        cmd+=[*fp.test_include_flags(),str(fp.TESTS/'capture_acceleration.cpp'),'-luser32','-luuid','-o',str(exe)]
        with (out/(exe.stem+'-build.log')).open('w') as log:subprocess.run(cmd,env=fp.zig_env(),stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300)
        r=subprocess.run([str(exe)],text=True,capture_output=True,timeout=120);(out/(exe.stem+'.log')).write_text(r.stdout+r.stderr);print(name,opt,r.stdout+r.stderr,flush=True);r.check_returncode();executables[name,opt]=exe
        if name=='current':
            constants=out/('replay-constants-'+opt+'.exe')
            subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-'+opt,'-UNDEBUG','-std=c++17',*fp.test_include_flags(),str(fp.TESTS/'test_replay_capture_constants.cpp'),'-o',str(constants)],env=fp.zig_env(),check=True)
            subprocess.run([str(constants)],check=True)
summary={}
if args.baseline_ref:
    for pair in range(args.pairs+1):
        for name in (['baseline','current'] if pair%2==0 else ['current','baseline']):
            result=subprocess.check_output([str(executables[name,'O2']),'--benchmark'],text=True,timeout=180)
            for line in result.splitlines():
                if line.startswith('CAPTURE_ACCEL '):runs.append({**dict(re.findall(r'(\w+)=([^\s]+)',line)),'name':name,'pair':pair-1})
    for stage in ['subdivision','constants']:
        rows=[r for r in runs if r['stage']==stage and r['pair']>=0];assert len({r['digest'] for r in rows})==1
        values={name:statistics.median(float(r['cpuMs']) for r in rows if r['name']==name) for name in ['baseline','current']}
        summary[stage]={**values,'speedup':values['baseline']/values['current']}
assert all(sha(Path(path))==value for path,value in inputs.items()),'Inputs changed during run'
(out/'validation.json').write_text(json.dumps({'pass':True,'inputs':inputs,'baseline_ref':args.baseline_ref,'runs':runs,'summary':summary,'scope':'Synthetic CPU build/copy versus existing immutable VB/IB lease reuse, plus current material constant capture versus ownership transfer of the already captured range bank. No hardware GPU interval or game FPS measurement.'},indent=2)+'\n')
print(json.dumps(summary,indent=2))
