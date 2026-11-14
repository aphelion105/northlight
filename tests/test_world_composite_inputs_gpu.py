# northlight-test: requires=windows-gpu,zig,client
"""Frozen shipped/current WorldComposite on the same HDR graph; optional ABBA timings."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
from windows_gpu_guard import processes

p = argparse.ArgumentParser(__doc__)
p.add_argument('--output', type=Path)
p.add_argument('--benchmark', action='store_true')
p.add_argument('--control', action='store_true')
p.add_argument('--capture-prefix')
a = p.parse_args()
out = (a.output or fp.output_dir() / 'world-composite-inputs').resolve()
out.mkdir(parents=True, exist_ok=True)
baseline = out / 'reference.bin'
baseline.write_bytes(subprocess.check_output(['git', 'show',
    '6e981fe2362a4584a64e93d5b2040e00b6849e24:shaders/compiled/WorldMaterialCompositeHdr.bin'], cwd=fp.REPO))
candidate = fp.COMPILED / 'WorldMaterialCompositeHdr.bin'
backend = fp.client_root() / 'renderer-backends/dxvk/dxvk_d3d9.dll'
capture = str(Path(a.capture_prefix).resolve()) if a.capture_prefix else '-'
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
inputs = {str(path): sha(path) for path in [Path(__file__), baseline, candidate, backend,
    fp.TESTS / 'world_composite_inputs_gpu.cpp', fp.TESTS / 'hdr_pipeline_gpu.cpp',
    *(p for p in (fp.REPO / 'src').rglob('*') if p.suffix in ('.h', '.inl')),
    *(Path(capture + suffix) for suffix in ('constants.f32', 'scene.fgr', 'depth.fgr',
       'material-height.fgr', 'normals.fgr', 'distance.fgr') if a.capture_prefix)]}
report = {'pass': False, 'inputs': inputs, 'control': a.control,
          'scope': 'Authored HDR graph and optional saved material inputs; not complete gameplay replay.',
          'timings_measured': a.benchmark, 'events': []}


def run(label, command):
    processes()
    env = {**fp.zig_env(), 'DISABLE_VULKAN_OBS_CAPTURE': '1', 'DXVK_LOG_PATH': str(out)}
    env.pop('NORTHLIGHT_BENCHMARK', None)
    if a.benchmark:
        env['NORTHLIGHT_BENCHMARK'] = '1'
    result = subprocess.run(list(map(str, command)), cwd=fp.REPO, env=env,
                            capture_output=True, text=True, timeout=600)
    (out / (label + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
    assert result.returncode == 0, (label, (result.stdout + result.stderr)[-2500:])
    return result.stdout


try:
    exe = out / 'composite.exe'
    run('compile', [fp.zig(), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-fno-rtti',
                   '-UNDEBUG', '-Wno-microsoft-exception-spec', *fp.include_flags(),
                   fp.TESTS / 'world_composite_inputs_gpu.cpp', '-luser32', '-o', exe])
    data = bytearray(exe.read_bytes())
    offset = struct.unpack_from('<I', data, 0x3c)[0] + 22
    struct.pack_into('<H', data, offset, struct.unpack_from('<H', data, offset)[0] | 0x20)
    exe.write_bytes(data)
    text = run('gpu', [exe, backend, baseline, candidate, capture, 'control' if a.control else 'compare'])
    report['events'] = [json.loads(line) for line in text.splitlines() if line.startswith('{')]
    images = [row for row in report['events'] if row['event'] == 'image']
    assert len(images) == (6 if a.capture_prefix else 5) and 'WORLD_COMPOSITE_INPUTS_PASS' in text
    assert all(sha(Path(path)) == digest for path, digest in inputs.items()), 'source drift'
    report['pass'] = True
    print('WORLD_COMPOSITE_INPUTS_PASS', images)
finally:
    (out / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
