# northlight-test: requires=windows-gpu,zig,client timing
"""Compare every GI output with and without skipping zero-support visibility.

Synthetic resident/missing/occluded/fading probes, not a gameplay FPS test.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import statistics
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT/'tests')]
import northlight_paths as fp
from scripts.shaders.shader_sources import source_files
from validate_shader_optimizations_gpu import image
from windows_gpu_guard import processes


def fixtures(out, benchmark, fp32=False):
    jobs = []
    modes = ['open', 'closed', 'mixed', 'fresh', 'history', 'missing', 'water', 'sky', 'uncertain', 'legacy']
    for entry in (('WorldGI',) if benchmark else ('WorldGI', 'WorldLightingUnshadowedGI')):
        for mode in (modes[:3] if benchmark else modes):
            if entry != 'WorldGI' and mode == 'legacy':
                continue
            w, h = (1920, 1080) if benchmark else (65, 43)
            folder = out/f'{entry}-{mode}'
            folder.mkdir(parents=True, exist_ok=True)
            n, columns = 8, 32
            c = [[0.] * 4 for _ in range(128)]
            c[0] = [.5/w, .5/h, .2, 1000]
            c[1] = [1, 1, 1, 0]
            c[2] = [1, 12, 100, 1/1024]
            for i in range(4):
                c[3+i][i] = 1
            c[16] = [0, 0, -1, 0]
            c[17] = [.7, .5, .3, 1]
            c[18] = [.2, .3, .4, .1]
            c[19][3] = 8
            c[20] = [n, .7, .8, 1]
            c[24][3] = 5
            c[27] = [1, 1, 0, 0]
            c[28] = [0, 0, -1, 0]
            c[29] = [.7, .5, .3, 0]
            c[30][0] = float(mode == 'water')
            c[33] = [w*2, h*2, w, h]
            c[66] = [1, .35, 0, 0]
            c[73][2] = 1
            c[82] = [0 if mode == 'legacy' else columns, 1/(columns*9), 1/(n**3//columns*9), 1/columns]
            textures = {s: (1, 1, lambda x, y: (0, 0, 0, 0)) for s in range(16)}
            textures[1] = (64, 48, lambda x, y: (1 if mode == 'sky' else 1000/999.8*(1-.2/(8+8*x/63)), 0, 0, 0))
            # Keep the historical optimization oracle on an unchanged fallback:
            # low-confidence upward normals still test bounce suppression.
            # Non-upward seam behavior is checked by test_gi_residency_gpu.py.
            textures[14] = (64, 48, lambda x, y: (0, 0, 1 if mode == 'uncertain' else -1, float(mode != 'uncertain')))
            textures[11] = (1, 1, lambda x, y: (1, 1, 0, 0))
            textures[10] = (n*n, n, lambda x, y: (x%n if x%n<n//2 else x%n-n, y if y<n//2 else y-n, x//n if x//n<n//2 else x//n-n, -1 if mode == 'missing' else 5 if mode == 'fresh' else 4.8 if mode == 'history' else 1))
            textures[7] = (n*n, n*6, lambda x, y: (100, 10000, 0 if mode == 'closed' else 1, 4.85 if mode == 'history' else -1000))
            for s in (4, 5, 6):
                textures[s] = (n*n, n, lambda x, y, s=s: (.13*s+.3*(x%3), .07*(x%5-2), .04*(y%3-1), -.05))
            textures[8] = (n*n*3, n, lambda x, y: (.08*(x%7+1), -.1, .02, .03))
            def visibility(x, y):
                index = x//9 + columns*(y//9)
                if mode == 'closed' or (mode == 'mixed' and index%3 == 0):
                    return (0, 0, 0, -1)
                if mode == 'mixed' and index%3 == 1:
                    return (.12, -.08, .2, 0)
                return (0, 0, 0, 1)
            textures[13] = (columns*9, n**3//columns*9, visibility)
            for slot, (tw, th, pixel) in textures.items():
                image(folder/f'{slot}.fgr', tw, th, pixel)
            targets = 2 if entry == 'WorldGI' else 3
            header = [w, h, (0x100 if fp32 else 0x300) | targets, 0, 0xFFFFFFFF, 15, 0, 0]
            (folder/'params.bin').write_bytes(struct.pack('<8I', *header) + struct.pack('<512f', *(v for row in c for v in row)) + bytes(32))
            jobs.append(f'{entry} {folder.as_posix()} 0')
    path = out/'cases.txt'
    path.write_text('\n'.join(jobs)+'\n')
    return path, len(jobs)


def main():
    p = argparse.ArgumentParser(__doc__)
    p.add_argument('--output', type=Path, default=fp.output_dir()/'gi-zero-weight-gpu')
    p.add_argument('--candidate', type=Path, default=fp.COMPILED)
    p.add_argument('--reference', help='Use bytecode from this git revision as the reference.')
    p.add_argument('--backend', type=Path, action='append')
    p.add_argument('--benchmark', action='store_true')
    p.add_argument('--control', action='store_true')
    p.add_argument('--fp32', action='store_true', help='Additional precision oracle; timings use production FP16 targets.')
    a = p.parse_args()
    assert not (a.fp32 and a.benchmark)
    processes()
    out = a.output.resolve(); out.mkdir(parents=True, exist_ok=True)
    reference = out/'reference'; reference.mkdir(exist_ok=True)
    entries = ('WorldGI', 'WorldLightingUnshadowedGI')
    shader_inputs = ()
    if a.reference:
        for entry in entries:
            (reference/f'{entry}.bin').write_bytes(subprocess.check_output(['git', 'show', f'{a.reference}:shaders/compiled/{entry}.bin'], cwd=ROOT))
    else:
        sys.path.insert(0, str(fp.SCRIPTS/'shaders'))
        from shader_compiler import Compiler
        world = fp.SHADERS/'world_effects.hlsl'
        shader_inputs = source_files(world)
        source = reference/'reference.hlsl'
        source.write_text(f'#define GI_FORCE_VISIBILITY_READS\n#include "{world.as_posix()}"\n')
        compiler = Compiler(allow_compatible_binary=True)
        for entry in entries:
            compiler.compile(source, entry, 'ps_3_0', reference/f'{entry}.bin')
    backends = a.backend or [Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll', fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll', fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll']
    candidate = reference if a.control else a.candidate.resolve()
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    inputs = {str(path): sha(path) for path in [Path(__file__), ROOT/'tests/shader_optimizations_gpu.cpp', ROOT/'tests/composite_distance_gpu.cpp', *backends, *shader_inputs, *[candidate/f'{e}.bin' for e in entries], *[reference/f'{e}.bin' for e in entries]]}
    cases, count = fixtures(out, a.benchmark, a.fp32)
    exe = out/'gi-oracle.exe'
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG', '-std=c++17', *fp.test_include_flags(), str(ROOT/'tests/shader_optimizations_gpu.cpp'), '-luser32', '-o', str(exe)], env=fp.zig_env(), check=True)
    report = dict(pass_=False, scope=__doc__, reference=a.reference or 'all probe visibility reads', inputs=inputs, control=a.control, benchmark=a.benchmark, output_bits=32 if a.fp32 else 16, runs=[])
    try:
        for i, backend in enumerate(backends):
            processes()
            env = {**os.environ, 'DISABLE_VULKAN_OBS_CAPTURE': '1', 'DXVK_LOG_PATH': str(out)}
            if a.benchmark: env['NORTHLIGHT_SHADER_LONG_TIMING'] = '1'
            result = subprocess.run([str(exe), str(cases), str(reference), str(candidate), str(backend.resolve())], cwd=out, env=env, text=True, capture_output=True, timeout=900)
            (out/f'backend-{i}.log').write_text(result.stdout+result.stderr)
            rows = [json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
            report['runs'].append(dict(backend=str(backend), exit_code=result.returncode, results=rows))
            if a.benchmark:
                blocks = {}
                for case, block, variant, ms in re.findall(r'TIMING case=(\S+) block=(\d+) variant=(\d+) ms=([\d.]+)', result.stdout):
                    blocks.setdefault(Path(case).name, {}).setdefault(int(block)//4, {}).setdefault(int(variant), []).append(float(ms))
                report['runs'][-1]['paired_gain_percent'] = {
                    case: statistics.median(100*(1-statistics.mean(v[1])/statistics.mean(v[0])) for v in series.values())
                    for case, series in blocks.items()}
            result.check_returncode()
            assert len(rows) == count and all(r['changed'] == r['nonfinite'] == 0 for r in rows), rows
            for row in rows:
                print(Path(row['case']).name, f"{row['before_ms']:.6f} -> {row['after_ms']:.6f} ms", 'exact', flush=True)
        report['pass_'] = all(sha(Path(path)) == digest for path, digest in inputs.items())
    finally:
        (out/'validation.json').write_text(json.dumps(report, indent=2)+'\n')
    assert report['pass_']


if __name__ == '__main__':
    main()
