#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client
"""Exact native/HDR pixel proof and isolated four-layer fixed sky timings."""
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
parser.add_argument('--output', type=Path, default=fp.output_dir()/'hdr-fixed-sky')
args = parser.parse_args()
processes()
renderer = fp.src('renderer.cpp').read_text(encoding='utf-8')
assert renderer.count('bool nativeSky=false,skyCaptured=false;') == 4
assert renderer.count('},false,nativeSky,&skyCaptured);') == 4
assert renderer.count('},draw,&nativeSky,&skyCaptured);}') == 4
assert renderer.count('claimed||mist||(skyCaptured&&*skyCaptured)') == 2
assert 'if(nativeSky)*nativeSky=linearSky;' in renderer
assert 'sky&&skyCaptured&&hdr.logical.MultiSampleType==D3DMULTISAMPLE_NONE' in renderer
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
binary = out/'hdr-fixed-sky.exe'
run = subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG',
    '-std=c++17', '-Wno-return-type', '-Wno-microsoft-exception-spec', *fp.test_include_flags(),
    str(fp.TESTS/'hdr_fixed_sky_gpu.cpp'), '-luser32', '-o', str(binary)], env=fp.zig_env(), text=True, capture_output=True)
(out/'compile.log').write_text(run.stdout+run.stderr, encoding='utf-8')
run.check_returncode()
pe = bytearray(binary.read_bytes()); at = struct.unpack_from('<I', pe, 0x3c)[0]+22
struct.pack_into('<H', pe, at, struct.unpack_from('<H', pe, at)[0] | 0x20); binary.write_bytes(pe)
results = []
for name, backend in [('native', Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll'),
                      ('dxvk2', fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll'),
                      ('dxvk3', fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll')]:
    folder = out/name; folder.mkdir(exist_ok=True)
    config = folder/'dxvk.conf'; config.write_text('dxvk.enableStateCache = False\nd3d9.presentInterval = 0\n')
    run = subprocess.run([str(binary), str(backend)], cwd=folder, text=True, capture_output=True, timeout=180,
                         env=dict(os.environ, DXVK_LOG_PATH=str(folder), DXVK_CONFIG_FILE=str(config), DISABLE_VULKAN_OBS_CAPTURE='1'))
    (folder/'run.log').write_text(run.stdout+run.stderr, encoding='utf-8'); print(name, run.stdout+run.stderr, flush=True)
    run.check_returncode(); assert 'HDR_FIXED_SKY_GPU_PASS' in run.stdout
    rows = [tuple(map(float, r)) for r in re.findall(r'HDR_FIXED_SKY_TIMING block=\d+ mrt=(\d+) gpuMs=([\d.]+) submitMs=([\d.]+)', run.stdout)]
    results.append(dict(backend=name, sha256=hashlib.sha256(backend.read_bytes()).hexdigest(),
                        **{label: dict(gpu_ms=statistics.median(r[1] for r in rows if r[0]==mode),
                                      submit_ms=statistics.median(r[2] for r in rows if r[0]==mode))
                           for mode,label in [(0,'replay'),(1,'mrt')]}))
(out/'validation.json').write_text(json.dumps(dict(pass_=True, backends=results), indent=2)+'\n')
