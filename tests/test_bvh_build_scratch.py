#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig,client manual
"""Identical SAH tree and ray results, optional scratch refusal/OOM, x86 build timing."""
import argparse,hashlib,json,re,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]));import northlight_paths as fp
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--reference',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--real-client',action='store_true');a=p.parse_args()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
paths=[fp.src('world_gi.cpp'),fp.src('world_gi.h'),fp.TESTS/'bvh_build_scratch.cpp',Path(__file__)];inputs={str(p):sha(p) for p in paths};executables={}
for mode in ('reference','candidate'):
    folder=out/mode;folder.mkdir(exist_ok=True)
    # Keep quote-includes in GI helpers on this same frozen header copy.
    for path in fp.src('world_gi.h').parent.glob('*.h'):(folder/path.name).write_bytes(path.read_bytes())
    for name in ('world_gi.cpp','world_gi.h'):
        source=(a.reference/name if mode=='reference' else fp.src(name)).read_text()
        if name.endswith('.h'):source=source.replace('friend class LocalSceneCache;','friend class LocalSceneCache;friend struct BvhBuildOracle;')
        (folder/name).write_text(source,encoding='utf-8',newline='\n')
    exe=folder/'bvh-build.exe';executables[mode]=exe
    cmd=[str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-I'+str(folder),*fp.test_include_flags(),str(fp.TESTS/'bvh_build_scratch.cpp'),str(folder/'world_gi.cpp'),'-o',str(exe)]
    r=subprocess.run(cmd,env=fp.zig_env(),capture_output=True,text=True);(folder/'compile.log').write_text(r.stdout+r.stderr);r.check_returncode()
records=[]
def run(mode,kind,policy):
    cmd=[str(executables[mode]),str(kind),policy,*([str(fp.client_root())] if kind==5 else [])]
    r=subprocess.run(cmd,capture_output=True,text=True,timeout=120);r.check_returncode();assert 'BVH_BUILD_PASS' in r.stdout
    record=dict(re.findall(r'(\w+)=([^ ]+)',r.stdout.strip()));record.update(binary=mode,stdout=r.stdout.strip());records.append(record);print(r.stdout.strip(),mode,flush=True);return record
for kind in (*range(5),*([5] if a.real_client else [])):
    reference=run('reference',kind,'allow');candidate=run('candidate',kind,'allow')
    for record in (candidate,run('candidate',kind,'deny'),run('candidate',kind,'oom')):
        assert all(reference[k]==record[k] for k in ('tree','rays','ownBytes','triangles')),(reference,record)
    if kind!=3:
        assert records[-2]['refused']=='1' and records[-1]['oom']=='1'
    for mode in ('reference','candidate'):
        median=run(mode,kind,'median')
        if mode=='reference':control=median
        else:assert all(control[k]==median[k] for k in ('tree','rays','ownBytes'))
for kind in (4,*([5] if a.real_client else [])):
    for mode in ('reference','candidate','candidate','reference')*2:run(mode,kind,'allow')
assert all(sha(Path(p))==h for p,h in inputs.items())
(out/'validation.json').write_text(json.dumps({'pass':True,'inputs':inputs,'records':records,'timing_scope':'x86 CPU build only; concurrent game can affect time. Identical full tree bytes and 12000 traced+occlusion rays per case.'},indent=2)+'\n',encoding='utf-8')
