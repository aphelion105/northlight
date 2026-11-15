"""Replay supplied interior buffers and point cubes; not a whole-scene/FPS replay."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
import windows_gpu_guard
from render_lamp_self_shadow import load, capture_plan, replay_contract


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, action='append', required=True)
    parser.add_argument('--benchmark', action='store_true')
    args = parser.parse_args()
    out = fp.output_dir(); out.mkdir(parents=True, exist_ok=True)
    before = windows_gpu_guard.processes(allow_game=not args.benchmark)
    paths = [fp.TESTS/name for name in ('lamp_capture_composite_gpu.cpp', 'captured_point_pass_gpu.cpp', 'lighting_pair_timing.h')]
    paths += [fp.GENERATED/name for name in ('world_compiled_shaders.h','local_light_compiled_shaders.h')]
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    inputs = {str(p): sha(p) for p in paths}
    executables = {}
    for stem in ('lamp_capture_composite_gpu', 'captured_point_pass_gpu'):
        exe = out/(stem+'.exe'); executables[stem] = exe
        subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG',
                        '-std=c++17', *fp.test_include_flags(), str(fp.TESTS/(stem+'.cpp')),
                        '-luser32', '-o', str(exe)], env=fp.zig_env(), check=True)
    report = dict(passed=False, source_inputs=inputs, captures=[], runs=[], processes_before=before,
                  scope='Exact exported buffers/cubes and current compositors. No geometry, stencil or complete scene replay; no game FPS claim.')
    try:
        for capture in args.capture:
            capture = capture.resolve(); prefix = str(capture/'capture-1-')
            journal = capture/'capture-1-lighting-inputs.jsonl'
            rows = [json.loads(s) for s in journal.read_text(encoding='utf-8').splitlines()]
            plan = capture_plan(rows); replay_contract(rows, prefix, plan)
            points = [r for r in rows if r['event']=='pass' and r['entry']=='LocalLightingPcss']
            assert points and all(p['first']==i and p['index']==points[0]['index']+i and p['submitted'] for i,p in enumerate(points))
            for slot in (0,2,3,4,5):
                bindings=[r for r in rows if r['event']=='input-binding' and r['group']==4 and r['slot']==slot]
                assert bindings and len({r['null'] for r in bindings})==1
                assert (capture/f'capture-1-lighting-input-g4-s{slot}.fgr').exists()==(not bindings[0]['null'])
            shader = next(r['shader'] for r in rows if r['event']=='shader-binding' and r['pass']==points[0]['index'])
            report['captures'].append(dict(path=str(capture), journal_sha256=sha(journal),
                files={p.name:sha(p) for p in capture.iterdir() if p.suffix in ('.fgr','.f32','.bin') and '-native-' not in p.name}))
            for backend in (Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll',
                            fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll',
                            fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll'):
                folder = out/capture.parent.name/backend.parent.name; folder.mkdir(parents=True,exist_ok=True)
                env = {**fp.zig_env(), 'DISABLE_VULKAN_OBS_CAPTURE':'1', 'DXVK_LOG_PATH':str(folder)}
                def run(name, command, environment=env):
                    result = subprocess.run(list(map(str,command)), cwd=folder, env=environment, capture_output=True, text=True, timeout=180)
                    (folder/(name+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
                    result.check_returncode(); return result.stdout
                run('points',[executables['captured_point_pass_gpu'],backend,prefix,folder,points[0]['index'],len(points),shader])
                point_results=[]
                for i in range(len(points)):
                    actual,clear,blocked=[load(folder/f'point-{i}-{mode}.fgr')[...,:3] for mode in ('captured','clear','blocked')]
                    assert all(np.isfinite(a).all() for a in (actual,clear,blocked))
                    assert abs(clear).max()<.00001 and actual.max()<.00001
                    assert (blocked-actual).max()<.001
                    energy=-blocked.min(axis=2)
                    signal=energy>.01
                    occlusion=np.divide(-actual.min(axis=2),energy,out=np.zeros_like(energy),where=signal)
                    point_results.append(dict(slot=i,signal_pixels=int(signal.sum()),
                        fully_occluded_pixels=int(((occlusion>.99)&signal).sum()),
                        partial_pixels=int(((occlusion>.01)&(occlusion<.99)&signal).sum()),
                        correction_min=float(actual.min())))
                assert sum(p['fully_occluded_pixels'] for p in point_results)>100
                command=[executables['lamp_capture_composite_gpu'],backend,prefix,folder,'authored',plan['material_pass'],0,plan['material_group'],int(plan['hdr'])]
                timing=[]
                for control in ([True,False] if args.benchmark else [False]):
                    bench_env=env.copy()
                    if args.benchmark:bench_env['NORTHLIGHT_LIGHTING_BENCH']='1'
                    if control:bench_env['NORTHLIGHT_LIGHTING_CONTROL']='1'
                    output=run('composite-AA' if control else 'composite-AB',command,bench_env)
                    timing += [json.loads(line.removeprefix('LIGHTING_COST ')) for line in output.splitlines() if line.startswith('LIGHTING_COST ')]
                old,new=[load(folder/f'world-{mode}.fgr') for mode in ('before','after')]
                recorded=load(capture/f"capture-1-lighting-input-g{plan['material_group']}-s0.fgr")
                replay_error=float(np.abs(old[...,:3]-recorded[...,:3]).mean())
                assert replay_error<.005 and np.isfinite(new).all()
                costs={case:{str(mode):dict(gpu_ms=statistics.median(r['gpu_ms'] for r in timing if r['case']==case and r['variant']==mode),
                    cpu_ms=statistics.median(r['cpu_ms'] for r in timing if r['case']==case and r['variant']==mode)) for mode in (0,1)} for case in sorted({r['case'] for r in timing})}
                result=dict(capture=str(capture),backend=str(backend),point_geometry=point_results,
                    old_replay_mean_abs_error=replay_error,costs=costs,timing=timing)
                report['runs'].append(result)
                print(json.dumps({k:v for k,v in result.items() if k not in ('timing',)}),flush=True)
        assert all(sha(Path(p))==value for p,value in inputs.items())
        report['processes_after']=windows_gpu_guard.processes(allow_game=not args.benchmark)
        report['passed']=True
    finally:
        (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':
    main()
