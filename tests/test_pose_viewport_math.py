# northlight-test: requires=windows-exe,zig
"""Compare projection proof math with r107, including cancellation and rounding modes."""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess,sys
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT));import northlight_paths as fp
p=argparse.ArgumentParser(__doc__);p.add_argument('--output',type=Path);p.add_argument('--native',action='store_true');p.add_argument('--benchmark',action='store_true');a=p.parse_args()
out=(a.output or fp.output_dir()).resolve();out.mkdir(parents=True,exist_ok=True)
header=fp.src('pose_bounds_handoff.h');before=subprocess.check_output(['git','show','b2385af0:src/replay/pose_bounds_handoff.h'],cwd=ROOT)
for name,raw in [('current',header.read_bytes()),('before',before)]:
    text=raw.decode().replace('#include "replay_bounds.h"','#include "pose_math_types.h"').replace('#include "captured_constant_epoch.h"','')
    if name=='before':text=text.replace('namespace NorthlightPoseBounds {','namespace BeforePoseBounds {',1)
    (out/(name+'.h')).write_text(text,encoding='utf-8',newline='\n')
(out/'pose_math_types.h').write_text('#pragma once\n#include <algorithm>\n#include <cmath>\n#include <limits>\n#include <cstdint>\n#include "captured_constant_epoch.h"\nnamespace NorthlightReplayBounds {struct Bounds {float low[3]={},high[3]={};bool valid=false;};}\n',encoding='utf-8',newline='\n')
files=[header,fp.src('captured_constant_epoch.h'),ROOT/'tests/pose_viewport_math.cpp',Path(__file__),out/'before.h',out/'current.h',out/'pose_math_types.h']
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();inputs={str(p):sha(p) for p in files};runs=[]
builds=[('O2',['-O2']),('sanitized',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all'])] if a.native else [('O2',['-O2']),('O0',['-O0'])]
for name,flags in builds:
    exe=out/(name+('' if a.native else '.exe'));cmd=(['clang++'] if a.native else [str(fp.zig()),'c++','-target','x86-windows-gnu'])+['-std=c++17','-UNDEBUG',*flags,'-I'+str(out),*fp.test_include_flags(),str(ROOT/'tests/pose_viewport_math.cpp'),'-o',str(exe)]
    subprocess.run(cmd,env=os.environ if a.native else fp.zig_env(),check=True)
    r=subprocess.run([str(exe),*(['timing'] if a.benchmark and name=='O2' else [])],capture_output=True,text=True,timeout=180);(out/(name+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8',newline='\n');r.check_returncode();assert 'POSE_VIEWPORT_MATH_PASS cases=480000' in r.stdout
    runs.append(dict(build=name,command=cmd,sha256=sha(exe),stdout=r.stdout));print(name,r.stdout.splitlines()[0],flush=True)
# A shorter rejection radius must fail the unchanged boundary oracle.
source=(out/'current.h').read_text(encoding='utf-8');needle='>48.01';assert source.count(needle)==1
(out/'current.h').write_text(source.replace(needle,'>48.',1),encoding='utf-8',newline='\n')
exe=out/('unsafe-radius'+('' if a.native else '.exe'));cmd=(['clang++'] if a.native else [str(fp.zig()),'c++','-target','x86-windows-gnu'])+['-std=c++17','-O2','-UNDEBUG','-I'+str(out),*fp.test_include_flags(),str(ROOT/'tests/pose_viewport_math.cpp'),'-o',str(exe)]
subprocess.run(cmd,env=os.environ if a.native else fp.zig_env(),check=True);r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=180)
(out/'unsafe-radius.log').write_text(r.stdout+r.stderr,encoding='utf-8',newline='\n');assert r.returncode and re.search(r'[Aa]ssertion.*failed',r.stderr),r.stderr
(out/'current.h').write_text(source,encoding='utf-8',newline='\n');assert all(sha(Path(p))==h for p,h in inputs.items())
(out/'validation.json').write_text(json.dumps(dict(pass_=True,reference='b2385af0',inputs=inputs,runs=runs,negative_control=dict(exit_code=r.returncode,stderr=r.stderr),scope='Projection proof only; identical bounds/admission/consumer decisions; range rejection skips the unnecessary viewport flag, not whole capture or gameplay FPS.'),indent=2)+'\n',encoding='utf-8',newline='\n')
print('POSE_VIEWPORT_MATH_VALIDATED negative_control=unsafe-radius',flush=True)
