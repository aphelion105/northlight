#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig manual
"""Compare material ray output and GPU work against the full-work reference paths."""
from pathlib import Path
import argparse
import json
import os
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
sys.path.insert(0, str(fp.REPO / 'scripts/shaders'))
from shader_compiler import Compiler, validate_assembly

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--compatible-compiler", action="store_true")
args = parser.parse_args()
out = fp.output_dir()
out.mkdir(parents=True, exist_ok=True)
source = (fp.SHADERS / 'auto_parallax.hlsl').read_text()
# The reference completes every shadow tap, including fully blocked rays.
reference = '''
float referenceShadow(sampler2D map,float2 uv,float height,float distance,MaterialFrame f,float4 control,float4 light,float steps) {
    float3 ray=float3(dot(light.xyz,f.tangent),dot(light.xyz,f.bitangent),dot(light.xyz,f.normal));
    float fade=parallaxFade(distance,f.tx,f.ty,control.y);
    if(control.x<=0||light.w<=0||ray.z<=.04||fade<=0)return 1;
    float span=1-height;
    float2 extent=clamp(ray.xy/max(ray.z,.08),-6,6)*control.x*span*fade;
    float blocked=0;
    [loop]for(int i=1;i<=16&&i<=steps;++i){
        float part=i/steps;
        float h=tex2Dgrad(map,uv+extent*part,f.tx,f.ty).b;
        blocked=max(blocked,saturate((h-height-span*part-.012)*16));
    }
    return 1-blocked;
}
'''
fixture = '''
sampler2D HeightMap:register(s0);
float4 Projection:register(c0);
float4 Control:register(c1);
float4 ShadowControl:register(c2);
float4 Ray(float2 uv:TEXCOORD0,float4 p:TEXCOORD1):COLOR0 {
    return float4(parallaxUV(HeightMap,uv,p,Projection,Control),0,1);
}
float4 ReferenceRay(float2 uv:TEXCOORD0,float4 p:TEXCOORD1):COLOR0 {
    MaterialFrame f=materialFrame(uv,p,Projection);
    return float4(parallaxUVFrame(HeightMap,uv,p.w,Control,f),0,1);
}
float4 Shadow(float2 uv:TEXCOORD0,float4 p:TEXCOORD1):COLOR0 {
    MaterialFrame f=materialFrame(uv,p,Projection);
    return parallaxShadow(HeightMap,uv,ShadowControl.x,p.w,f,Control,float4(.96,0,.28,1),ShadowControl.y);
}
float4 ReferenceShadow(float2 uv:TEXCOORD0,float4 p:TEXCOORD1):COLOR0 {
    MaterialFrame f=materialFrame(uv,p,Projection);
    return referenceShadow(HeightMap,uv,ShadowControl.x,p.w,f,Control,float4(.96,0,.28,1),ShadowControl.y);
}
'''
path = out / 'material-rays.hlsl'
path.write_text(source + reference + fixture)
compiler = Compiler(allow_compatible_binary=args.compatible_compiler)
for entry in ('Ray', 'ReferenceRay', 'Shadow', 'ReferenceShadow'):
    compiler.compile(path, entry, 'ps_3_0', out / (entry + '.bin'))
    validate_assembly(out / (entry + '.bin.asm'), 'ps_3_0')
exe = out / 'material-ray-work.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG', '-std=c++17',
                *fp.test_include_flags(), str(fp.TESTS / 'material_ray_work_gpu.cpp'), '-luser32', '-o', str(exe)],
               env=fp.zig_env(), check=True)
backend = os.environ.get('NORTHLIGHT_TEST_D3D9', str(Path(os.environ.get('SystemRoot', 'C:/Windows')) / 'SysWOW64/d3d9.dll'))
subprocess.run([str(exe), backend, str(out)], cwd=out, check=True, timeout=120)
rows = json.loads((out / 'material-ray-work.json').read_text())
assert len(rows) == 18 and all(row['max_error'] <= .00001 for row in rows)
print('PASS 18 paired material ray cases, including fade boundaries, blocked and unblocked shadow rays')
