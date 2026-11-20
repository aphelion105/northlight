#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client timing
"""Fine common cascade prototype: image oracle and comparison with the shipped resolve."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT/'scripts/shaders')]
import northlight_paths as fp
from scripts.shaders.shader_sources import copy_sources, source_files
from shader_compiler import Compiler, validate_assembly
from validate_native_scene_trace import processes


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--shader-source', type=Path, default=fp.TESTS/'fixtures/common_shadow_cascade.hlsl')
    parser.add_argument('--backend', type=Path, action='append')
    parser.add_argument('--output', type=Path, default=fp.output_dir()/'common-cascade')
    args = parser.parse_args()
    running = processes()
    out = args.output.resolve(); out.mkdir(parents=True, exist_ok=False)
    inputs = [args.shader_source, *source_files(fp.SHADERS/'world_effects.hlsl'),
              fp.TESTS/'common_shadow_cascade_gpu.cpp',
              fp.TESTS/'composite_distance_gpu.cpp', fp.TESTS/'removal_distance_gpu.cpp',
              fp.src('legacy_fog.h'), fp.src('gpu_frame_timer.h'), Path(__file__)]
    hashes = lambda: {str(p.resolve()): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
    before = hashes()
    source=out/'common.hlsl'
    shutil.copyfile(args.shader_source,source)
    copy_sources(fp.SHADERS/'world_effects.hlsl', out)
    compiler = Compiler(allow_compatible_binary=True)
    report = {'scope':'Authored depth fixture. Stage 0: resolve only. Stage 1: GPU depth union, exact summaries and resolve. Stage 2: summaries and resolve, with a FREE pre-unified depth map. All geometry/raster/cache work is FREE in every stage. An optimistic component comparison, not renderer FPS or runtime acceptance.',
              'inputs':before, 'compiler':compiler.provenance, 'shaders':{}, 'cases':[],
              'uncontended_timing_valid':not any(p['name'].lower() in ('wow.exe','wow64.exe') for p in running)}
    for entry in ('UnifiedDirectionalShadow','CommonReference','CommonDirectional','CommonUnion','CommonBlocks','CommonTiles','ActorTiles','CommonKernel','CommonKernelReference'):
        compiler.compile(source,entry,'ps_3_0',out/(entry+'.bin'))
        budget={'UnifiedDirectionalShadow':1024,'CommonReference':1024,'CommonDirectional':1152}.get(entry,256)
        report['shaders'][entry] = validate_assembly(out/(entry+'.bin.asm'),'ps_3_0',slot_limit=budget)
        print(entry,report['shaders'][entry]['static_instruction_slots'],flush=True)
    exe=out/'common-cascade.exe'
    command=[str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-Wno-return-type',*map(str,fp.test_include_flags()),str(fp.TESTS/'common_shadow_cascade_gpu.cpp'),'-luser32','-o',str(exe)]
    with (out/'compile.log').open('w') as sink:
        subprocess.run(command,env=fp.zig_env(),stdout=sink,stderr=subprocess.STDOUT,check=True,timeout=180)
    for i,backend in enumerate(args.backend or [Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll',fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll',fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll']):
        run=out/str(i);run.mkdir();(run/'dxvk.conf').write_text('d3d9.presentInterval = 0\ndxvk.enableStateCache = False\n')
        env=os.environ.copy();env.update(DXVK_CONFIG_FILE=str(run/'dxvk.conf'),DXVK_LOG_PATH=str(run),DISABLE_VULKAN_OBS_CAPTURE='1')
        result=subprocess.run([str(exe),str(backend.resolve()),str(out)+os.sep],env=env,cwd=run,capture_output=True,text=True,timeout=600,creationflags=subprocess.CREATE_NO_WINDOW)
        text=result.stdout+result.stderr;(run/'gpu.log').write_text(text)
        print(text[-2400:],flush=True)
        assert result.returncode==0 and 'COMMON_CASCADE_PASS cases=30' in text,result.returncode
        rows=[dict(re.findall(r'(\w+)=([^ ]+)',line)) for line in text.splitlines() if line.startswith('TIMING ') and 'warm=0 ' in line]
        observed=[]
        for stage in (0,1,2):
            for shape in (0,1,2,3,4,8):
                record={'stage':stage,'shape':shape}
                for mode in (0,1):
                    samples=[float(r['gpuMs']) for r in rows if int(r['shape'])==shape and int(r['common'])==mode and int(r['stage'])==stage]
                    assert len(samples)==6 and all(v>=0 for v in samples)
                    record['common_ms' if mode else 'baseline_ms']=statistics.median(samples)
                    record['common_samples_ms' if mode else 'baseline_samples_ms']=samples
                observed.append(record)
        def records(prefix):
            return [dict(re.findall(r'(\w+)=([^ ]+)',line)) for line in text.splitlines() if line.startswith(prefix+' ')]
        images,kernels=records('IMAGE'),records('KERNEL')
        assert len(images)==len(kernels)==30
        report['cases'].append({'backend':str(backend),'observed_gpu_times':observed,
                                'images':images,'kernels':kernels,'log':str(run/'gpu.log')})
    assert hashes()==before,'Source changed during the GPU test'
    report['pass']=True
    report['runtime_acceptance']=False
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS',out)


if __name__=='__main__':
    main()
