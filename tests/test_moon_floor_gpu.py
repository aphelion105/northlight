#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client
"""Actual city field/renderer ownership and all optional moon fog variants. No timings/game control."""
import argparse,hashlib,json,os,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
from windows_gpu_guard import processes
parser=argparse.ArgumentParser(__doc__)
parser.add_argument('--host-only',action='store_true',help='Focused actual-renderer wiring regression; shader fixtures remain a separate full run.')
parser.add_argument('--output',type=Path,default=fp.output_dir()/'moon-floor')
parser.add_argument('--allow-game-process',action='store_true',help='Explicit isolated correctness-only authorization; no timings.')
args=parser.parse_args()
if not args.allow_game_process:processes()
out=args.output.resolve();assert not fp.is_client(out);out.mkdir(parents=True,exist_ok=True);(out/'settings').mkdir(exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
entries=['WorldFog','WorldFogActor','WorldFogActorTiled','WorldFogTransition','WorldFogActorTransition','WorldFogActorTiledTransition']
world=json.loads((fp.SHADERS/'world-shader-build.json').read_text());transition=json.loads((fp.SHADERS/'shadow-transition-shader-build.json').read_text())
assert world['source_sha256']==sha(fp.SHADERS/'world_effects.hlsl')
assert all(sha(fp.SHADERS/name)==value for name,value in transition['sources'].items())
for entry in entries:
 for name in [entry,entry.replace('WorldFog','MoonFloorFog')]:
  manifest=transition if name.endswith('Transition') else world
  assert sha(fp.COMPILED/(name+'.bin'))==manifest['shaders'][name]['sha256']
owned=['src/sky/regional_fog.h','src/world/world_renderer.h','shaders/world_effects.hlsl','shaders/shadow_transition.hlsl','src/generated/world_compiled_shaders.h','src/generated/shadow_transition_compiled_shaders.h','tests/moon_floor_field.cpp','tests/moon_floor_gpu.cpp','tests/moon_floor_host_gpu.cpp']
inputs={name:sha(fp.REPO/name) for name in owned}
report={'game_control':False,'timings_measured':False,'actual_renderer':True,'allow_game_process':args.allow_game_process,'inputs':inputs,'runs':[]}
env={k:v for k,v in os.environ.items() if not k.startswith(('NORTHLIGHT_','DXVK_','TRACE_','RETRACE_','APITRACE_'))};env['DXVK_LOG_PATH']=str(out)
def run(label,command):
 r=subprocess.run(list(map(str,command)),cwd=out,env=env,capture_output=True,text=True,timeout=300)
 (out/(label+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8')
 report['runs'].append({'test':label,'exit_code':r.returncode,'stdout':r.stdout,'stderr':r.stderr})
 (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
 print(label,r.returncode,'\n'.join(line for line in r.stdout.splitlines() if 'PASS' in line or 'FLOOR_REFERENCE' in line),flush=True)
 assert r.returncode==0,r.stderr
binaries={}
for name in (['moon_floor_host_gpu'] if args.host_only else ['moon_floor_field','moon_floor_gpu','moon_floor_host_gpu']):
 exe=out/(name+'.exe');extra=[];flags=[]
 if name=='moon_floor_host_gpu':
  extra=[fp.src(s) for s in ['world_gi.cpp','world_mesh_plan.cpp','static_shadow_scene.cpp','settings_overlay.cpp','settings_panel.cpp','imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp','imgui_impl_dx9.cpp']]
  flags=['-Wno-microsoft-exception-spec','-DIMGUI_USER_CONFIG="northlight_imconfig.h"','-luuid']
 cmd=[fp.zig(),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG',*flags,*fp.test_include_flags(),'-I'+str(fp.TESTS),fp.TESTS/(name+'.cpp'),*extra,'-luser32','-o',exe]
 r=subprocess.run(list(map(str,cmd)),env=fp.zig_env(),capture_output=True,text=True);(out/(name+'-compile.log')).write_text(r.stdout+r.stderr);assert r.returncode==0,r.stderr;binaries[name]=exe
if not args.host_only:run('field',[binaries['moon_floor_field']])
for backend,runtime in [('dxvk',fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll'),('dxvk2',fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll'),('native',Path(os.environ.get('WINDIR','C:/Windows'))/'SysWOW64/d3d9.dll')]:
 report.setdefault('runtime_sha256',{})[backend]=sha(runtime)
 run('host-'+backend,[binaries['moon_floor_host_gpu'],runtime,out/'settings'])
 for entry in ([] if args.host_only else entries):
  cmd=[binaries['moon_floor_gpu'],fp.COMPILED/(entry+'.bin'),fp.COMPILED/(entry.replace('WorldFog','MoonFloorFog')+'.bin'),runtime]
  if entry!='WorldFog':cmd.append(('3' if entry=='WorldFogTransition' else '2') if entry.endswith('Transition') else '1')
  run(entry+'-'+backend,cmd)
report['inputs_unchanged']=all(sha(fp.REPO/name)==value for name,value in inputs.items());report['pass']=report['inputs_unchanged'] and all(r['exit_code']==0 for r in report['runs'])
report['exe_sha256']={name:sha(exe) for name,exe in binaries.items()}
(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');assert report['pass']
