#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig manual
"""PCSS contact/width and real local resolve state, on native D3D9 and DXVK."""
import argparse,ctypes,hashlib,json,os,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
sys.path.insert(0,str(fp.SCRIPTS/'shaders'))
from shader_compiler import Compiler,validate_assembly
sys.path.insert(0,str(fp.TESTS))
from validate_native_scene_trace import processes
p=argparse.ArgumentParser(__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--backend',action='append',required=True);p.add_argument('--benchmark',action='store_true');p.add_argument('--baseline-shaders',type=Path);p.add_argument('--fusion-only',action='store_true',help='Run the point-pass baseline comparison independently of the directional PCSS probe');a=p.parse_args()
if a.fusion_only and (not a.baseline_shaders or a.benchmark):p.error('--fusion-only requires --baseline-shaders and excludes --benchmark')
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);initial_processes=processes()
timing_valid=a.benchmark and os.environ.get('NORTHLIGHT_TEST_CORRECTNESS_ONLY')!='1' and not any(p['name'].lower() in ('wow.exe','wow64.exe') for p in initial_processes)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
inputs={str(p):sha(p) for folder in (fp.SRC,fp.SHADERS) for p in folder.rglob('*') if p.is_file()}
inputs.update({str(p):sha(p) for p in (Path(__file__),fp.TESTS/'pcss_filters_gpu.cpp')})
if a.baseline_shaders:inputs.update({str(p.resolve()):sha(p) for p in (a.baseline_shaders/'LocalShadowResolve.bin',a.baseline_shaders/'LocalLightingSoft.bin')})
api=ctypes.WinDLL('kernel32',use_last_error=True)
api.CreateMutexW.argtypes=[ctypes.c_void_p,ctypes.c_int,ctypes.c_wchar_p];api.CreateMutexW.restype=ctypes.c_void_p
api.WaitForSingleObject.argtypes=[ctypes.c_void_p,ctypes.c_ulong];api.ReleaseMutex.argtypes=[ctypes.c_void_p];api.CloseHandle.argtypes=[ctypes.c_void_p]
mutex=api.CreateMutexW(None,0,'Local\\NorthlightOfflineGPU')
if not mutex or api.WaitForSingleObject(mutex,0) not in (0,128):raise RuntimeError('Another offline GPU test is active')
try:
    fp.out=lambda:out/'toolchain'
    source=out/'probe.hlsl';source.write_text((fp.SHADERS/'world_effects.hlsl').read_text().replace('#include "auto_bump_normal.hlsl"',(fp.SHADERS/'auto_bump_normal.hlsl').read_text())+'''
float4 ProbeInfo:register(c80);
float4 PcssProbe(float2 uv:TEXCOORD0):COLOR0{
 float3 lp=float3((uv.x-.5)*.06,(.5-uv.y)*.06,.6);
 return shadowMapFiltered(lp,ProbeInfo.z>.5,ProbeInfo.z>.5?48:192,ProbeInfo.xy,ProbeInfo.w>.5).xxxx;
}
''')
    compiler=Compiler(allow_compatible_binary=True);compiler.compile(source,'PcssProbe','ps_3_0',out/'probe.bin')
    stats=validate_assembly(out/'probe.bin.asm','ps_3_0')
    sources=['world_gi.cpp','world_mesh_plan.cpp','static_shadow_scene.cpp','settings_overlay.cpp','settings_panel.cpp','imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp','imgui_impl_dx9.cpp']
    env=fp.zig_env()
    for key,folder in [('TEMP','temp'),('TMP','temp'),('DXVK_SHADER_CACHE_PATH','shader-cache'),('DXVK_STATE_CACHE_PATH','state-cache')]:
        (out/folder).mkdir(exist_ok=True);env[key]=str(out/folder)
    env.update(DXVK_LOG_PATH=str(out),DISABLE_VULKAN_OBS_CAPTURE='1')
    exe=out/'pcss.exe'
    command=[str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-Wno-microsoft-exception-spec','-DIMGUI_USER_CONFIG="northlight_imconfig.h"',*fp.test_include_flags(),str(fp.TESTS/'pcss_filters_gpu.cpp'),*(str(fp.src(s)) for s in sources),'-luser32','-luuid','-o',str(exe)]
    with (out/'compile.log').open('w') as log:subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300)
    results=[]
    for index,backend in enumerate(a.backend):
        controls=[str((a.baseline_shaders/(name+'.bin')).resolve()) for name in ('LocalShadowResolve','LocalLightingSoft')] if a.baseline_shaders else []
        processes();run=subprocess.run([str(exe),str(Path(backend).resolve()),str(out/'probe.bin'),*controls,*(['benchmark'] if a.benchmark else ['fusion-only'] if a.fusion_only else [])],cwd=out,env=env,capture_output=True,text=True,timeout=180,creationflags=subprocess.CREATE_NO_WINDOW)
        text=run.stdout+run.stderr;(out/f'gpu-{index}.log').write_text(text);print(text,flush=True)
        assert run.returncode==0 and (a.fusion_only or 'PCSS_PIPELINE_PASS' in text)
        if controls:assert 'POINT_PCSS_FUSION_PASS cases=96 ' in text
        results.append({'backend':backend,'output':text})
    assert all(sha(Path(p))==h for p,h in inputs.items()),'Source changed during PCSS oracle'
    (out/'validation.json').write_text(json.dumps({'pass':True,'game_launched':False,'inputs':inputs,'probe':stats,'results':results,'timing_valid':timing_valid,'scope':'Authored correctness and draw/resource counts; component timings require an uncontaminated benchmark run and never imply gameplay FPS.'},indent=2)+'\n')
finally:
    api.ReleaseMutex(mutex);api.CloseHandle(mutex)
