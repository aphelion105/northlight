#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig timing
"""Shared shadow resolve versus the existing actor/world passes, including GPU timing."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT/'scripts/shaders')]
import northlight_paths as fp
from scripts.shaders.shader_sources import source_files, world_source_bytes
from shader_compiler import Compiler, validate_assembly
from validate_native_scene_trace import processes


def main():
    p = argparse.ArgumentParser(__doc__)
    p.add_argument('--shader-source', type=Path, default=fp.SHADERS/'world_effects.hlsl')
    p.add_argument('--backend', type=Path, action='append')
    p.add_argument('--output', type=Path, default=fp.output_dir()/'shared-directional')
    a = p.parse_args()
    running = processes()
    inputs = [a.shader_source, *source_files(fp.SHADERS/'world_effects.hlsl'),
              fp.TESTS/'shared_directional_gpu.cpp', fp.TESTS/'composite_distance_gpu.cpp',
              fp.src('gpu_frame_timer.h'), Path(__file__)]
    hashes = lambda: {str(path.resolve()): hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs}
    before = hashes()
    out = a.output.resolve(); out.mkdir(parents=True, exist_ok=False)
    source = out/'shared.hlsl'
    source.write_bytes(world_source_bytes(a.shader_source) + b'\nsampler2D ReferenceActor:register(s8);\nsampler2D ReferenceWorld:register(s9);\nfloat4 CombineReference(float2 uv:TEXCOORD0):COLOR0 {float v=min(tex2Dlod(ReferenceActor,float4(uv,0,0)).r,tex2Dlod(ReferenceWorld,float4(uv,0,0)).r);return float4(v,v,v,1);}\n')
    (out/'auto_bump_normal.hlsl').write_bytes((fp.SHADERS/'auto_bump_normal.hlsl').read_bytes())
    compiler = Compiler(allow_compatible_binary=True)
    report = {'scope':'Authored 1920x1080 resolve workload; no gameplay FPS claim. Baseline timing excludes the comparison-only combine draw.',
              'source_sha256':before[str(a.shader_source.resolve())], 'inputs':before,
              'game_running':any(p['name'].lower() in ('wow.exe','wow64.exe') for p in running),
              'compiler':compiler.provenance, 'shaders':{}, 'cases':[]}
    for entry in ('ActorShadow','DirectionalShadow','CombineReference','UnifiedDirectionalShadow'):
        compiler.compile(source,entry,'ps_3_0',out/(entry+'.bin'))
        report['shaders'][entry] = validate_assembly(out/(entry+'.bin.asm'),'ps_3_0',slot_limit=1024 if entry=='UnifiedDirectionalShadow' else 512)
    exe = out/'shared-directional.exe'
    cmd = [str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-Wno-return-type',*map(str,fp.test_include_flags()),str(fp.TESTS/'shared_directional_gpu.cpp'),'-luser32','-o',str(exe)]
    with (out/'compile.log').open('w') as sink:
        subprocess.run(cmd,env=fp.zig_env(),stdout=sink,stderr=subprocess.STDOUT,check=True,timeout=180)
    for i,backend in enumerate(a.backend or [Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll',fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll',fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll']):
        run=out/str(i);run.mkdir();(run/'dxvk.conf').write_text('d3d9.presentInterval = 0\ndxvk.enableStateCache = False\n')
        env=os.environ.copy();env.update(DXVK_CONFIG_FILE=str(run/'dxvk.conf'),DXVK_LOG_PATH=str(run),DISABLE_VULKAN_OBS_CAPTURE='1')
        result=subprocess.run([str(exe),str(backend.resolve()),str(out)+os.sep],env=env,cwd=run,capture_output=True,text=True,timeout=240,creationflags=subprocess.CREATE_NO_WINDOW)
        text=result.stdout+result.stderr;(run/'gpu.log').write_text(text)
        print(text[-1800:],flush=True)
        assert result.returncode==0 and 'SHARED_DIRECTIONAL_PASS ' in text, result.returncode
        rows=[dict(re.findall(r'(\w+)=([^ ]+)',line)) for line in text.splitlines() if line.startswith('TIMING ') and 'warm=0 ' in line]
        observed=[]
        for shape in (0,1,2,4,7):
            record={'shape':shape}
            for mode in (0,1):
                samples=[float(r['gpuMs']) for r in rows if int(r['shape'])==shape and int(r['unified'])==mode]
                assert len(samples)==6 and all(v>=0 for v in samples)
                record['shared_ms' if mode else 'baseline_ms']=statistics.median(samples)
            observed.append(record)
        report['cases'].append({'backend':str(backend),'observed_gpu_times':observed,'uncontended_timing_valid':not report['game_running'],'log':str(run/'gpu.log')})
    assert hashes()==before, 'Source changed during the GPU test'
    report['pass']=True
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS',out)


if __name__=='__main__':
    main()
