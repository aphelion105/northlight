#!/usr/bin/env python3
"""Build/run the x86 D3D11 GPU BVH feasibility experiment, without touching the client."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT/'scripts/shaders')]
import northlight_paths as fp
from shader_compiler import Compiler

ENTRIES = ('SceneBounds', 'ReduceBounds', 'Morton', 'SortLocal', 'SortGlobal',
           'MergeLocal', 'LeafBounds', 'InnerBounds', 'TraceRays', 'SolveProbes', 'SolveParallel',
           'RadixTopology', 'ClearVisits', 'RadixBounds', 'MaterialOpacity')


def validate_cs5(path):
    data = path.read_bytes()
    assert data[:4] == b'DXBC' and len(data) >= 32, path
    size, count = struct.unpack_from('<II', data, 24)
    assert size == len(data) and 0 < count <= (len(data)-32)//4, path
    found = False
    for offset in struct.unpack_from(f'<{count}I', data, 32):
        assert offset+8 <= len(data), path
        tag, length = struct.unpack_from('<4sI', data, offset)
        assert offset+8+length <= len(data), path
        if tag in (b'SHDR', b'SHEX'):
            assert length >= 8 and struct.unpack_from('<I', data, offset+8)[0] == 0x50050, path
            found = True
    assert found, path


def build(out):
    compiler = Compiler(allow_compatible_binary=True)  # Explicit fixture-only distribution.
    for entry in ENTRIES:
        destination = out/(entry+'.bin')
        # Reuse the verified Microsoft helper; its Python SM3 marker checker
        # intentionally cannot validate a DXBC compute container.
        compiled = subprocess.run([*compiler.command, str(ROOT/'scripts/gpu_bvh_prototype.hlsl'),
                        entry, 'cs_5_0', str(destination)], env=compiler.env,
                       capture_output=True, timeout=120)
        (out/(entry+'-compile.log')).write_bytes(compiled.stdout+compiled.stderr)
        if compiled.returncode:
            print((compiled.stdout+compiled.stderr).decode('utf-8', errors='replace'))
        compiled.check_returncode()
        validate_cs5(destination)
    exe = out/'gpu-bvh-prototype.exe'
    command = [str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2',
               '-UNDEBUG', *fp.include_flags(), str(ROOT/'scripts/gpu_bvh_prototype.cpp'),
               str(fp.src('world_gi.cpp')), '-ld3d11', '-ldxgi', '-luuid', '-lbcrypt', '-o', str(exe)]
    result = subprocess.run(command, env=fp.zig_env(), capture_output=True, text=True)
    (out/'compile.log').write_text(result.stdout+result.stderr, encoding='utf-8')
    if result.returncode:
        print(result.stdout+result.stderr)
    result.check_returncode()
    return exe, compiler.provenance


def run(out, scenes, rounds, measure_rejected=False):
    out.mkdir(parents=True, exist_ok=False)
    inputs = [ROOT/'scripts/prototype_gpu_bvh.py', ROOT/'scripts/gpu_bvh_prototype.cpp',
              ROOT/'scripts/gpu_bvh_prototype.hlsl', ROOT/'northlight_paths.py',
              ROOT/'scripts/build_environment.py', ROOT/'scripts/shaders/shader_compiler.py',
              ROOT/'scripts/shaders/compile_shaders.cpp', fp.src('world_gi.cpp')]
    inputs += sorted((ROOT/'src').rglob('*.h'))
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    before = {str(p.relative_to(ROOT)): sha(p) for p in inputs}
    git = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    (out/'results.json').write_text(json.dumps(dict(pass_=False, status='BUILDING', commit=git)))
    exe, compiler = build(out)
    records = []
    for kind in scenes:
        command = [str(exe), str(out), str(kind), str(rounds), str(fp.client_root())]
        if measure_rejected:
            command.append('--measure-rejected')
        result = subprocess.run(command, capture_output=True, text=True, timeout=600)
        (out/f'scene-{kind}.log').write_text(result.stdout+result.stderr, encoding='utf-8')
        rows = [json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
        records.append(dict(scene=kind, exit_code=result.returncode, log_sha256=sha(out/f'scene-{kind}.log'), rows=rows))
        (out/'results.json').write_text(json.dumps(dict(pass_=False, compiler=compiler,
            input_sha256=before, executable_sha256=sha(exe), records=records), indent=2)+'\n')
        print(f'Scene {kind}: exit={result.returncode}; log={out/f"scene-{kind}.log"}', flush=True)
        if result.returncode:
            print(result.stdout[-700:]+result.stderr, flush=True)
        if not (measure_rejected and result.returncode == 2 and 'GPU_BVH_REJECTED' in result.stdout):
            result.check_returncode()
            assert 'GPU_BVH_PASS' in result.stdout
    assert all(sha(ROOT/p) == digest for p, digest in before.items()), 'Inputs changed during run'
    assert git == subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    report = dict(pass_=all(r['exit_code'] == 0 for r in records), compiler=compiler, commit=git, input_sha256=before,
                  status='PASS' if all(r['exit_code'] == 0 for r in records) else 'REJECTED',
                  executable_sha256=sha(exe), records=records,
                  production_eligible=False,
                  timing_notes=['CPU process time has Windows scheduler tick quantization; CPU cycles are raw, not converted to time.',
                                'GPU wall time includes 1 ms Sleep polling; timestamp excludes polling, includes supplied commands.',
                                'Cold packing, allocation and upload are separate; tree/ray validation readbacks are excluded.',
                                'GI kernels have two warmups per leaf/batch; CPU scene copy is outside SAH build interval.',
                                'No D3D9 probe atlas upload, mixed-render GPU contention, device loss or gameplay test.'],
                  scope='Isolated native D3D11 hardware, x86 O2; no game or D3D9 publication; sun/sky GI only')
    (out/'results.json').write_text(json.dumps(report, indent=2)+'\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=fp.output_dir()/'gpu-bvh')
    parser.add_argument('--scenes', type=int, nargs='+', default=[0, 1, 2, 3])
    parser.add_argument('--rounds', type=int, default=2)
    parser.add_argument('--measure-rejected', action='store_true',
                        help='Collect diagnostics after parity failures; report still fails and exits 2.')
    args = parser.parse_args()
    if not all(0 <= i <= 7 for i in args.scenes) or not 1 <= args.rounds <= 20:
        parser.error('scenes must be 0..7 and rounds 1..20')
    report = run(args.output.resolve(), args.scenes, args.rounds, args.measure_rejected)
    sys.exit(0 if report['pass_'] else 2)
