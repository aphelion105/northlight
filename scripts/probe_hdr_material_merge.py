"""Measure the currently disabled expanded-HDR material fusion, without changing the client.

This is a manual research probe, not production acceptance or game FPS evidence.
Scene, depth, camera, normals and material height may come from a capture;
lighting/fog inputs and native draw ordering remain authored.
"""
import argparse
import hashlib
import json
from pathlib import Path
import statistics
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tests'), str(ROOT / 'scripts')]
import northlight_paths as fp
from build_renderer import source_identity
from windows_gpu_guard import processes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--backend', type=Path, required=True)
    parser.add_argument('--capture-prefix', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    out = (args.output or fp.output_dir() / 'hdr-material-merge-probe').resolve()
    if fp.is_client(out):
        parser.error('Use an output directory outside the client')
    out.mkdir(parents=True, exist_ok=False)
    census = processes()
    backend = args.backend.resolve()
    digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    report = dict(scope=__doc__, source=source_identity(ROOT), backend=str(backend),
                  backend_sha256=digest(backend), processes=census, status='running')
    inputs = [Path(__file__).resolve(), fp.TESTS / 'hdr_material_merge_probe.cpp', fp.TESTS / 'hdr_pipeline_gpu.cpp']
    if args.capture_prefix:
        inputs += [Path(str(args.capture_prefix.resolve()) + name) for name in ('scene.fgr', 'depth.fgr', 'constants.f32', 'material-height.fgr', 'normals.fgr', 'distance.fgr')]
    report['inputs'] = {str(p): digest(p) for p in inputs}
    report_path = out / 'report.json'

    def save():
        report_path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')

    def run(name, command, timeout):
        env = {**fp.zig_env(), 'DISABLE_VULKAN_OBS_CAPTURE': '1', 'DXVK_LOG_PATH': str(out)}
        result = subprocess.run(list(map(str, command)), cwd=out, env=env, capture_output=True, text=True, errors='replace', timeout=timeout)
        (out / (name + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        if result.returncode:
            raise RuntimeError(f'{name} failed ({result.returncode}); see {out / (name + ".log")}')
        return result.stdout

    save()
    try:
        exe = out / 'probe.exe'
        run('compile', [fp.zig(), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-fno-rtti', '-Wno-microsoft-exception-spec', *fp.include_flags(), fp.TESTS / 'hdr_material_merge_probe.cpp', '-luser32', '-o', exe], 300)
        pe = bytearray(exe.read_bytes()); flags = struct.unpack_from('<I', pe, 0x3c)[0] + 22
        struct.pack_into('<H', pe, flags, struct.unpack_from('<H', pe, flags)[0] | 0x20); exe.write_bytes(pe)
        report['executable_sha256'] = digest(exe)
        output = run('probe', [exe, backend, *([args.capture_prefix.resolve()] if args.capture_prefix else [])], 300)
        assert 'HDR_MATERIAL_MERGE_PROBE_PASS' in output
        events = [json.loads(line) for line in output.splitlines() if line.startswith('{')]
        report['cases'] = []
        for image in (e for e in events if e['event'] == 'image'):
            timings = [e for e in events if e['event'] == 'timing' and e['case'] == image['case'] and e['block'] >= 2]
            assert len(timings) == 32
            case = dict(image)
            for metric in ('gpu_ms', 'cpu_ms'):
                case[metric] = {name: statistics.median(e[metric] for e in timings if e['merged'] == merged) for name, merged in [('separate', False), ('merged', True)]}
            case['paired_gpu_ratios'] = [statistics.mean(e['gpu_ms'] for e in timings if e['block'] == block and e['merged']) / statistics.mean(e['gpu_ms'] for e in timings if e['block'] == block and not e['merged']) for block in range(2, 10)]
            report['cases'].append(case)
            print(json.dumps(case), flush=True)
        report['status'] = 'probe_passed_not_production_acceptance'
    except Exception as error:
        report['status'] = 'failed'; report['error'] = str(error)
        raise
    finally:
        save()


if __name__ == '__main__':
    main()
