#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client
"""Replay native transparent draws through production HDR sky ownership, offline."""
import argparse
import os
import struct
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

parser = argparse.ArgumentParser(__doc__)
parser.add_argument('--renderer', type=Path, default=fp.src('renderer.cpp'))
parser.add_argument('--output', type=Path, default=fp.output_dir()/'smoke-sky')
parser.add_argument('--benchmark', action='store_true')
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
renderer = args.renderer.read_text()
start = renderer.index('    bool hdrWorldAlphaDraw(')
end = renderer.index('    template<class Draw> HRESULT projectedDraw(', start)
methods = renderer[start:end]
start = renderer.index('    template<class Draw> void hdrReplaySky(')
end = renderer.index('    template<class Capture,class Draw> HRESULT drawHook(', start)
(out/'smoke_sky_methods.inc').write_text(methods+renderer[start:end])
assert renderer.count('effectCoverageDraw([&]{return ext->Draw') == 4
assert 'owner.effectCoverageDraw([&]{return args.submit(device);},true)' in renderer
exe = out/'smoke-sky.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                '-Wno-return-type', '-Wno-microsoft-exception-spec', *fp.include_flags(), '-I'+str(out), str(fp.TESTS/'smoke_sky_gpu.cpp'),
                '-luser32', '-o', str(exe)], env=fp.zig_env(), check=True)
pe = bytearray(exe.read_bytes()); flags = struct.unpack_from('<I', pe, 0x3c)[0]+22
struct.pack_into('<H', pe, flags, struct.unpack_from('<H', pe, flags)[0] | 0x20); exe.write_bytes(pe)
backends = [Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll',
            fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll',
            fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll']
for index, backend in enumerate(backends):
    for samples in ((0,) if index == 0 else (0, 2, 8)):
        for draws in ((40, 220) if args.benchmark and index and samples == 2 else (0,)):
            command = [str(exe), str(backend), str(samples)]
            if draws:
                command.append(str(draws))
            run = subprocess.run(command, cwd=out, text=True, capture_output=True, timeout=90)
            (out/f'backend-{index}-msaa-{samples}-draws-{draws}.log').write_text(run.stdout+run.stderr)
            print(backend, run.stdout+run.stderr, end='', flush=True)
            run.check_returncode()
            assert 'SMOKE_SKY_GPU_PASS' in run.stdout
