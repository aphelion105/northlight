#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client
"""Actual settings panel rendering on private SDR/scRGB GPU targets, without the game."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,os,shutil,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
parser=argparse.ArgumentParser(__doc__);parser.add_argument('--sdr-only',action='store_true',help='Validate the native SDR path without asserting unsupported native scRGB output')
parser.add_argument('--x64',action='store_true',help='Build an AMD64 host and use system D3D9 by default')
args=parser.parse_args()
repo=fp.REPO
out=fp.output_dir()/'overlay-gpu';out.mkdir(parents=True,exist_ok=False);(out/'settings').mkdir()
presets=sorted((repo/'client-config'/'appearance-presets').glob('*.ini'))
appearance=out/'settings'/'appearance-presets';appearance.mkdir()
for preset in presets: shutil.copy2(preset,appearance/preset.name)
spec=importlib.util.spec_from_file_location('overlay_guard',repo/'tests/validate_native_scene_trace.py');guard=importlib.util.module_from_spec(spec);spec.loader.exec_module(guard)
backend=Path(os.environ['NORTHLIGHT_TEST_D3D9']) if os.environ.get('NORTHLIGHT_TEST_D3D9') else (
    Path(os.environ['SystemRoot'])/'System32/d3d9.dll' if args.x64 else fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll')
env={k:v for k,v in os.environ.items() if not k.startswith(('NORTHLIGHT_','DXVK_','TRACE_','RETRACE_','APITRACE_'))}
for key,name in [('TEMP','temp'),('TMP','temp'),('ZIG_LOCAL_CACHE_DIR','zig-local'),('ZIG_GLOBAL_CACHE_DIR','zig-global'),('DXVK_SHADER_CACHE_PATH','shader-cache'),('DXVK_STATE_CACHE_PATH','state-cache'),('__GL_SHADER_DISK_CACHE_PATH','driver-cache')]:
    (out/name).mkdir(exist_ok=True);env[key]=str(out/name)
(out/'dxvk.conf').write_bytes(b'dxvk.allowFse = False\nd3d9.presentInterval = 0\n')
env.update(DISABLE_VULKAN_OBS_CAPTURE='1',DXVK_CONFIG_FILE=str(out/'dxvk.conf'),DXVK_LOG_PATH=str(out))
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
sources=['settings_overlay.cpp','settings_panel.cpp','imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp','imgui_impl_dx9.cpp']
inputs={str(p):sha(p) for p in [fp.TESTS/'settings_overlay_gpu.cpp',*(fp.src(s) for s in sources),fp.src('hdr_compiled_shaders.h'),fp.src('settings_service.h'),backend,*presets]}
report={'pass':False,'inputs':inputs,'commands':[],'game_launched':False,'actual_menu':True,'private_render_target':True,'windows_hdr_output_tested':False,'sdr_only':args.sdr_only}
def run(label,command):
    guard.processes();p=subprocess.run(list(map(str,command)),cwd=out,env=env,capture_output=True,text=True,timeout=240)
    (out/(label+'.log')).write_text(p.stdout+p.stderr);report['commands'].append({'label':label,'exit_code':p.returncode});print(label,p.returncode,(p.stdout+p.stderr)[-6000:],flush=True);assert p.returncode==0,label
try:
    exe=out/'overlay-gpu.exe'
    target='x86_64-windows-gnu' if args.x64 else 'x86-windows-gnu'
    run('compile',[fp.zig(),'c++','-target',target,'-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-DNORTHLIGHT_OVERLAY_TEST_INPUT=1','-DIMGUI_USER_CONFIG="northlight_imconfig.h"',*fp.test_include_flags(),'-I'+str(fp.TESTS),fp.TESTS/'settings_overlay_gpu.cpp',*(fp.src(s) for s in sources),'-luser32','-luuid','-o',exe])
    run('gpu',[exe,backend,out/'settings',*(['--sdr-only'] if args.sdr_only else [])]);report['pass']=True
finally:
    guard.processes();report['inputs_unchanged']=all(sha(Path(p))==v for p,v in inputs.items());report['pass']&=report['inputs_unchanged'];(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
assert report['pass']
