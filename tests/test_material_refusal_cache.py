# northlight-test: requires=windows-exe,zig
"""Colliding negative-cache keys must not spend eight cold turns on two textures."""
import argparse,hashlib,json,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

p=argparse.ArgumentParser(__doc__);p.add_argument('--baseline-ref');args=p.parse_args()
out=fp.output_dir()/'material-refusal';out.mkdir(parents=True,exist_ok=True)
prefix=(fp.TESTS/'auto_normal_index.cpp').read_text().split('template<class T,class=void>struct Indexed')[0]
prefix=prefix.replace('#include "cache_linear.h"','').replace('#include "cache_integrated.h"','')
if args.baseline_ref:prefix+='\n#include "cache_baseline.h"\n'
(out/'refusal_fixture.h').write_text(prefix)
def exposed(source,name):
    source='\n'.join(line for line in source.splitlines() if not line.startswith('#include '))+'\n'
    return source.replace('class NorthlightAutoNormalCache {','class NorthlightAutoNormalCache {public:',1).replace('NorthlightAutoNormalCache',name)
(out/'cache_current.h').write_text(exposed(fp.src('auto_normal_cache.h').read_text(),'CacheCurrent'))
baseline=None
if args.baseline_ref:
    baseline=subprocess.check_output(['git','rev-parse',args.baseline_ref],cwd=fp.REPO,text=True).strip()
    old=subprocess.check_output(['git','show',baseline+':src/water/auto_normal_cache.h'],cwd=fp.REPO,text=True)
    (out/'cache_baseline.h').write_text(exposed(old,'CacheBaseline'))
watched=[*fp.SRC.rglob('*.h'),*fp.SRC.rglob('*.inl'),Path(__file__),fp.TESTS/'material_refusal_cache.cpp',fp.TESTS/'auto_normal_index.cpp',fp.REPO/'northlight_paths.py']
sha=lambda path:hashlib.sha256(Path(path).read_bytes()).hexdigest();inputs={str(path):sha(path) for path in watched};runs=[]
for mode in ['O2','O0']:
    exe=out/('refusal-'+mode+'.exe');command=[str(fp.zig()),'c++','-target','x86-windows-gnu','-'+mode,'-UNDEBUG','-std=c++17']
    if baseline:command+=['-DNORTHLIGHT_REFUSAL_BASELINE=1']
    command+=['-I',str(out),*fp.test_include_flags(),str(fp.TESTS/'material_refusal_cache.cpp'),'-o',str(exe)]
    with (out/(mode+'-build.log')).open('w') as log:subprocess.run(command,env=fp.zig_env(),stdout=log,stderr=subprocess.STDOUT,check=True,timeout=180)
    result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=120);(out/(mode+'.log')).write_text(result.stdout+result.stderr);print(result.stdout+result.stderr,flush=True);result.check_returncode()
    assert 'MATERIAL_REFUSAL_PASS' in result.stdout;runs.append({'mode':mode,'output':result.stdout})
assert all(sha(path)==value for path,value in inputs.items()),'Inputs changed during validation'
(out/'validation.json').write_text(json.dumps({'pass':True,'baseline':baseline,'inputs':inputs,'runs':runs,'game_launched':False,'scope':'Actual resolve with address collisions, exact refusal keys, COM leases, eight-attempt cap and frame/config/reset retries. Fake-device work counts, not gameplay FPS.'},indent=2)+'\n')
