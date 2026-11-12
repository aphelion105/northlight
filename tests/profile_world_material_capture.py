# northlight-test: requires=windows-gpu,zig manual timing
"""Compare opaque shadow capture CPU and exact point-shadow output in one executable. Never starts WoW."""
from pathlib import Path
import argparse,hashlib,json,os,re,statistics,struct,subprocess,sys
ROOT=Path(__file__).resolve().parents[1];sys.path[:0]=[str(ROOT),str(ROOT/'scripts')]
import northlight_paths as fp
from build_renderer import source_identity

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--backend',type=Path,action='append',required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--rounds',type=int,default=4)
p.add_argument('--reuse-exe',type=Path,help='Reuse this runner\'s verified executable at the same source identity')
a=p.parse_args();assert 1<=a.rounds<=8
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);identity=source_identity()
sha=lambda path:hashlib.sha256(Path(path).read_bytes()).hexdigest()
inputs={str(path):sha(path) for path in [Path(__file__).resolve(),ROOT/'tests/world_capture_handoff_gpu.cpp',*[v.resolve() for v in a.backend]]}
# Only the test override adds the old-policy switch. Production has no test flag
# or extra field; both policies execute the same compiled capture implementation.
headers=out/'headers';headers.mkdir(exist_ok=True)
source=fp.src('world_renderer.h');header=source.read_text(encoding='utf-8')
assert header.count('class WorldRenderer {')==1 and header.count('const bool materialNeeded=alpha||')==1
header=header.replace('class WorldRenderer {','extern bool northlightForceMaterialCapture;\nclass WorldRenderer {',1)
header=header.replace('const bool materialNeeded=alpha||','const bool materialNeeded=northlightForceMaterialCapture||alpha||',1)
(headers/'world_renderer.h').write_text(header,encoding='utf-8')
exe=a.reuse_exe.resolve() if a.reuse_exe else out/'capture.exe';env={**fp.zig_env(),'PYTHONUTF8':'1','NORTHLIGHT_TEST_ALLOW_RUNNING_GAME':'1'}
units=['world_gi.cpp','world_mesh_plan.cpp','static_shadow_scene.cpp','settings_overlay.cpp','settings_panel.cpp','imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp','imgui_impl_dx9.cpp']
command=[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-DIMGUI_USER_CONFIG="northlight_imconfig.h"','-DNORTHLIGHT_CAPTURE_MATERIAL=1','-DNORTHLIGHT_SHARED_SOURCE=1','-DNORTHLIGHT_CAPTURE_HANDOFF=1','-DNORTHLIGHT_CAPTURE_RASTER=1','-I'+str(headers),*fp.test_include_flags(),ROOT/'tests/world_capture_handoff_gpu.cpp',*[fp.src(n) for n in units],'-luser32','-luuid','-o',exe]
if a.reuse_exe:
 prior=json.loads((exe.parent/'validation.json').read_text(encoding='utf-8'))
 assert prior['pass_'] and prior['source_identity']==identity and prior['exe_sha256']==sha(exe)
 fixture=str(ROOT/'tests/world_capture_handoff_gpu.cpp');assert prior['inputs'][fixture]==inputs[fixture]
 assert sha(headers/'world_renderer.h')==sha(exe.parent/'headers/world_renderer.h')
else:
 print('COMPILING capture fixture (no DLL)',flush=True)
 with (out/'compile.log').open('w',encoding='utf-8') as log:subprocess.run(list(map(str,command)),env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=360)
 data=bytearray(exe.read_bytes());at=struct.unpack_from('<I',data,0x3c)[0]+22;struct.pack_into('<H',data,at,struct.unpack_from('<H',data,at)[0]|0x20);exe.write_bytes(data)
runs=[]
def run(backend,index,scenario,repeat,legacy):
 folder=out/f'backend-{index}-case-{scenario}-round-{repeat}-legacy-{legacy}';folder.mkdir(exist_ok=True)
 result=subprocess.run([str(exe),str(backend),str(folder),'1','1',str(legacy),str(scenario)],cwd=folder,env={**env,'DXVK_LOG_PATH':str(folder)},capture_output=True,text=True,timeout=120)
 (folder/'run.log').write_text(result.stdout+result.stderr,encoding='utf-8')
 if result.returncode:print((result.stdout+result.stderr)[-3000:],flush=True)
 result.check_returncode();assert 'CAPTURE_MATERIAL_PASS' in result.stdout
 frames=[dict(re.findall(r'(\w+)=([^ ]+)',line)) for line in result.stdout.splitlines() if line.startswith('CAPTURE_FRAME ') and 'warm=0 ' in line]
 assert len(frames)==32 and all(int(f['packets'])==512 and int(f['handoffs'])==512 for f in frames)
 record=dict(backend=index,scenario=scenario,repeat=repeat,legacy=bool(legacy),cpu_ms=statistics.median(float(f['cpuMs']) for f in frames),packets_sha256=sha(folder/'packets.bin'),faces_sha256=sha(folder/'faces.bin'))
 runs.append(record);return record
for index,backend in enumerate(a.backend):
 backend=backend.resolve()
 for scenario in range(5):
  for repeat in range(a.rounds):
   pair=[run(backend,index,scenario,repeat,legacy) for legacy in ([1,0] if repeat%2==0 else [0,1])]
   for key in ('packets_sha256','faces_sha256'):assert pair[0][key]==pair[1][key],(index,scenario,key)
  pair=[r for r in runs if r['backend']==index and r['scenario']==scenario]
  stats={label:statistics.median(r['cpu_ms'] for r in pair if r['legacy']==legacy) for label,legacy in [('before',True),('after',False)]}
  print('CAPTURE_CPU',index,scenario,json.dumps(stats),flush=True)
assert identity==source_identity() and all(sha(path)==digest for path,digest in inputs.items())
report=dict(pass_=True,source_identity=identity,inputs=inputs,exe_sha256=sha(exe),production_dll_built=False,installer_built=False,runs=runs,scope='512 rigid shadow captures per frame, source acquisition, replay recycling and capture under the mirror gate. Exact packet bytes and six point-shadow faces. Excludes game draws, other effects and Present.')
(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print('CAPTURE_MATERIAL_CPU_PASS',flush=True)
