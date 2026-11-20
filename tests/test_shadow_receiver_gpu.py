# northlight-test: requires=zig,windows-gpu,client
"""Receiver-plane continuity, crease bias and independent blockers on D3D9."""
import json
import os
from pathlib import Path
import subprocess
import sys

sys.path[:0] = [str(Path(__file__).resolve().parents[1]),
               str(Path(__file__).resolve().parents[1]/'scripts/shaders')]
import northlight_paths as fp
from scripts.shaders.shader_sources import world_source_text
from shader_compiler import Compiler

out = fp.output_dir()
world = world_source_text(fp.SHADERS/'world_effects.hlsl').replace(
    '"auto_bump_normal.hlsl"', '"'+(fp.SHADERS/'auto_bump_normal.hlsl').as_posix()+'"')
query = '''
float4 Fixture:register(c116);
float4 ReceiverQuery(float2 uv:TEXCOORD0):COLOR0 {
 float x=(uv.x-.5)*16;
 float3 p=float3(x,0,Fixture.x*2*abs(x)+(1-Fixture.x)*2*x);
 float4 normal=tex2Dlod(NormalBuffer,float4(uv,0,0));
 RECEIVER
 float v=directionalShadowFiltered(p+normal.xyz*(.08*saturate(normal.w)),normal.xyz,saturate(normal.w),false,normal.w);
 return v.xxxx;
}
'''
compiler = Compiler(allow_compatible_binary=True)
for name, receiver in [('current', 'normal=shadowReceiver(uv,normalizedDepth(depthUV(uv)),normal);'),
                       ('negative', '')]:
    body = world if name == 'current' else world.replace(
        'return result*shadowReceiverFacing(facing,confidence);', 'return result;')
    source = out/(name+'.hlsl');source.write_text(body+query.replace('RECEIVER', receiver))
    compiler.compile(source, 'ReceiverQuery', 'ps_3_0', out/(name+'.bin'))
support = (fp.TESTS/'shadow_transition_gpu.cpp').read_text().split('int main(int argc,')[0]
source = out/'oracle.cpp';source.write_text(support+(fp.TESTS/'shadow_receiver_gpu.cpp').read_text())
exe = out/'shadow-receiver.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                *fp.test_include_flags(), str(source), '-luser32', '-o', str(exe)], env=fp.zig_env(), check=True)
backends = [Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll',
            fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll',
            fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll']
runs = []
for index, backend in enumerate(backends):
    run = out/str(index);run.mkdir(exist_ok=True)
    result = subprocess.run([str(exe), str(backend), str(out)], cwd=run,
        env={**os.environ, 'DXVK_LOG_PATH': str(run), 'DISABLE_VULKAN_OBS_CAPTURE': '1'},
        capture_output=True, text=True, timeout=120)
    (run/'gpu.log').write_text(result.stdout+result.stderr)
    print(result.stdout+result.stderr, flush=True);result.check_returncode()
    assert 'SHADOW_RECEIVER_PASS' in result.stdout
    runs.append(dict(backend=str(backend), result=result.stdout))
(out/'validation.json').write_text(json.dumps(dict(pass_=True, runs=runs, timing_valid=False), indent=2)+'\n')
