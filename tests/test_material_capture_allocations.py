# northlight-test: requires=windows-exe,zig slow
"""Material capture allocations: duplicate/unique texture keys, rollback, pressure and leases."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess,sys
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT));import northlight_paths as fp
p=argparse.ArgumentParser(__doc__);p.add_argument('--output',type=Path);p.add_argument('--headers',type=Path);p.add_argument('--reference',default='d9b39749');a=p.parse_args()
out=(a.output or fp.output_dir()/'material-capture-allocations').resolve();out.mkdir(parents=True,exist_ok=True)
fixture=(fp.TESTS/'displacement_state_reuse.cpp').read_text().split('int main(')[0]
fixture=fixture.replace('struct MaterialCaptureStateTestAccess {','struct MaterialCaptureStateTestAccess {\n static void captureAllocations();\n static void replayBindings();\n static void replayConstantRanges();')
fixture=fixture.replace('static int failAllocationAfter=-1;','static size_t allocationCount=0;static int failAllocationAfter=-1;').replace('void* operator new(std::size_t n){','void* operator new(std::size_t n){++allocationCount;')
(out/'material_bindings_reference.h').write_text(fixture,encoding='utf-8')
(out/'material_packet_bindings.h').write_text((fp.TESTS/'test_material_batch_bindings.cpp').read_text().rsplit('int main(',1)[0],encoding='utf-8')
(out/'packet_batch_fixture.h').write_text((fp.TESTS/'test_material_packet_auxiliary.cpp').read_text().split('void MaterialCaptureStateTestAccess::packetAuxiliary()')[0],encoding='utf-8')
control=out/'control';control.mkdir(exist_ok=True);(control/'auto_bump_renderer.h').write_bytes(subprocess.check_output(['git','show',a.reference+':src/water/auto_bump_renderer.h'],cwd=ROOT))
header=(a.headers or fp.src('auto_bump_renderer.h').parent)/'auto_bump_renderer.h'
texture_intern='deferredTextures.try_emplace(t.texture,t.bytes)' in header.read_text(encoding='utf-8')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
watched=[header,fp.TESTS/'material_capture_allocations.cpp',Path(__file__),fp.TESTS/'displacement_state_reuse.cpp',fp.TESTS/'test_material_batch_bindings.cpp',fp.TESTS/'test_material_packet_auxiliary.cpp']
inputs={str(f.resolve()):sha(f) for f in watched};runs=[];executables={}
for opt in ['O2','O0']:
 for name,folder in [('control',control),('current',header.parent)]:
  exe=out/(name+'-'+opt+'.exe');cmd=[fp.zig(),'c++','-target','x86-windows-gnu','-'+opt,'-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-Wno-return-type','-I'+str(folder),'-I'+str(out),*fp.test_include_flags(),fp.TESTS/'material_capture_allocations.cpp','-luser32','-luuid','-o',exe]
  if name=='current' and texture_intern:cmd.insert(2,'-DNORTHLIGHT_TEXTURE_INTERN_TEST=1')
  with (out/(name+'-'+opt+'-build.log')).open('w') as log:subprocess.run(list(map(str,cmd)),env=fp.zig_env(),stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300)
  executables[name,opt]=exe
 for turn,name in enumerate(['control','current']):
  r=subprocess.run([str(executables[name,opt])],capture_output=True,timeout=180);log=out/(name+'-'+opt+'.log');log.write_bytes(r.stdout+r.stderr);print(log.name,r.returncode,(r.stdout+r.stderr).decode(errors='replace')[-1800:],flush=True);r.check_returncode();assert b'MATERIAL_CAPTURE_ALLOCATIONS_PASS' in r.stdout;runs.append(dict(name=name,mode=opt,exit_code=r.returncode))
 def records(name):return [dict(re.findall(r'(\w+)=([^\s]+)',l)) for l in (out/(name+'-'+opt+'.log')).read_text().splitlines() if l.startswith('PACKET_CASE')]
 old,new=records('control'),records('current');assert len(old)==len(new)==16
 for x,y in zip(old,new):
  for field in ['phase','draws','accepted','barriers','hash','endEmpty']:assert x[field]==y[field],(field,x,y)
  if x['phase']=='1' and int(x['draws'])>64:assert int(y['allocations'])<int(x['allocations']),(x,y)
 for name in ['control','current']:assert 'TEXTURE_INTERN_PASS' in (out/(name+'-'+opt+'.log')).read_text()
 unique=[re.search(r'UNIQUE_TEXTURE_CASE allocations=(\d+)',(out/(name+'-'+opt+'.log')).read_text())[1] for name in ['control','current']];assert unique[0]==unique[1],unique
if texture_intern:
 text=header.read_text(encoding='utf-8')
 for name,needle,replacement,marker in [
  ('duplicate-allocation','deferredTextures.try_emplace(t.texture,t.bytes)','deferredTextures.emplace(t.texture,t.bytes)','DUPLICATE_TEXTURE_MUST_NOT_ALLOCATE'),
  ('lost-rollback','for(unsigned i=0;i<insertedCount;++i){auto entry=deferredTextures.find(inserted[i]);','for(unsigned i=1;i<insertedCount;++i){auto entry=deferredTextures.find(inserted[i]);','DEFERRED_TEXTURE_ROLLBACK_MISMATCH')]:
  assert text.count(needle)==1;folder=out/name;folder.mkdir(exist_ok=True);(folder/'auto_bump_renderer.h').write_text(text.replace(needle,replacement,1),encoding='utf-8')
  exe=folder/'negative.exe';cmd=[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-DNORTHLIGHT_TEXTURE_INTERN_TEST=1','-std=c++17','-Wno-microsoft-exception-spec','-Wno-return-type','-I'+str(folder),'-I'+str(out),*fp.test_include_flags(),fp.TESTS/'material_capture_allocations.cpp','-luser32','-luuid','-o',exe]
  with (folder/'compile.log').open('w') as log:subprocess.run(list(map(str,cmd)),env=fp.zig_env(),stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300)
  r=subprocess.run([str(exe)],capture_output=True,timeout=180);(folder/'run.log').write_bytes(r.stdout+r.stderr);assert r.returncode and marker.encode() in r.stderr,name;runs.append(dict(negative=name,rejected=True));print('Rejected',name,flush=True)
assert all(sha(Path(f))==h for f,h in inputs.items())
(out/'validation.json').write_text(json.dumps(dict(pass_=True,reference=a.reference,inputs=inputs,runs=runs,scope='Actual recordCapture/clearDeferred/endFrame with immutable source and fake native API. Instrumented allocator counts/faults only; no CPU/GPU timing claim.'),indent=2)+'\n',encoding='utf-8')
