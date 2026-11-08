#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client manual timing
"""Exact captured PS recipes: factory/creation/first-draw/real EVENT attribution."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
from validate_native_scene_trace import processes

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--backend', type=Path, action='append')
p.add_argument('--header-override', type=Path, help='Experimental generated helper header, outside production')
a = p.parse_args()
out = a.output.resolve(); out.mkdir(parents=True, exist_ok=False)
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
def fnv(path):
    h = 14695981039346656037
    for c in path.read_bytes(): h = ((h ^ c)*1099511628211) & ((1 << 64)-1)
    return f'{h:016x}'

vs = fp.REPO/'out/native-pom-r10-verified/m2/native-vs.bin'
recipes = {'ac008ac06cd451a9': fp.REPO/'out/native-pom-r10-verified/m2/native-ps.bin',
           '777239a63edea3a1': fp.REPO/'out/event-rootcause-20261107-r14/lamp/diagnose/captured-m2/native-ps.bin'}
assert fnv(vs) == 'e8d455338d8ae2ff'
assert all(fnv(path) == key for key,path in recipes.items())
inputs = {str(path):sha(path) for path in [*[f for f in fp.SRC.rglob('*') if f.is_file()], vs, *recipes.values(), Path(__file__), fp.TESTS/'native_local_light_gpu.cpp', fp.TESTS/'native_event_cold_checks.h', fp.TESTS/'native_local_sampler_read_checks.h']}
if a.header_override: inputs[str(a.header_override.resolve())] = sha(a.header_override)
initial = processes(); contended = any(item['name'].lower() in ('wow.exe','wow64.exe') for item in initial)
exe = out/'native-event-cold.exe'
extra = ['-I', a.header_override.resolve().parent] if a.header_override else []
cmd = [fp.zig(), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG', '-DNORTHLIGHT_TEST_EVENT_COLD', *extra, *fp.test_include_flags(), fp.TESTS/'native_local_light_gpu.cpp', '-luser32', '-o', exe]
with (out/'compile.log').open('w') as log:
    subprocess.run(list(map(str,cmd)), env=fp.zig_env(), stdout=log, stderr=subprocess.STDOUT, check=True, timeout=180)
backends = a.backend or [Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll', fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll', fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll']
results = []
for i,backend in enumerate(backends):
    for source,ps in recipes.items():
        for fence in (False,True):
            run = out/f'{i}-{source}-fence{int(fence)}'; run.mkdir()
            config = run/'dxvk.conf'; config.write_text('dxvk.enableStateCache = False\nd3d9.presentInterval = 0\n')
            env = {**fp.zig_env(), 'DXVK_LOG_PATH':str(run), 'DXVK_CONFIG_FILE':str(config), 'DISABLE_VULKAN_OBS_CAPTURE':'1', 'NORTHLIGHT_TEST_CORRECTNESS_ONLY':'1'}
            if fence: env['NORTHLIGHT_TEST_COLD_CREATE_FENCE'] = '1'
            other = next(path for key,path in recipes.items() if key!=source)
            r = subprocess.run(list(map(str,[exe,backend.resolve(),vs,ps,run,'m2',fp.COMPILED,other])), cwd=run, env=env, capture_output=True, text=True, timeout=120)
            text = r.stdout+r.stderr; (run/'gpu.log').write_text(text, encoding='utf-8'); print(i,source,'fence',int(fence),text,flush=True)
            r.check_returncode(); assert 'NATIVE_EVENT_COLD_PASS ' in text
            rows = [dict(re.findall(r'(\w+)=([^ ]+)',line)) for line in text.splitlines() if line.startswith('NATIVE_EVENT_COLD ')]
            assert len(rows)==8
            results.append({'backend':str(backend.resolve()),'backend_sha256':sha(backend),'source_ps':source,'creation_fence':fence,'rows':rows,'log':str(run/'gpu.log')})
assert all(sha(Path(path))==digest for path,digest in inputs.items()), 'Inputs changed during validation'
report = {'pass':True,'inputs':inputs,'command':list(map(str,cmd)),'cases':results,'game_running_at_start':contended,'game_launched':False,'driver_caches_preserved':True,'scope':'Fresh-process attribution, not vendor-cache-cold or gameplay FPS. Concurrent game invalidates uncontended performance claims; real EVENT and pixel assertions remain valid.'}
(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8',newline='\n')
