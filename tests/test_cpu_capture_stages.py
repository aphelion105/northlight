# northlight-test: requires=windows-exe,zig,client,stormlib
"""Exact source aliases, current-pose certificates and mixed-owner frame maintenance."""
import argparse, ast, hashlib, io, json, re, statistics, subprocess, sys, tarfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
import client_fixtures
import replay_gpu_fixture

p=argparse.ArgumentParser(__doc__);p.add_argument('--baseline-ref');p.add_argument('--pairs',type=int,default=7);args=p.parse_args()
out=fp.output_dir()/'cpu-capture-stages';out.mkdir(parents=True,exist_ok=True)
prefix=(fp.TESTS/'displacement_state_reuse.cpp').read_text().split('struct DisplacementStateTestAccess {')[0]
tree=ast.parse((fp.TESTS/'test_replay_skin_safety.py').read_text())
harness=next(ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='harness' for t in n.targets)).split('int main(')[0]
(out/'cpu_material_fixture.h').write_text(prefix+harness)
program=client_fixtures.four_bone_vs3()
stub=replay_gpu_fixture.stub;first=stub.index('using UINT');last=stub.index('constexpr unsigned D3DUSAGE')
stub=stub[:first]+'#include <windows.h>\nconstexpr HRESULT D3D_OK=0,D3DERR_INVALIDCALL=HRESULT(0x8876086cu);\n'+stub[last:]
native=out/'native';native.mkdir(exist_ok=True);(native/'d3d9.h').write_text(stub);(native/'replay_fixture.h').write_text(replay_gpu_fixture.fixture)
(native/'cpu_native_fixture.h').write_text((fp.TESTS/'test_canonical_gpu_scene.cpp').read_text().replace('int main(', 'int cpu_canonical_main(', 1))
headers=['auto_displacement_renderer.h','replay_bounds.h','replay_envelopes.inl','scene_native_store.h']
watched=[Path(__file__),fp.TESTS/'cpu_capture_material.cpp',fp.TESTS/'cpu_capture_boundary.cpp',fp.TESTS/'displacement_state_reuse.cpp',fp.TESTS/'test_replay_skin_safety.py',fp.TESTS/'test_canonical_gpu_scene.cpp',fp.TESTS/'test_shared_gpu_residency.cpp',fp.TESTS/'replay_gpu_fixture.py',*[fp.src(n) for n in headers]]
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
inputs={str(f):sha(f) for f in watched};executables={};rows=[]
variants=[('current',None)]
if args.baseline_ref:
    frozen=out/'baseline';frozen.mkdir(exist_ok=True)
    archive=subprocess.check_output(['git','archive',args.baseline_ref,'src'],cwd=Path(__file__).resolve().parents[1])
    with tarfile.open(fileobj=io.BytesIO(archive)) as source:
        for member in source:
            if member.isfile() and Path(member.name).suffix in ('.h','.inl'):
                target=frozen/Path(member.name).name
                target.write_bytes(source.extractfile(member).read())
    variants.insert(0,('baseline',frozen))
for name,folder in variants:
    for opt in (['O2','O0'] if name=='current' else ['O2']):
        for kind in ['material','boundary']:
            exe=out/(name+'-'+kind+'-'+opt+'.exe')
            cmd=[str(fp.zig()),'c++','-target','x86-windows-gnu','-'+opt,'-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec']
            if folder:cmd+=['-I',str(folder)]
            if name=='current' or 'certifiedPoseHits' in (folder/'replay_envelopes.inl').read_text():cmd+=['-DNORTHLIGHT_CPU_CERTIFICATES=1']
            if name=='current':cmd+=['-DNORTHLIGHT_CAPTURE_ACCELERATION=1']
            cmd+=['-I',str(native if kind=='boundary' else out),*fp.test_include_flags(),str(fp.TESTS/('cpu_capture_'+kind+'.cpp')),'-luser32','-luuid','-o',str(exe)]
            with (out/(exe.stem+'-build.log')).open('w') as log:subprocess.run(cmd,env=fp.zig_env(),stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300)
            executables[name,kind,opt]=exe
        for stage,kind in [('aliases','material'),('poses','material'),('pose-bases','material'),('pose-rewrites','material'),('boundary','boundary')]:
            command=[str(executables[name,kind,opt])]+([stage,str(program)] if kind=='material' else [])
            run=subprocess.run(command,capture_output=True,text=True,timeout=180)
            (out/(name+'-'+stage+'-'+opt+'.log')).write_text(run.stdout+run.stderr)
            print(name,opt,(run.stdout+run.stderr).strip(),flush=True);run.check_returncode()
summary={}
if args.baseline_ref:
    for stage,kind in [('aliases','material'),('poses','material'),('pose-bases','material'),('pose-rewrites','material'),('boundary','boundary')]:
        for pair in range(args.pairs+1):
            for name in (['baseline','current'] if pair%2==0 else ['current','baseline']):
                command=[str(executables[name,kind,'O2'])]+([stage,str(program)] if kind=='material' else [])
                text=subprocess.check_output(command,text=True,timeout=180)
                row=dict(re.findall(r'(\w+)=([^\s]+)',text));row.update(name=name,pair=pair-1);rows.append(row)
        samples=[r for r in rows if r['stage']==stage and r['pair']>=0]
        assert len({r['digest'] for r in samples})==1
        medians={name:statistics.median(float(r['cpuMs']) for r in samples if r['name']==name) for name in ['baseline','current']}
        summary[stage]={**medians,'speedup':medians['baseline']/medians['current']}
assert all(sha(Path(path))==value for path,value in inputs.items()),'Inputs changed during run'
(out/'validation.json').write_text(json.dumps({'pass':True,'inputs':inputs,'program_sha256':sha(program),'baseline_ref':args.baseline_ref,'rows':rows,'summary':summary,'scope':'CPU components only; aliases, eight repeated current-pose consumers, and 6000 certified plus two legacy cache entries. No game FPS measurement.'},indent=2)+'\n')
print(json.dumps(summary,indent=2))
