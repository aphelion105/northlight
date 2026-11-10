# northlight-test: requires=windows-gpu,zig manual timing
"""Compare actual model capture CPU work; point-shadow rasterization checks output only.

The fixture captures 512 repeated rigid draws, including source acquisition,
packet recycling and WorldRenderer::capture. It excludes material processing,
skinning, other effects, GPU timings, presentation and game FPS.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import os
import re
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import northlight_paths as current
sys.path.insert(0, str(ROOT / 'scripts'))
from build_renderer import source_identity


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('upstream', 'baseline', 'backend', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--verify-backend', type=Path, action='append', default=[])
    parser.add_argument('--rounds', type=int, default=3)
    args = parser.parse_args()
    assert 1 <= args.rounds <= 10
    out = args.output.resolve();out.mkdir(parents=True, exist_ok=False)
    repos = {'upstream': args.upstream.resolve(), 'before': args.baseline.resolve(), 'pr': ROOT}
    git = lambda repo, *a: subprocess.check_output(['git', '-C', str(repo), *a], text=True).strip()
    for label in ('upstream', 'before'):
        assert not git(repos[label], 'diff', 'HEAD', '--', 'src', 'shaders'), label + ' sources changed'
    digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    identities = {label: source_identity(repo) for label, repo in repos.items()}
    backends = [args.backend.resolve(), *(p.resolve() for p in args.verify_backend)]
    inputs = [Path(__file__).resolve(), ROOT / 'tests/world_capture_handoff_gpu.cpp', *backends]
    hashes = {str(p): digest(p) for p in inputs}
    env = {**current.zig_env(), 'NORTHLIGHT_ZIG': str(current.zig()), 'DISABLE_VULKAN_OBS_CAPTURE': '1'}
    builds = {}
    for label, repo in repos.items():
        spec = importlib.util.spec_from_file_location('_capture_paths', repo / 'northlight_paths.py')
        fp = importlib.util.module_from_spec(spec);spec.loader.exec_module(fp)
        folder = out / ('build-' + label);folder.mkdir()
        headers = folder / 'headers';headers.mkdir()
        header = fp.src('world_renderer.h').read_text(encoding='utf-8')
        if 'friend struct PointTerrainCoverageTest;' not in header:
            assert header.count('class WorldRenderer {') == 1
            header = header.replace('class WorldRenderer {', 'class WorldRenderer {\nfriend struct PointTerrainCoverageTest;', 1)
        (headers / 'world_renderer.h').write_text(header, encoding='utf-8')
        snapshot = fp.src('draw_snapshot.h').read_text(encoding='utf-8')
        shared, handoff = 'shareSnapshotPool(' in snapshot, 'bool readShared(' in snapshot
        assert handoff == (label == 'pr')
        exe = folder / 'capture.exe'
        units = ['world_gi.cpp', 'world_mesh_plan.cpp', 'static_shadow_scene.cpp', 'settings_overlay.cpp', 'settings_panel.cpp',
                 'imgui.cpp', 'imgui_draw.cpp', 'imgui_tables.cpp', 'imgui_widgets.cpp', 'imgui_impl_dx9.cpp']
        command = [current.zig(), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG', '-std=c++17',
                   '-Wno-microsoft-exception-spec', '-DIMGUI_USER_CONFIG="northlight_imconfig.h"',
                   f'-DNORTHLIGHT_SHARED_SOURCE={int(shared)}', f'-DNORTHLIGHT_CAPTURE_HANDOFF={int(handoff)}',
                   '-I' + str(headers), *fp.test_include_flags(), ROOT / 'tests/world_capture_handoff_gpu.cpp',
                   *(fp.src(n) for n in units), '-luser32', '-luuid', '-o', exe]
        with (folder / 'compile.log').open('w') as log:
            subprocess.run(list(map(str, command)), cwd=repo, env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=360)
        builds[label] = {'exe': str(exe), 'sha256': digest(exe), 'shared_source': shared, 'handoff': handoff}
        print('CAPTURE_BUILD_PASS', label, flush=True)

    def run(folder, label, mode, backend):
        folder.mkdir()
        result = subprocess.run([builds[label]['exe'], str(backend), str(folder), str(mode)], cwd=folder,
                                env={**env, 'DXVK_LOG_PATH': str(folder)}, capture_output=True, text=True, timeout=120)
        (folder / 'run.log').write_text(result.stdout + result.stderr)
        result.check_returncode()
        assert 'WORLD_CAPTURE_HANDOFF_GPU_PASS' in result.stdout
        if label == 'pr':assert 'CAPTURE_HANDOFF_CONTROLS_PASS' in result.stdout
        frames = [dict(re.findall(r'(\w+)=([^ ]+)', line)) for line in result.stdout.splitlines()
                  if line.startswith('CAPTURE_FRAME ') and 'warm=0 ' in line]
        assert len(frames) == 32 and all(int(f['packets']) == 512 for f in frames)
        return {'label': label, 'mode': mode, 'output': str(folder),
                **{key: statistics.median(float(f[key]) for f in frames)
                   for key in ('cpuMs', 'packets', 'chargedBytes', 'handoffs', 'geometryCalls')},
                **{name + '_sha256': digest(folder / (name + '.bin')) for name in ('packets', 'faces')}}

    records = []
    phases = [('upstream', (('upstream', 0), ('pr', 1), ('pr', 1), ('upstream', 0))),
              ('baseline', (('before', 0), ('pr', 1), ('pr', 1), ('before', 0))),
              ('control', (('pr', 0), ('pr', 1), ('pr', 1), ('pr', 0)))]
    for phase, order in phases:
        for repeat in range(args.rounds):
            for position, (label, mode) in enumerate(order):
                record = {'phase': phase, 'round': repeat, 'order': position,
                          **run(out / f'{phase}-{repeat}-{position}', label, mode, backends[0])}
                records.append(record);print(json.dumps(record), flush=True)
    summary = []
    for phase, order in phases:
        row = {'phase': phase}
        for label, mode in dict.fromkeys(order):
            values = [r for r in records if r['phase'] == phase and r['label'] == label and r['mode'] == mode]
            assert len(values) == 2 * args.rounds
            row[f'{label}-{mode}'] = {key: statistics.median(r[key] for r in values)
                                     for key in ('cpuMs', 'geometryCalls', 'chargedBytes', 'handoffs')}
        # Existing upstream shadow behavior may differ. The new optimization must be exact.
        if phase != 'upstream':
            for name in ('packets', 'faces'):
                assert len({r[name + '_sha256'] for r in records if r['phase'] == phase}) == 1, (phase, name)
        summary.append(row)
    verification = []
    for index, backend in enumerate(backends[1:]):
        pair = [run(out / f'verify-{index}-{mode}', 'pr', mode, backend) for mode in (0, 1)]
        for name in ('packets', 'faces'):assert pair[0][name + '_sha256'] == pair[1][name + '_sha256']
        verification.append({'backend': str(backend), 'exact': True, 'records': pair})
    assert all(source_identity(repos[label]) == identity for label, identity in identities.items())
    assert all(digest(Path(path)) == sha for path, sha in hashes.items())
    report = {'pass': True, 'scope': __doc__, 'total_northlight_cost_measured': False, 'gpu_timing_measured': False,
              'source_identities': identities, 'commits': {label: git(repo, 'rev-parse', 'HEAD') for label, repo in repos.items()},
              'inputs': hashes, 'builds': builds, 'inputs_unchanged': True, 'order': 'ABBA', 'rounds': args.rounds,
              'records': records, 'summary': summary, 'verification': verification}
    (out / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(summary, indent=2), flush=True)


if __name__ == '__main__':main()
