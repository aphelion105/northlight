# northlight-test: requires=windows-gpu,zig,client
"""Subtexel contact/soft shadow motion, cube seams and the previous-filter control."""
import json
import os
from pathlib import Path
import subprocess
import sys
sys.path[:0] = [str(Path(__file__).resolve().parents[1]),
               str(Path(__file__).resolve().parents[1]/'scripts/shaders')]
import northlight_paths as fp
import windows_gpu_guard
from shader_compiler import Compiler

windows_gpu_guard.processes(allow_game=True)  # Images only; no timing claims.
out = fp.output_dir()
body = (fp.SHADERS/'local_light_effects.hlsl').read_text().replace(
    '"auto_bump_normal.hlsl"', '"'+(fp.SHADERS/'auto_bump_normal.hlsl').as_posix()+'"')
old = (fp.TESTS/'fixtures/reference/local-shadow-filter-12b96df9.hlsl').read_text()
query = '''
float4 Direction:register(c80);float4 Along:register(c81);
float4 Sweep:register(c82);float4 SurfaceNormal:register(c83);
float4 Query(float2 uv:TEXCOORD0):COLOR0 {
 float3 p=(Direction.xyz+Along.xyz*((uv.x-.5)*Sweep.x))*Sweep.y;
 return FUNCTION(p,SurfaceNormal.xyz,length(p),SOFT).xxxx;
}
'''
compiler = Compiler(allow_compatible_binary=True)
for version in ['current', 'previous']:
    for soft in [False, True]:
        name = version+('-soft' if soft else '-hard')
        function = 'localVisibilityInRange' if version == 'current' else 'previousLocalVisibilityInRange'
        source = out/(name+'.hlsl')
        source.write_text(body+old+query.replace('FUNCTION', function).replace('SOFT', str(soft).lower()))
        compiler.compile(source, 'Query', 'ps_3_0', out/(name+'.bin'))
exe = out/'continuity.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG',
    '-std=c++17', *fp.test_include_flags(), str(fp.TESTS/'local_shadow_continuity_gpu.cpp'),
    '-luser32', '-o', str(exe)], env=fp.zig_env(), check=True)
runs = []
for index, backend in enumerate([Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll',
        fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll',
        fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll']):
    folder = out/str(index);folder.mkdir(exist_ok=True)
    run = subprocess.run([str(exe), str(backend), str(out)], cwd=folder,
        env={**os.environ, 'DXVK_LOG_PATH': str(folder), 'DISABLE_VULKAN_OBS_CAPTURE': '1'},
        capture_output=True, text=True, timeout=180)
    (folder/'gpu.log').write_text(run.stdout+run.stderr)
    print(run.stdout+run.stderr, flush=True);run.check_returncode()
    assert 'LOCAL_SHADOW_CONTINUITY_PASS' in run.stdout
    runs.append(dict(backend=str(backend), output=run.stdout))
(out/'validation.json').write_text(json.dumps(dict(pass_=True, runs=runs,
    scope='Analytical cube boundaries and subtexel receiver motion; not a video-scene replay or performance measurement.'), indent=2)+'\n')
