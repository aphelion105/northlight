# northlight-test: requires=zig,windows-gpu,client
"""GI coverage across solid probe corners and uncertain receiver normals."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

sys.path[:0] = [str(Path(__file__).resolve().parents[1]),
               str(Path(__file__).resolve().parents[1]/'scripts/shaders')]
import northlight_paths as fp
from scripts.shaders.shader_sources import source_files, world_source_text
from shader_compiler import Compiler

out = fp.output_dir()
world = world_source_text(fp.SHADERS/'world_effects.hlsl').replace(
    '"auto_bump_normal.hlsl"', '"'+(fp.SHADERS/'auto_bump_normal.hlsl').as_posix()+'"')
query = '''
float4 ReceiverNormal:register(c116);
float4 ReceiverPosition:register(c117);
GIOutput ResidencyQuery(float2 uv:TEXCOORD0) {
 GIOutput o;
 float3 p=ReceiverPosition.xyz+float3(uv.x*ReceiverPosition.w,0,0);
 o.correction=float4(worldGIValue(p,ReceiverNormal,false,o.prelit),0);
 return o;
}
'''
compiler = Compiler(allow_compatible_binary=True)
broken = world.replace('metadata.w!=-1', 'metadata.w>=0').replace(
    'float skyView=saturate(.5+.5*n.z);', 'float skyView=lerp(1,saturate(.5+.5*n.z),smoothNormal.w);')
assert broken != world
for name, source in [('current', world), ('negative', broken)]:
    path = out/(name+'.hlsl')
    path.write_text(source+query)
    compiler.compile(path, 'ResidencyQuery', 'ps_3_0', out/(name+'.bin'))
exe = out/'gi-residency.exe'
support = (fp.TESTS/'shadow_transition_gpu.cpp').read_text().split('int main(int argc,')[0]
source = out/'oracle.cpp'
source.write_text(support+(fp.TESTS/'gi_residency_gpu.cpp').read_text())
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                *fp.test_include_flags(), str(source),
                str(fp.src('world_gi.cpp')), '-luser32', '-o', str(exe)], env=fp.zig_env(), check=True)
backends = [Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll',
            fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll',
            fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll']
runs = []
for i, backend in enumerate(backends):
    run = out/str(i); run.mkdir()
    result = subprocess.run([str(exe), str(backend), str(out)], cwd=run,
        env={**os.environ, 'DXVK_LOG_PATH': str(run), 'DISABLE_VULKAN_OBS_CAPTURE': '1'},
        capture_output=True, text=True, timeout=120)
    (run/'gpu.log').write_text(result.stdout+result.stderr)
    print(result.stdout, flush=True)
    result.check_returncode()
    assert 'GI_RESIDENCY_PASS' in result.stdout
    runs.append(dict(backend=str(backend), result=result.stdout))
inputs = [Path(__file__), fp.TESTS/'gi_residency_gpu.cpp', *source_files(fp.SHADERS/'world_effects.hlsl'),
          fp.src('probe_activation.h'), fp.src('probe_texture_upload.h')]
(out/'validation.json').write_text(json.dumps(dict(pass_=True, timing_valid=False, runs=runs,
    sha256={str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}), indent=2)+'\n')
