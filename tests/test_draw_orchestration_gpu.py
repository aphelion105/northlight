# northlight-test: requires=windows-gpu,zig,client manual timing
"""Actual Device all-off/active draw gates, frozen/current x86 submit costs and native pixels."""
from pathlib import Path
import argparse,hashlib,json,os,re,statistics,subprocess,sys
ROOT=Path(__file__).resolve().parents[1];sys.path[:0]=[str(ROOT),str(ROOT/'tests')];import northlight_paths as fp
from windows_gpu_guard import processes
p=argparse.ArgumentParser(__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--reference',default='3fc6b718');p.add_argument('--backend',type=Path);p.add_argument('--pairs',type=int,default=3);a=p.parse_args();assert 1<=a.pairs<=8
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);backend=(a.backend or fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll').resolve()
sources=['world_gi.cpp','world_mesh_plan.cpp','static_shadow_scene.cpp','settings_overlay.cpp','settings_panel.cpp','imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp','imgui_impl_dx9.cpp']
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();inputs={str(f):sha(f) for f in [*fp.SRC.rglob('*.h'),*fp.SRC.rglob('*.inl'),*fp.SRC.rglob('*.cpp'),fp.TESTS/'pipeline_all_off_gpu.cpp',Path(__file__),backend]}
overlay=out/'reference';overlay.mkdir(exist_ok=True);(overlay/'renderer.cpp').write_bytes(subprocess.check_output(['git','show',a.reference+':src/proxy/renderer.cpp'],cwd=ROOT))
env={**fp.zig_env(),'DISABLE_VULKAN_OBS_CAPTURE':'1','DXVK_LOG_PATH':str(out),'DXVK_CONFIG':'d3d9.customVendorId = 1002; d3d9.presentInterval = 0','PYTHONUTF8':'1'}
runs=[];rows=[]
def run(label,cmd,cwd=ROOT):
 processes();r=subprocess.run(list(map(str,cmd)),cwd=cwd,env=env,capture_output=True,timeout=600);(out/(label+'.log')).write_bytes(r.stdout+r.stderr);print(label,r.returncode,(r.stdout+r.stderr).decode(errors='replace')[-900:] if r.returncode else '',flush=True);r.check_returncode();runs.append(dict(label=label,exit_code=r.returncode));return r.stdout.decode(errors='replace')
for mode in ['reference','current']:
 extra=['-I'+str(overlay)] if mode=='reference' else []
 run('compile-'+mode,[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-DIMGUI_USER_CONFIG="northlight_imconfig.h"',*extra,*fp.test_include_flags(),fp.TESTS/'pipeline_all_off_gpu.cpp',*[fp.src(s) for s in sources],'-luser32','-luuid','-o',out/(mode+'.exe')])
for pair in range(a.pairs):
 for mode in (['reference','current'] if pair%2==0 else ['current','reference']):
  settings=out/(mode+'-'+str(pair));settings.mkdir(exist_ok=True);(settings/'northlight-renderer.ini').write_text('[Renderer]\nSettingsOverlay=0\nExperimentalHDR=0\n[Quality]\nDiagnostics=0\nRenderProfile=0\n',encoding='utf-8')
  text=run(mode+'-'+str(pair),[out/(mode+'.exe'),backend,settings],settings);assert all(x in text for x in ['ALL_OFF_PIPELINE_PASS','ALL_OFF_IMAGE_PASS','ALL_OFF_WORKER_PASS','ACTIVE_ROUTE_DEVICE_PASS'])
  for line in text.splitlines():
   if line.startswith(('ALL_OFF_CPU ','ACTIVE_ROUTE_CPU ')):rows.append(dict(re.findall(r'(\w+)=([^\s]+)',line),kind=line.split()[0],build=mode,pair=pair))
summary=[]
for kind in ['ALL_OFF_CPU','ACTIVE_ROUTE_CPU']:
 cells=sorted({(r['mode'],r.get('gates','-')) for r in rows if r['kind']==kind})
 expected={(str(mode),'-') for mode in range(3)} if kind=='ALL_OFF_CPU' else {(str(mode),str(gate)) for mode in range(5) for gate in range(2)}
 assert set(cells)==expected,(kind,cells)
 for cell in cells:
  groups={mode:[r for r in rows if r['kind']==kind and (r['mode'],r.get('gates','-'))==cell and r['build']==mode] for mode in ['reference','current']}
  assert all(len(v)==3*a.pairs for v in groups.values())
  for key in ['draws','reads','normalDemand']:
   expected=sorted(int(r[key]) for r in groups['reference'] if key in r);actual=sorted(int(r[key]) for r in groups['current'] if key in r);assert expected==actual,(kind,cell,key,expected,actual)
  if kind=='ALL_OFF_CPU':assert all(r['reads']=='0' for v in groups.values() for r in v)
  med={mode:statistics.median(float(r['ms']) for r in g) for mode,g in groups.items()};summary.append(dict(kind=kind,mode=cell[0],gates=cell[1],median_ms=med,ratio=med['current']/med['reference']))
assert all(sha(Path(f))==h for f,h in inputs.items())
(out/'validation.json').write_text(json.dumps(dict(pass_=True,reference=a.reference,inputs=inputs,runs=runs,rows=rows,summary=summary,actual_device=True,scope='Same x86 actual Device with native draws stubbed for CPU costs. Four forms, all-off and fixed-function active gates. Authored real native image check separately. Capture/handoff consumers have a separate production-route oracle; not gameplay FPS.'),indent=2)+'\n',encoding='utf-8')
print(json.dumps(summary,indent=2))
