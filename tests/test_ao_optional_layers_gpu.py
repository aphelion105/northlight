# northlight-test: requires=windows-gpu,zig,client manual timing
"""Shared world compositor: optional pass counts, live switches and paired CPU/GPU timings."""
from pathlib import Path
import argparse,hashlib,json,math,os,re,statistics,struct,subprocess,sys
ROOT=Path(__file__).resolve().parents[1];sys.path[:0]=[str(ROOT),str(ROOT/'tests')]
import northlight_paths as fp
from windows_gpu_guard import processes

p=argparse.ArgumentParser(__doc__);p.add_argument('--reference',required=True);p.add_argument('--output',type=Path,required=True)
p.add_argument('--backend',action='append',type=Path,required=True);p.add_argument('--size',default='3840x2160');p.add_argument('--correctness-only',action='store_true');p.add_argument('--mode',type=int,default=0,choices=range(7));p.add_argument('--compare-pixels',action='store_true');a=p.parse_args()
assert not a.compare_pixels or a.correctness_only
processes();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
width,height=map(int,a.size.split('x'));assert width>=640 and height>=360
env={**fp.zig_env(),'PYTHONUTF8':'1','DISABLE_VULKAN_OBS_CAPTURE':'1','DXVK_LOG_PATH':str(out),'NORTHLIGHT_OPTIONAL_TIMING_MODE':str(a.mode)}
(out/'dxvk.conf').write_text('dxvk.allowFse = False\nd3d9.presentInterval = 0\n');env['DXVK_CONFIG_FILE']=str(out/'dxvk.conf')
sources=['world_gi.cpp','world_mesh_plan.cpp','static_shadow_scene.cpp','settings_overlay.cpp','settings_panel.cpp','imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp','imgui_impl_dx9.cpp']
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
inputs={str(p):sha(p) for p in [*fp.SRC.rglob('*.h'),*fp.SRC.rglob('*.inl'),*fp.SRC.rglob('*.cpp'),ROOT/'tests/ao_optional_layers_gpu.cpp',ROOT/'tests/world_screen_effects_gpu.cpp',Path(__file__),*a.backend]}
report=dict(pass_=False,reference=a.reference,inputs=inputs,runs=[],samples=[],mode=a.mode,pixel_comparisons=[],scope='Actual shared WorldRenderer with authored scene/depth/filtered AO. CPU submits only; GPU timestamp timeline includes submission gaps. Raw AO/blur and model draws are outside this measured scope. No gameplay FPS measurement.',game_launched=False)
def run(label,command,cwd=ROOT):
 processes();r=subprocess.run(list(map(str,command)),cwd=cwd,env=env,capture_output=True,text=True,timeout=300)
 (out/(label+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8');report['runs'].append(dict(label=label,exit_code=r.returncode))
 if r.returncode:print((r.stdout+r.stderr)[-4500:],flush=True)
 r.check_returncode();print('PASS',label,flush=True);return r.stdout
try:
 overlay=out/'reference';overlay.mkdir(exist_ok=True)
 for name in ['world_renderer.h','world_shadow_experiment.inl','world_compiled_shaders.h']:
  relative=fp.src(name).relative_to(ROOT).as_posix();raw=subprocess.check_output(['git','show',a.reference+':'+relative])
  if name=='world_renderer.h':
   # Compatibility accessor only: the frozen render/capture bodies are untouched.
   text=raw.decode('utf-8');marker='    bool effectsEnabled()const'
   assert text.count(marker)==1
   if 'bool surfaceLightingEnabled(' not in text:
    text=text.replace(marker,'    bool surfaceLightingEnabled()const{return effects.gi||effects.shadows||effects.fog||(quality.localLights&&quality.localLightLimit)||quality.autoBump;}\n'+marker)
   raw=text.encode('utf-8')
  (overlay/name).write_bytes(raw)
 for mode in ['current','reference']:
  extra=['-DNORTHLIGHT_OPTIONAL_LAYER_REFERENCE=1','-I'+str(overlay)] if mode=='reference' else []
  run('compile-'+mode,[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-DIMGUI_USER_CONFIG="northlight_imconfig.h"',*extra,*fp.test_include_flags(),'-I'+str(ROOT/'tests'),ROOT/'tests/ao_optional_layers_gpu.cpp',*[fp.src(s) for s in sources],'-luser32','-luuid','-o',out/(mode+'.exe')])
 # Compile both binaries before any timed work; ABBA process ordering for each backend.
 for backend_index,backend in enumerate(a.backend):
  for index,mode in enumerate(['reference','current'] if a.correctness_only else ['reference','current','current','reference']):
   settings=out/f'settings-{backend_index}-{index}';settings.mkdir(exist_ok=True)
   (settings/'northlight-renderer.ini').write_text('[Renderer]\nSettingsOverlay=0\nExperimentalHDR=0\n[Quality]\nDiagnostics=0\nRenderProfile=0\n',encoding='utf-8')
   if a.compare_pixels:env['NORTHLIGHT_OPTIONAL_PIXEL_DIRECTORY']=str(settings)
   label=f'gpu-{backend_index}-{index}-{mode}';text=run(label,[out/(mode+'.exe'),backend.resolve(),settings,640 if a.correctness_only else width,360 if a.correctness_only else height,int(not a.correctness_only)],settings)
   assert 'OPTIONAL_MODEL_POLICY_PASS' in text and 'OPTIONAL_SHARED_COMPOSITOR_PASS' in text and 'OPTIONAL_LAYERS_GPU_PASS' in text
   for line in text.splitlines():
    if line.startswith('OPTIONAL_LAYER_TIMING warm=0 '):
     row=dict(re.findall(r'(\w+)=([^ ]+)',line));report['samples'].append(dict(backend=backend_index,process=index,mode=mode,cpu_ms=float(row['cpuMs']),gpu_ms=float(row['gpuMs'])))
 if a.compare_pixels:
  for i in range(len(a.backend)):
   reference=sorted((out/f'settings-{i}-0').glob('capture-*-layer.fgr'));assert len(reference)==26
   changed=0;maximum=0.
   for path in reference:
    candidate=out/f'settings-{i}-1'/path.name;before=path.read_bytes();after=candidate.read_bytes()
    assert before[:20]==after[:20] and len(before)==len(after),candidate
    assert struct.unpack('<5I',before[:20])[3:]==(113,8) # RGBA16F
    for (old,),(new,) in zip(struct.iter_unpack('<H',before[20:]),struct.iter_unpack('<H',after[20:])):
     if old==new:continue
     x=struct.unpack('<e',struct.pack('<H',old))[0];y=struct.unpack('<e',struct.pack('<H',new))[0]
     # Backend rounding may differ by one FP16 step; reject visible drift.
     delta=abs(x-y);assert math.isfinite(x) and math.isfinite(y) and abs(old-new)<=1 and delta<=.001,(candidate,old,new)
     changed+=1;maximum=max(maximum,delta)
   report['pixel_comparisons'].append(dict(backend=i,frames=26,changed_components=changed,max_absolute_error=maximum,RGBA='FP16 <= 1 ULP and .001 absolute'))
 report['summary']=[]
 for backend_index,backend in enumerate(a.backend):
  item=dict(backend=str(backend))
  if not a.correctness_only:
   for mode in ['reference','current']:
    rows=[v for v in report['samples'] if v['backend']==backend_index and v['mode']==mode];assert len(rows)==16
    item[mode]={key:statistics.median(v[key] for v in rows) for key in ['cpu_ms','gpu_ms']}
  report['summary'].append(item)
 report['pass_']=True
finally:
 report['inputs_unchanged']=all(sha(Path(p))==v for p,v in inputs.items());report['pass_'] &= report['inputs_unchanged']
 (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report['summary'],indent=2));assert report['pass_']
