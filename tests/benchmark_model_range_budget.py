# northlight-test: requires=windows-exe,zig manual timing
"""Validate the range cache's cold-work budget and compare warm CPU cost."""
from pathlib import Path
import argparse,hashlib,json,re,statistics,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT),str(ROOT/'scripts')]
import northlight_paths as fp
from build_renderer import source_identity
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--reference-header',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
identity=source_identity();digest=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
inputs=[Path(__file__),fp.TESTS/'model_normal_range_budget.cpp',fp.src('model_normal_range.h'),a.reference_header.resolve()]
hashes={str(path):digest(path) for path in inputs}
before=a.reference_header.read_text(encoding='utf-8').replace('NorthlightModelNormalRange','ReferenceModelNormalRange')
(out/'range_reference.h').write_text(before,encoding='utf-8',newline='\n')
current=fp.src('model_normal_range.h').read_text(encoding='utf-8')
(out/'clocked_range.h').write_text(current.replace('NorthlightModelNormalRange','ClockedModelNormalRange').replace('std::chrono::steady_clock','RangeBudgetClock'),encoding='utf-8',newline='\n')
report={'pass':False,'source_identity':identity,'inputs':hashes,'runs':[],'timing':{}}
try:
    for mode in ('O0','O2'):
        exe=out/(mode+'.exe');command=[fp.zig(),'c++','-target','x86-windows-gnu','-std=c++17','-'+mode,'-UNDEBUG','-I',out,*fp.test_include_flags(),fp.TESTS/'model_normal_range_budget.cpp','-o',exe]
        with (out/(mode+'-compile.log')).open('w') as log:subprocess.run(list(map(str,command)),env=fp.zig_env(),stdout=log,stderr=subprocess.STDOUT,check=True)
        run=subprocess.run([str(exe),*(['timing'] if mode=='O2' else [])],capture_output=True,text=True,timeout=180)
        (out/(mode+'.log')).write_text(run.stdout+run.stderr,encoding='utf-8');run.check_returncode();assert 'RANGE_BUDGET_PASS' in run.stdout
        report['runs'].append({'mode':mode,'exe_sha256':digest(exe),'result':run.stdout.splitlines()[0]})
        if mode=='O2':
            rows=[dict(re.findall(r'(\w+)=([^ ]+)',line)) for line in run.stdout.splitlines() if line.startswith('RANGE_BUDGET_TIMING ') and 'warm=0 ' in line]
            for side,name in ((0,'before'),(1,'current')):
                values=[float(r['ns']) for r in rows if int(r['mode'])==side];assert len(values)==6;report['timing'][name+'_ns']=statistics.median(values)
    assert source_identity()==identity and all(digest(Path(path))==sha for path,sha in hashes.items())
    report['pass']=True
finally:(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8',newline='\n')
print(json.dumps(report,indent=2))
