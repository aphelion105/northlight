# northlight-test: requires=windows-exe,zig
"""Snapshot copies and declaration lookup: exact ownership, failures and optional same-input timing."""
import argparse,ast,hashlib,io,json,re,statistics,subprocess,sys,tarfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

p=argparse.ArgumentParser(__doc__);p.add_argument('--baseline-ref');p.add_argument('--benchmark',action='store_true');args=p.parse_args()
out=fp.output_dir()/'pipeline-capture';out.mkdir(parents=True,exist_ok=True)
def literal(name,variable):
    return next(ast.literal_eval(n.value) for n in ast.parse((fp.TESTS/name).read_text()).body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id==variable for t in n.targets))
stub=literal('test_terrain_snapshot.py','stub')
stub=stub[:stub.index('using UINT')]+'#include <windows.h>\nconstexpr HRESULT D3D_OK=0,D3DERR_INVALIDCALL=HRESULT(0x8876086cu);\n'+stub[stub.index('constexpr unsigned D3DUSAGE'):]
(out/'d3d9.h').write_text(stub)
harness=literal('test_draw_snapshot.py','harness').replace('int main(){','void originalSnapshotTests(){',1)
harness=harness.replace('++locks;*out=','++locks;++activeLocks;*out=').replace('++unlocks;return failUnlock','++unlocks;assert(activeLocks>0);--activeLocks;return failUnlock')
(out/'pipeline_capture_fixture.h').write_text('static unsigned activeLocks=0;\n'+harness)
declarations=(fp.TESTS/'test_render_state_caches.cpp').read_text().split('struct IDirect3DVertexBuffer9')[0]
declarations=declarations.replace('int refs=1,reads=0;', 'unsigned layoutOffset=0;int refs=1,reads=0;').replace('e[0]={};','e[0]={};e[0].Offset=uint16_t(layoutOffset);')
(out/'pipeline_declaration_fixture.h').write_text(declarations+'\n#include "vertex_declaration_cache.h"\n')
headers=[path for path in fp.SRC.rglob('*') if path.is_file()]
inputs=[*headers,Path(__file__),fp.REPO/'northlight_paths.py',fp.TESTS/'pipeline_capture_buffers.cpp',fp.TESTS/'pipeline_declaration_cache.cpp',fp.TESTS/'test_draw_snapshot.py',fp.TESTS/'test_terrain_snapshot.py',fp.TESTS/'test_render_state_caches.cpp']
sha=lambda path:hashlib.sha256(Path(path).read_bytes()).hexdigest()
watched={str(path):sha(path) for path in inputs};variants=[('current',None)];provenance=None
if args.baseline_ref:
    provenance=subprocess.check_output(['git','rev-parse',args.baseline_ref],cwd=fp.REPO,text=True).strip()
    archive=subprocess.check_output(['git','archive',provenance,'src'],cwd=fp.REPO);frozen=out/'baseline';frozen.mkdir(exist_ok=True)
    with tarfile.open(fileobj=io.BytesIO(archive)) as source:
        for member in source:
            if member.isfile() and Path(member.name).suffix in ('.h','.inl'):(frozen/Path(member.name).name).write_bytes(source.extractfile(member).read())
    variants.insert(0,('baseline',frozen))
executables={};runs=[]
for name,folder in variants:
    for mode in (['O2','O0'] if name=='current' else ['O2']):
        for kind,source in [('capture','pipeline_capture_buffers.cpp'),('declaration','pipeline_declaration_cache.cpp')]:
            exe=out/(name+'-'+kind+'-'+mode+'.exe')
            command=[str(fp.zig()),'c++','-target','x86-windows-gnu','-'+mode,'-UNDEBUG','-std=c++17']
            if folder:command+=['-I',str(folder)]
            command+=['-I',str(out),*fp.test_include_flags(),str(fp.TESTS/source),'-o',str(exe)]
            with (out/(exe.stem+'-build.log')).open('w') as log:subprocess.run(command,env=fp.zig_env(),stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300)
            result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=180)
            (out/(exe.stem+'.log')).write_text(result.stdout+result.stderr);print(name,mode,result.stdout+result.stderr,flush=True);result.check_returncode()
            runs.append({'variant':name,'mode':mode,'kind':kind,'output':result.stdout});executables[name,kind]=exe if mode=='O2' else executables[name,kind]
rows=[];summary={}
if args.benchmark:
    assert args.baseline_ref,'A frozen baseline is required for timing comparisons'
    for kind,modes in [('capture',['buffered-cold','up-cold','buffered-warm','up-warm','buffered-growing','up-growing','indexed-growing']),('declaration',['1','8','32','128','129'])]:
        for stage in modes:
            for pair in range(8):
                for name in (['baseline','current'] if pair%2==0 else ['current','baseline']):
                    output=subprocess.check_output([str(executables[name,kind]),stage],text=True,timeout=120)
                    row=dict(re.findall(r'(\w+)=([^\s]+)',output));row.update(variant=name,pair=pair);rows.append(row)
            samples=[row for row in rows if row['stage']==('declaration-'+stage if kind=='declaration' else stage) and row['pair']>0]
            for field in ['digest',*(['reads'] if kind=='declaration' else ['allocations'])]:assert len({row[field] for row in samples})==1,(stage,field,samples)
            medians={name:statistics.median(float(row['cpuMs']) for row in samples if row['variant']==name) for name in ['baseline','current']}
            medians['speedup']=medians['baseline']/medians['current'];summary[kind+'-'+stage]=medians;print(kind,stage,medians,flush=True)
assert all(sha(path)==digest for path,digest in watched.items()),'Inputs changed'
(out/'validation.json').write_text(json.dumps({'pass':True,'baseline':provenance,'inputs':watched,'runs':runs,'timing':summary,'rows':rows,'game_launched':False,'scope':'Exact snapshot bytes, padding, draw budgets, lock/ref balance, failure recovery and declaration LRU. Optional timings are synthetic CPU components, not whole capture or game FPS.'},indent=2)+'\n')
