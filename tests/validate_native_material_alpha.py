"""Check every mounted SM3 M2 pixel combiner's original alpha and native validator."""
from pathlib import Path
import argparse,json,subprocess,sys
root=Path(__file__).resolve().parents[1];sys.path[:0]=[str(root),str(root/'renderer')]
import northlight_paths as fp
from world_scene_builder import Assets
from extract_signatures import variants
p=argparse.ArgumentParser();p.add_argument('--backend',required=True);p.add_argument('--fused-only',action='store_true');p.add_argument('--msaa',action='store_true');p.add_argument('--packed',action='store_true');args=p.parse_args();backend='native' if 'windows' in args.backend.lower() else 'dxvk'
if args.msaa and not args.fused_only:p.error('--msaa requires --fused-only')
if args.packed and not args.msaa:p.error('--packed requires --msaa')
out=fp.out()/('native-alpha-'+backend+('-fused' if args.fused_only else '')+('-msaa' if args.msaa else '')+('-packed' if args.packed else ''));out.mkdir(exist_ok=True);assets=Assets();files=[]
try:
    for name in assets.providers:
        if name.startswith('shaders\\pixel\\ps_3_0\\combiners_') and name.endswith('.bls'):
            for index,_,code in variants(assets.read(name)):
                file=name.rsplit('\\',1)[1]+f'-{index}.bin';(out/file).write_bytes(code);files.append(file)
finally:assets.close()
(out/'files.txt').write_text('\n'.join(files)+'\n')
if args.packed:
    sys.path.insert(0,str(root/'scripts/shaders'))
    from shader_compiler import Compiler
    probe=out/'packed_probe.hlsl'
    probe.write_text('''#include "auto_bump_capture.hlsl"
float4 ProbeControl:register(c3);
float4 PackMaterialProbe(float2 uv:TEXCOORD0):COLOR0 {
    return float4(m2PackMaterial(tex2D(Diffuse,uv),ProbeControl.x,ProbeControl.y),0,0,0);
}
float4 Fp16MaterialProbe(float2 uv:TEXCOORD0):COLOR0 {
    float4 v=tex2D(Diffuse,uv);
    v.a=1+(1+floor(saturate(v.a)*255+.5))/256;
    return v;
}
''',encoding='utf-8')
    compiler=Compiler(allow_compatible_binary=True)
    for entry in ('PackMaterialProbe','Fp16MaterialProbe'):
        compiler.compile(probe,entry,'ps_3_0',out/(entry+'.bin'))
env=fp.zig_env();env.update(TEMP=str(fp.out()/'tmp'),TMP=str(fp.out()/'tmp'));exe=out/'alpha.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec',*fp.test_include_flags(),str(root/'tests/native_material_alpha_gpu.cpp'),'-luser32','-o',str(exe)],env=env,check=True)
run=subprocess.run([str(exe),str(Path(args.backend).resolve()),str(out),*(['fused'] if args.fused_only else []),*(['msaa'] if args.msaa else []),*(['packed'] if args.packed else [])],cwd=out,env=env,capture_output=True,text=True,timeout=120);(out/'gpu.log').write_text(run.stdout+run.stderr)
if run.returncode:raise RuntimeError(run.stdout+run.stderr)
assert ('NATIVE_M2_FUSED_PASS' if args.fused_only else 'NATIVE_MATERIAL_ALPHA_PASS') in run.stdout;print(run.stdout,flush=True)
if args.msaa:assert 'NATIVE_M2_MSAA_PASS' in run.stdout
if args.packed:assert 'NATIVE_M2_PACKED_PASS' in run.stdout
(out/'validation.json').write_text(json.dumps({'backend':args.backend,'files':len(files),'output':run.stdout},indent=2)+'\n')
