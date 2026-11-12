#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client
"""Check identical HDR sky storage with one fewer MSAA0 FP16 copy."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import statistics
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
from windows_gpu_guard import processes

parser = argparse.ArgumentParser(__doc__)
parser.add_argument('--output', type=Path, default=fp.output_dir()/'hdr-sky-storage')
args = parser.parse_args()
processes()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
renderer = fp.src('renderer.cpp').read_text(encoding='utf-8')
start = renderer.index('    IDirect3DSurface9* hdrSkyTarget()const{')
end = renderer.index('\n    }', start)+6
(out/'hdr_sky_storage_methods.inc').write_text(renderer[start:end]+'\n')
assert 'hdrSkyForeground.draw(ext,hdrSkyTarget(),' in renderer
assert 'hdrSkyValid?hdrSkyTarget():nullptr' in renderer
assert 'hdrFrame.beginSkyResidual()' in renderer
assert 'ext->SetRenderTarget(0,hdrSkyTarget())' in renderer
assert 'return hdrSkyDraw()?hdrSkyTarget():nullptr;' in renderer
env = fp.zig_env()
env.update(DISABLE_VULKAN_OBS_CAPTURE='1')
binary = out/'hdr-sky-storage.exe'
command = [str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG',
           '-std=c++17', '-Wno-return-type', '-Wno-microsoft-exception-spec',
           *fp.test_include_flags(), '-I'+str(out), str(fp.TESTS/'hdr_sky_storage_gpu.cpp'),
           '-luser32', '-o', str(binary)]
compiled = subprocess.run(command, env=env, text=True, capture_output=True)
(out/'compile.log').write_text(compiled.stdout+compiled.stderr)
compiled.check_returncode()
pe = bytearray(binary.read_bytes())
at = struct.unpack_from('<I', pe, 0x3c)[0]+22
struct.pack_into('<H', pe, at, struct.unpack_from('<H', pe, at)[0] | 0x20)
binary.write_bytes(pe)
results = []
for name, backend in [
    ('native', Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll'),
    ('dxvk2', fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll'),
    ('dxvk3', fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll'),
]:
    run_dir = out/name
    run_dir.mkdir(exist_ok=True)
    (run_dir/'dxvk.conf').write_text('dxvk.enableStateCache = False\nd3d9.presentInterval = 0\n')
    run_env = dict(env, DXVK_LOG_PATH=str(run_dir), DXVK_CONFIG_FILE=str(run_dir/'dxvk.conf'))
    run = subprocess.run([str(binary), str(backend)], cwd=run_dir, env=run_env,
                         text=True, capture_output=True, timeout=180)
    (run_dir/'run.log').write_text(run.stdout+run.stderr)
    print(name, run.stdout+run.stderr, flush=True)
    run.check_returncode()
    assert 'HDR_SKY_STORAGE_GPU_PASS' in run.stdout
    rows = [tuple(map(float, row)) for row in re.findall(
        r'HDR_SKY_STORAGE_TIMING block=\d+ alias=(\d+) gpuMs=([\d.]+) submitMs=([\d.]+)', run.stdout)]
    result = dict(backend=name, sha256=hashlib.sha256(backend.read_bytes()).hexdigest(), pass_=True)
    for alias in (0, 1):
        result['alias' if alias else 'copy'] = dict(
            gpu_ms=statistics.median(r[1] for r in rows if r[0] == alias),
            submit_ms=statistics.median(r[2] for r in rows if r[0] == alias))
    results.append(result)
(out/'validation.json').write_text(json.dumps(dict(pass_=True, backends=results,
    source_sha256={str(p.relative_to(fp.REPO)): hashlib.sha256(p.read_bytes()).hexdigest()
                   for p in [fp.src('renderer.cpp'), fp.TESTS/'hdr_pipeline_gpu.cpp',
                             fp.TESTS/'hdr_sky_storage_gpu.cpp', Path(__file__)]}), indent=2)+'\n')
