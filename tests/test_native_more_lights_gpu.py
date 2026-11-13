#!/usr/bin/env python3
# northlight-test: requires=client,stormlib,zig,windows-gpu manual
"""Real-client shader expansion, image parity and scoped draw-state restoration."""
from pathlib import Path
import json
import os
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
import client_archives
from mpq import Archive
from extract_m2_shader_families import collect
from extract_signatures import variants

out = fp.output_dir()
out.mkdir(parents=True, exist_ok=True)
records = {(False, code): False for family, code in collect(fp.client_root())[0].values() if family == 'Model'}
for path in client_archives.chain(fp.client_root(), 'all'):
    with Archive(path) as archive:
        for model in ('vs_2_0', 'vs_3_0'):
            for shader in ('mapobjdiffuse_t1', 'mapobjspecular_t1', 'terrain'):
                name = f'shaders\\vertex\\{model}\\{shader}.bls'
                if not archive.has(name):
                    continue
                for _, _, code in variants(archive.read(name)):
                    key = (shader == 'terrain', code)
                    records[key] = records.get(key, False) or shader != 'mapobjspecular_t1'
source = out / 'client-variants.bin'
with source.open('wb') as stream:
    stream.write(struct.pack('<I', len(records)))
    for (terrain, code), numeric in records.items():
        stream.write(struct.pack('<III', terrain, numeric, len(code) // 4))
        stream.write(code)
exe = out / 'native-more-lights.exe'
cmd = [fp.zig(), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
       *fp.test_include_flags(), fp.TESTS / 'native_more_lights_gpu.cpp', '-luser32', '-o', exe]
with (out / 'compile.log').open('w') as log:
    subprocess.run(list(map(str, cmd)), env=fp.zig_env(), stdout=log, stderr=subprocess.STDOUT, check=True, timeout=180)
results = []
for backend in (Path(os.environ['WINDIR']) / 'SysWOW64/d3d9.dll', fp.client_root() / 'renderer-backends/dxvk/dxvk_d3d9.dll'):
    run = out / ('dxvk' if backend.name == 'dxvk_d3d9.dll' else 'native')
    run.mkdir(exist_ok=True)
    result = subprocess.run(list(map(str, [exe, source, backend])), cwd=run,
                            env={**fp.zig_env(), 'DXVK_LOG_PATH': str(run), 'DISABLE_VULKAN_OBS_CAPTURE': '1'},
                            capture_output=True, text=True, timeout=180)
    (run / 'gpu.log').write_text(result.stdout + result.stderr)
    print(str(backend), result.stdout + result.stderr, flush=True)
    results.append({'backend': str(backend), 'returncode': result.returncode, 'log': str(run / 'gpu.log')})
    (out / 'results.json').write_text(json.dumps(results, indent=2))
    result.check_returncode()
