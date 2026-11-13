#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client timing
"""Exact point-cube output, clipped-gap batching and bounded static retention."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
import client_fixtures
from validate_native_scene_trace import processes

p=argparse.ArgumentParser(__doc__)
p.add_argument('--backend', type=Path, action='append')
p.add_argument('--output', type=Path, default=fp.output_dir()/'point-shadow-stutter')
p.add_argument('--reference-point-from', help='Compile the prior WorldRenderer/point implementation from this Git ref as an image/work-count control')
p.add_argument('--benchmark-preparation-from', help='Include this Git ref\'s pool preparation beside the current method for same-executable CPU ABBA')
a=p.parse_args();initial_processes=processes();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
timing_valid=os.environ.get('NORTHLIGHT_TEST_CORRECTNESS_ONLY')!='1' and not any(p['name'].lower() in ('wow.exe','wow64.exe') for p in initial_processes)
env=fp.zig_env();env.update(DXVK_LOG_PATH=str(out),DISABLE_VULKAN_OBS_CAPTURE='1')
(out/'dxvk.conf').write_text('dxvk.enableStateCache = False\nd3d9.presentInterval = 0\n')
env['DXVK_CONFIG_FILE']=str(out/'dxvk.conf')
sources=['world_gi.cpp','world_mesh_plan.cpp','static_shadow_scene.cpp','settings_overlay.cpp','settings_panel.cpp','imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp','imgui_impl_dx9.cpp']
exe=out/'point-shadow-stutter.exe'
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
inputs={str(path):sha(path) for folder in (fp.SRC,fp.SHADERS) for path in folder.rglob('*') if path.is_file()}
inputs.update({str(path):sha(path) for path in (Path(__file__),fp.TESTS/'point_shadow_stutter_gpu.cpp')})
client_fixtures.actor_client_programs(out);inputs[str(out/'actor_client_programs.h')]=sha(out/'actor_client_programs.h')
reference=[]
assert not (a.reference_point_from and a.benchmark_preparation_from)
if a.benchmark_preparation_from:
 overlay=out/'preparation-reference';overlay.mkdir()
 before=subprocess.check_output(['git','show',a.benchmark_preparation_from+':src/world/world_point_rendering.inl'],cwd=fp.REPO).decode()
 first=before.index('    void renderPointShadows(');last=before.index('    bool pointSoftShaders()',first)
 method=before[first:last].replace('void renderPointShadows(', 'void renderPointShadowsReference(',1)
 point=fp.src('world_point_rendering.inl').read_text();marker='    bool pointSoftShaders()'
 assert point.count(marker)==1
 for name,data in [('world_renderer.h',fp.src('world_renderer.h').read_text()),('world_point_rendering.inl',point.replace(marker,method+marker))]:
  target=overlay/name;target.write_text(data,encoding='utf-8',newline='\n');inputs[str(target)]=sha(target)
 reference=['-I',overlay,'-DNORTHLIGHT_POINT_PREPARATION_REFERENCE=1']
if a.reference_point_from:
 overlay=out/'reference';overlay.mkdir()
 for name,path in [('world_renderer.h','src/world/world_renderer.h'),('world_point_rendering.inl','src/world/world_point_rendering.inl')]:
  target=overlay/name;target.write_bytes(subprocess.check_output(['git','show',a.reference_point_from+':'+path],cwd=fp.REPO));inputs[str(target)]=sha(target)
 point=(overlay/'world_point_rendering.inl').read_text()
 reference=['-I',overlay]
 if 'const bool direct=!pointSlot().cacheFace[face];' not in point:reference+=['-DNORTHLIGHT_POINT_UNION_REFERENCE=1']
 if 'PointSlot recycled;' not in point:reference+=['-DNORTHLIGHT_POINT_RECYCLE_REFERENCE=1']
 if 'geometryOnlyFaces' not in point:reference+=['-DNORTHLIGHT_POINT_TERRAIN_REFERENCE=1']
cmd=[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-DIMGUI_USER_CONFIG="northlight_imconfig.h"',*reference,'-I',out,*fp.test_include_flags(),fp.TESTS/'point_shadow_stutter_gpu.cpp',*(fp.src(s) for s in sources),'-luser32','-luuid','-o',exe]
with (out/'compile.log').open('w') as log:subprocess.run(list(map(str,cmd)),env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300)
backends=a.backend or [Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll',fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll',fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll']
skin_program=client_fixtures.four_bone_vs3();inputs[str(skin_program)]=sha(skin_program)
report={'pass':False,'game_launched':False,'inputs':inputs,'command':list(map(str,cmd)),'cases':[]}
for i,backend in enumerate(backends):
 processes();run=out/str(i);run.mkdir();settings=run/'northlight-quality.ini';settings.write_text('[Quality]\nDiagnostics=0\n')
 result=subprocess.run([str(exe),str(backend.resolve()),str(run)+os.sep,str(skin_program)],env=env,cwd=run,capture_output=True,text=True,timeout=180)
 text=result.stdout+result.stderr;(run/'gpu.log').write_text(text);print(text[-3500:],flush=True)
 assert result.returncode==0 and all(marker in text for marker in ('POINT_LAMP_CASTER_PASS','SHADOW_CAPTURE_DEMAND_PASS','GEOMETRY_PREWARM_GPU_PASS','POINT_PREWARM_GPU_PASS','POINT_PREWARM_IMAGE_PASS','POINT_GAP_GPU_PASS','POINT_STATIC_KEEP_GPU_PASS','POINT_LAZY_STATIC_GPU_PASS','POINT_REPLAY_COALESCE_GPU_PASS','POINT_COLD_COMPLETION_GPU_PASS','POINT_TOGGLE_GPU_PASS','POINT_RESET_BOUNDS_GPU_PASS','POINT_SKIN_SOURCE_GPU_PASS')),result.returncode
 assert text.count('POINT_SKIN_SOURCE_GPU_PASS ')==2
 assert 'POINT_SINGLE_BONE_CHEAP_GPU_PASS ' in text
 assert 'POINT_WHITE_CUBE_GPU_PASS ' in text
 assert text.count('POINT_DIRECT_GPU_PASS ')==10
 assert 'POINT_RECYCLE_GPU_PASS ' in text
 terrain=[]
 if not a.reference_point_from or 'geometryOnlyFaces' in point:
  assert 'POINT_TERRAIN_REUSE_PASS ' in text and 'POINT_TERRAIN_ACTORS_PASS ' in text
  rows=[dict(re.findall(r'(\w+)=([^ ]+)',line)) for line in text.splitlines() if line.startswith('POINT_TERRAIN_TIMING ') and 'warm=0 ' in line]
  for retained in (0,1):
   samples=[r for r in rows if int(r['retained'])==retained];assert len(samples)==6
   terrain.append({'retained':bool(retained),**{key:statistics.median(float(r[key]) for r in samples) for key in ('cpuMs','completeMs','gpuMs','draws')}})
 preparation=[]
 if a.benchmark_preparation_from:
  assert 'POINT_PREPARATION_PASS ' in text
  rows=[dict(re.findall(r'(\w+)=([^ ]+)',line)) for line in text.splitlines() if line.startswith('POINT_PREPARATION_TIMING ') and 'warm=0 ' in line]
  for count in (0,1,4,16,64):
   entry={'lights':count}
   for current in (0,1):
    values=[float(r['cpuMs']) for r in rows if int(r['lights'])==count and int(r['current'])==current];assert len(values)==6
    entry['current_ms' if current else 'before_ms']=statistics.median(values)
   preparation.append(entry)
 samples=[dict(re.findall(r'(\w+)=([^ ]+)',line)) for line in text.splitlines() if line.startswith('POINT_GAP_TIMING ')]
 timing={str(mode):{key:statistics.median(float(s[key]) for s in samples if s['optimized']==str(mode)) for key in ('submitMs','completeMs','draws')} for mode in (0,1)}
 report['cases'].append({'backend':str(backend.resolve()),'sha256':sha(backend),'log':str(run/'gpu.log'),'timing':timing if timing_valid else None,'preparation':preparation if timing_valid else None,'terrain':terrain if timing_valid else None})
assert all(sha(Path(path))==digest for path,digest in inputs.items()),'Source changed during the oracle'
report['pass']=True;report['timing_valid']=timing_valid;report['scope']='Authored static workload; no gameplay FPS claim. Concurrent game invalidates timing; pixel/state assertions remain valid.'
(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS',out)
