# northlight-test: requires=windows-exe,zig slow
"""Frozen/current production Draw* and drawHook bodies: ordered calls, handoff and faults."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,sys
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT));import northlight_paths as fp
p=argparse.ArgumentParser(__doc__);p.add_argument('--reference',default='3fc6b718');p.add_argument('--output',type=Path);p.add_argument('--benchmark',action='store_true');a=p.parse_args()
out=a.output or fp.output_dir()/'draw-orchestration';out.mkdir(parents=True,exist_ok=True)
def member(text,signature):
 start=text.index(signature);opening=text.index('{',start);depth=1;end=opening+1
 while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[start:end]
def members(text):
 signatures=['template<class Draw> void captureModelNormals(', 'template<class Capture,class Draw> HRESULT drawHook(']
 signatures+=['HRESULT STDMETHODCALLTYPE '+name+'(' for name in ['DrawPrimitive','DrawIndexedPrimitive','DrawPrimitiveUP','DrawIndexedPrimitiveUP']]
 for optional in ['template<class Capture> void prepareDraw(', 'template<NorthlightWaterRenderer::UserPointer UserPointer,class Raw,class Draw> void captureGameDraw(']:
  if optional in text:signatures.insert(0,optional)
 return '\n'.join(member(text,s) for s in signatures)
source=fp.src('renderer.cpp');fixture=fp.TESTS/'draw_orchestration.cpp';inputs={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in [source,fixture,Path(__file__)]}
old=subprocess.check_output(['git','show',a.reference+':src/proxy/renderer.cpp'],cwd=ROOT).decode('utf-8');current=source.read_text(encoding='utf-8')
assert current.count('if(enabled)spellArgs=NorthlightSpellLayer::DrawArgs::')==4,'native aurora admission needs all four draw tuples even with particle/spell features off'
before=members(old);after=members(current)
header=out/'draw_orchestration_routes.h'
def write_routes(body):header.write_text('struct Reference:Base {\n'+before+'\n};\nstruct Current:Base {\n'+body+'\n};\n',encoding='utf-8',newline='\n')
write_routes(after)
runs=[]
for opt in ['O2'] if a.benchmark else ['O2','O0']:
 exe=out/(opt+'.exe');cmd=[fp.zig(),'c++','-target','x86-windows-gnu','-'+opt,'-UNDEBUG','-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-I'+str(out),*fp.test_include_flags(),fixture,'-o',exe]
 with (out/(opt+'-build.log')).open('w') as log:subprocess.run(list(map(str,cmd)),env=fp.zig_env(),stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300)
 r=subprocess.run([str(exe),*(['benchmark'] if a.benchmark else [])],capture_output=True,timeout=300);(out/(opt+'.log')).write_bytes(r.stdout+r.stderr);print((r.stdout+r.stderr).decode(errors='replace')[-2500:],flush=True);r.check_returncode();assert b'DRAW_ORCHESTRATION_PASS' in r.stdout
 runs.append(dict(mode=opt,exit_code=r.returncode))
if not a.benchmark and 'void captureGameDraw(' in after:
 for label,needle,replacement in [
  ('water-raw','captureWater(vs,UserPointer,draw);','captureWater(vs,UserPointer,raw);'),
  ('terrain-order','if(sharedTerrain)captureWorld(&positions);','if(false)captureWorld(&positions);'),
  ('up-normal-guard','if(!userMemory||materialCapture)captureModelNormals','if(true)captureModelNormals'),
  ('hdr-failure','if(SUCCEEDED(hr))hdrReplaySky','if(true)hdrReplaySky')]:
  assert after.count(needle)==1;write_routes(after.replace(needle,replacement,1));exe=out/(label+'.exe');cmd=[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-unused-parameter','-I'+str(out),*fp.test_include_flags(),fixture,'-o',exe]
  with (out/(label+'-build.log')).open('w') as log:subprocess.run(list(map(str,cmd)),env=fp.zig_env(),stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300)
  r=subprocess.run([str(exe)],capture_output=True,timeout=300);(out/(label+'.log')).write_bytes(r.stdout+r.stderr);assert r.returncode and b'DRAW_ORCHESTRATION_MISMATCH' in r.stdout,label;runs.append(dict(negative=label,rejected=True));print('Rejected',label,flush=True)
 write_routes(after)
assert all(hashlib.sha256(Path(f).read_bytes()).hexdigest()==h for f,h in inputs.items())
(out/'validation.json').write_text(json.dumps(dict(pass_=True,reference=a.reference,inputs=inputs,runs=runs,benchmark=a.benchmark,scope='Production adapter/capture/drawHook bodies with recording component boundaries; no game/GPU or effect-algorithm timing.'),indent=2)+'\n',encoding='utf-8')
