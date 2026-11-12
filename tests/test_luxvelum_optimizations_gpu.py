#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client
"""Fog references, independent dry distance and fullscreen MSAA coverage on native D3D9/DXVK."""
import hashlib
import json
import os
import re
import shutil
from pathlib import Path
import subprocess
import sys

sys.path[:0] = [str(Path(__file__).resolve().parents[1]), str(Path(__file__).resolve().parents[1] / 'scripts')]
import northlight_paths as fp
fp.use_source_modules()
from shader_compiler import Compiler, validate_assembly
from windows_gpu_guard import processes

out = fp.output_dir()
env = dict(os.environ)
baseline_ref = env.get('NORTHLIGHT_FOG_BENCHMARK_FROM')
if baseline_ref:
    baseline_ref = subprocess.check_output(['git', 'rev-parse', '--verify', baseline_ref + '^{commit}'], cwd=fp.REPO, text=True).strip()
    baseline = out / 'baseline'
    baseline.mkdir()
    for name in ('ForeverFogActor', 'ForeverFogActorNoNoise'):
        (baseline / (name + '.bin')).write_bytes(subprocess.check_output(['git', 'show', baseline_ref + ':shaders/compiled/' + name + '.bin'], cwd=fp.REPO))
    env.update(NORTHLIGHT_OPTIMIZATION_BENCHMARK='1', NORTHLIGHT_FOG_BENCHMARK_BASELINE=str(baseline))
compiler = Compiler(allow_compatible_binary=True)
reference = (fp.TESTS / 'fixtures/forever_fog_reference.hlsl').read_text()
for name in ('world_effects.hlsl', 'shadow_transition.hlsl', 'auto_bump_normal.hlsl'):
    shutil.copyfile(fp.SHADERS / name, out / name)
for transition in (False, True):
    source = out / ('transition-reference.hlsl' if transition else 'world-reference.hlsl')
    include = 'shadow_transition.hlsl' if transition else 'world_effects.hlsl'
    source.write_text(f'#include "{include}"\n' + reference, encoding='utf-8', newline='\n')
    for actor in (False, True):
        entry = 'ForeverFogActorReference' if actor else 'ForeverFogReference'
        binary = out / (entry + ('Transition' if transition else '') + '.bin')
        compiler.compile(source, entry, 'ps_3_0', binary)
        validate_assembly(Path(str(binary) + '.asm'), 'ps_3_0', slot_limit=768)
source = out / 'coverage.hlsl'
source.write_text('float4 Coverage(float2 uv:TEXCOORD0):COLOR0{return float4(uv,uv.x*uv.y,1);}\n')
compiler.compile(source, 'Coverage', 'ps_3_0', out / 'Coverage.bin')
for name in ('ForeverFogNoNoise', 'ForeverFogActorNoNoise', 'ForeverFogNoNoiseTransition', 'ForeverFogActorNoNoiseTransition'):
    assembly = (fp.COMPILED / (name + '.bin.asm')).read_text()
    assert 'dcl_volume' not in assembly, name
exe = out / 'luxvelum-gpu.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                *fp.test_include_flags(), str(fp.TESTS / 'luxvelum_optimizations_gpu.cpp'), '-luser32', '-o', str(exe)], env=fp.zig_env(), check=True)
backends = [Path(os.environ['WINDIR']) / 'SysWOW64/d3d9.dll', fp.client_root() / 'renderer-backends/dxvk/dxvk_d3d9.dll']
results = []
for backend in backends:
    processes()
    result = subprocess.run([str(exe), str(fp.COMPILED), str(out), str(backend)], cwd=out, env=env, capture_output=True, text=True, timeout=180)
    (out / (backend.parent.name + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
    print(result.stdout + result.stderr, flush=True)
    result.check_returncode()
    assert 'LUXVELUM_GPU_PASS' in result.stdout
    timings = [dict(re.findall(r'(\w+)=([^ ]+)', line)) for line in result.stdout.splitlines() if line.startswith('LUXVELUM_FOG_GPU_MS ')]
    if baseline_ref:
        assert {(int(row['noise']), int(row['state'])) for row in timings} == {(noise, state) for noise in range(2) for state in range(6)}
    results.append({'backend': str(backend), 'sha256': hashlib.sha256(backend.read_bytes()).hexdigest(), 'timings': timings, 'stdout': result.stdout})
(out / 'validation.json').write_text(json.dumps({'pass': True, 'game_launched': False, 'baseline_ref': baseline_ref, 'results': results}, indent=2) + '\n')
