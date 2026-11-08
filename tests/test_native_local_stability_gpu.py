#!/usr/bin/env python3
# northlight-test: requires=client,stormlib,zig,windows-gpu manual
"""Native camera stability, smooth normals and exact near-cap legacy admission."""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
parser=argparse.ArgumentParser(__doc__)
parser.add_argument('--prepare-only',action='store_true')
parser.add_argument('--run-only',action='store_true')
args=parser.parse_args()
out=fp.output_dir();out.mkdir(parents=True,exist_ok=True)
sources={'camera':'native_local_camera_stability_gpu.cpp','smooth':'native_local_smooth_normal_gpu.cpp',
         'scale':'native_local_normal_scale_gpu.cpp','legacy':'native_local_legacy_filter_gpu.cpp'}
inputs=[fp.TESTS/name for name in sources.values()]+[fp.src('native_local_light_patch.h'),fp.src('native_local_light_renderer.h'),fp.GENERATED/'local_light_compiled_shaders.h',fp.SHADERS/'local_light_effects.hlsl']
def hashes():return {str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
if not args.run_only:
    fp.use_source_modules()
    from extract_m2_shader_families import collect
    from extract_signatures import variants
    from world_scene_builder import Assets
    (out/'native-vs.bin').write_bytes(collect(fp.client_root())[0][0x78592f36b48da13d][1])
    assets=Assets()
    try:(out/'native-ps.bin').write_bytes(next(variants(assets.read(r'shaders\pixel\ps_3_0\combiners_mod.bls')))[2])
    finally:assets.close()
    sys.path.insert(0,str(fp.SCRIPTS/'shaders'))
    from shader_compiler import Compiler
    source=out/'probes.hlsl'
    source.write_text((fp.SHADERS/'local_light_effects.hlsl').read_text().replace('#include "auto_bump_normal.hlsl"',(fp.SHADERS/'auto_bump_normal.hlsl').read_text())+'''
float4 ProbeView(float3 view:TEXCOORD1):COLOR0{return float4(view,1);}
float4 ProbeNormal(float3 view:TEXCOORD1):COLOR0{float3 p,n;nativeLocalReceiver(view,p,n);return float4(n,1);}
''')
    compiler=Compiler(allow_compatible_binary=True)
    for entry in ('ProbeView','ProbeNormal'):compiler.compile(source,entry,'ps_3_0',out/(entry+'.bin'))
    header=(fp.GENERATED/'local_light_compiled_shaders.h').read_text()
    for count in (1,2,3):
        payload=re.search(r'kNativeLocalLightingLegacy'+str(count)+r'Shader\[\]\s*=\s*\{(.*?)\};',header,re.S)[1]
        header=re.sub(r'(kNativeLocalLighting'+str(count)+r'Shader\[\]\s*=\s*\{).*?(\};)',lambda m:m[1]+payload+m[2],header,flags=re.S)
    baseline=out/'baseline';baseline.mkdir(exist_ok=True);(baseline/'local_light_compiled_shaders.h').write_text(header)
    before=hashes()
    for variant in ('current','baseline'):
        for name,source in sources.items():
            if variant=='baseline' and name=='scale':continue
            command=[fp.zig(),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG',*(['-I',baseline] if variant=='baseline' else []),*fp.test_include_flags(),'-I',fp.TESTS,fp.TESTS/source,'-luser32','-o',out/f'{variant}-{name}.exe']
            with (out/f'{variant}-{name}-compile.log').open('w') as log:subprocess.run(list(map(str,command)),env=fp.zig_env(),stdout=log,stderr=subprocess.STDOUT,check=True,timeout=180)
    if hashes()!=before:raise RuntimeError('Production inputs changed during compilation')
    (out/'source-hashes.json').write_text(json.dumps(before,indent=2))
if args.prepare_only:sys.exit(0)
before=json.loads((out/'source-hashes.json').read_text())
if hashes()!=before:raise RuntimeError('Prepared stability executables are stale')
results={}
for backend,dll in [('native',Path(os.environ.get('WINDIR','C:/Windows'))/'SysWOW64/d3d9.dll'),('dxvk2',fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll'),('dxvk3',fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll')]:
    for variant in ('current','baseline'):
        for name in sources:
            if variant=='baseline' and name=='scale':continue
            tag=f'{backend}-{variant}-{name}'
            env={**fp.zig_env(),'DXVK_LOG_PATH':str(out),'DISABLE_VULKAN_OBS_CAPTURE':'1'}
            for key in ('NORTHLIGHT_EXPECT_FIXED','NORTHLIGHT_EXPECT_LEGACY','NORTHLIGHT_PAD_SLOTS'):env.pop(key,None)
            if variant=='current' and name=='smooth':env['NORTHLIGHT_EXPECT_FIXED']='1'
            if name=='legacy':env['NORTHLIGHT_PAD_SLOTS']='334'
            if variant=='current' and name=='legacy':env['NORTHLIGHT_EXPECT_LEGACY']='1'
            run=subprocess.run(list(map(str,[out/f'{variant}-{name}.exe',dll,out/'native-vs.bin',out/'native-ps.bin',out,'m2',fp.COMPILED])),cwd=out,env=env,capture_output=True,text=True,timeout=180)
            (out/(tag+'.log')).write_text(run.stdout+run.stderr);run.check_returncode();results[tag]=run.stdout
            if name=='camera':
                span=float(re.search(r'maxSpan=([\d.e+-]+)',run.stdout)[1])
                if not (span<.03 if variant=='current' else span>.4):raise RuntimeError(f'{tag}: camera negative control failed ({span})')
            if name=='scale' and max(map(float,re.findall(r'normalError=([\d.e+-]+)',run.stdout)))>.004:raise RuntimeError('Native receiver normal lost unit length')
            if variant=='baseline' and name=='smooth' and 'nonzeroNativeBlockedButKept=10' not in run.stdout:raise RuntimeError('Smooth-normal negative control did not reproduce')
            print(tag,'PASS',flush=True)
    for name in ('legacy',):
        a=re.findall(r'^IMAGE .*$',results[f'{backend}-current-{name}'],re.M)
        b=re.findall(r'^IMAGE .*$',results[f'{backend}-baseline-{name}'],re.M)
        if len(a)!=126 or a!=b:raise RuntimeError('Legacy fallback changed rendered images')
if hashes()!=before:raise RuntimeError('Production inputs changed during GPU validation')
(out/'validation.json').write_text(json.dumps({'source_hashes':before,'results':results,'game_controlled':False,'timing_claims':False,'actual_fountain_receiver_identified':False},indent=2)+'\n')
