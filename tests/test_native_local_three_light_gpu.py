#!/usr/bin/env python3
# northlight-test: requires=client,stormlib,zig,windows-gpu manual
"""Exact three-source native endpoints, physical-slot permutations and GPU displacement."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
from extract_m2_shader_families import collect
from extract_signatures import variants
from world_scene_builder import Assets

out = fp.output_dir(); out.mkdir(parents=True, exist_ok=True)
inputs = [fp.TESTS / 'native_local_three_light_gpu.cpp', fp.TESTS / 'native_local_sampler_read_checks.h', fp.REPO / 'src/lights/native_local_light_patch.h', fp.REPO / 'src/lights/native_local_light_renderer.h', fp.GENERATED / 'local_light_compiled_shaders.h', fp.SHADERS / 'local_light_effects.hlsl']
def hashes():
    return {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
before = hashes()
vs = collect(fp.client_root())[0][0x78592f36b48da13d][1]
assets = Assets()
try:
    ps = next(variants(assets.read(r'shaders\pixel\ps_3_0\combiners_mod.bls')))[2]
finally:
    assets.close()
(out / 'native-vs.bin').write_bytes(vs); (out / 'native-ps.bin').write_bytes(ps)
exe = out / 'native-three-light.exe'
command = [fp.zig(), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG', *fp.test_include_flags(), fp.TESTS / 'native_local_three_light_gpu.cpp', '-luser32', '-o', exe]
with (out / 'compile.log').open('w') as log:
    subprocess.run(list(map(str, command)), env=fp.zig_env(), stdout=log, stderr=subprocess.STDOUT, check=True, timeout=180)
runs = {}
backends = [('native', Path(os.environ.get('WINDIR', 'C:/Windows')) / 'SysWOW64/d3d9.dll'), ('dxvk2', fp.client_root() / 'renderer-backends/dxvk2/dxvk2_d3d9.dll'), ('dxvk3', fp.client_root() / 'renderer-backends/dxvk/dxvk_d3d9.dll')]
for name, dll in backends:
    for displaced in (False, True):
        tag = name + ('-displaced' if displaced else '-native')
        run = out / tag; run.mkdir(exist_ok=True)
        env = {**fp.zig_env(), 'DXVK_LOG_PATH': str(run), 'DISABLE_VULKAN_OBS_CAPTURE': '1'}
        env.pop('NORTHLIGHT_THREE_DISPLACEMENT', None)
        if displaced: env['NORTHLIGHT_THREE_DISPLACEMENT'] = '1'
        result = subprocess.run(list(map(str, [exe, dll, out / 'native-vs.bin', out / 'native-ps.bin', run, 'm2', fp.COMPILED])), env=env, cwd=run, capture_output=True, text=True, timeout=180)
        (run / 'gpu.log').write_text(result.stdout + result.stderr)
        print(tag, result.stdout, flush=True); result.check_returncode(); runs[tag] = result.stdout.strip()
assert before == hashes(), 'Production input changed during correctness validation'
(out / 'validation.json').write_text(json.dumps({'source_hashes': before, 'runs': runs, 'game_controlled': False, 'timing_claims': False, 'limits': ['Four matching lights still have at most three corrected sources.', 'Three-source POM is rejected when reserved TEXCOORDs leave fewer than six free outputs.', 'No current fountain capture ties this controlled reproduction to the reported draw.']}, indent=2) + '\n')
