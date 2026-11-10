# northlight-test: requires=windows-gpu,zig manual timing
"""Compare the common SDR pipeline with a separate, unchanged upstream checkout.

The captured scene/depth is fixed; caster geometry and fog are authored. This
does not measure model/material capture, HDR, weather, presentation or game FPS.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import os
import re
import statistics
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import northlight_paths as current
sys.path.insert(0, str(ROOT / 'scripts'))
from build_renderer import source_identity


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--upstream', type=Path, required=True)
    parser.add_argument('--capture', type=Path, required=True)
    parser.add_argument('--backend', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--rounds', type=int, default=3)
    parser.add_argument('--before-ref', help='Optional frozen point-rendering control from this repository.')
    args = parser.parse_args()
    assert 1 <= args.rounds <= 10
    upstream, capture, backend, out = (p.resolve() for p in (args.upstream, args.capture, args.backend, args.output))
    out.mkdir(parents=True, exist_ok=False)
    git = lambda repo, *a: subprocess.check_output(['git', '-C', str(repo), *a], text=True).strip()
    assert not git(upstream, 'diff', 'HEAD', '--', 'src', 'shaders'), 'Upstream production sources must be unchanged'
    identities = {str(repo): source_identity(repo) for repo in (ROOT, upstream)}
    digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    inputs = [Path(__file__).resolve(), ROOT / 'tests/upstream_pipeline_gpu.cpp', backend,
              *(capture / name for name in ('capture-1-scene.fgr', 'capture-1-depth.fgr', 'capture-1-constants.f32'))]
    hashes = {str(p): digest(p) for p in inputs}
    control = None
    if args.before_ref:
        control = out / 'control';control.mkdir()
        (control / 'world_point_rendering.inl').write_bytes(subprocess.check_output(
            ['git', 'show', args.before_ref + ':src/world/world_point_rendering.inl'], cwd=ROOT))
    env = {**current.zig_env(), 'NORTHLIGHT_ZIG': str(current.zig()), 'DISABLE_VULKAN_OBS_CAPTURE': '1'}
    builds = {}
    for label, repo in [('upstream', upstream), ('pr', ROOT)] + ([('before', ROOT)] if control else []):
        spec = importlib.util.spec_from_file_location('_pipeline_paths', repo / 'northlight_paths.py')
        fp = importlib.util.module_from_spec(spec);spec.loader.exec_module(fp)
        folder = out / ('build-' + label);folder.mkdir();headers = folder / 'headers';headers.mkdir()
        text = fp.src('world_renderer.h').read_text()
        assert text.count('class WorldRenderer {') == 1
        (headers / 'world_renderer.h').write_text(text.replace('class WorldRenderer {', 'class WorldRenderer {\nfriend struct PipelineCompareTest;', 1))
        build_env = env.copy()
        for key, name in [('TEMP', 'tmp'), ('TMP', 'tmp'), ('ZIG_LOCAL_CACHE_DIR', 'zig-local'), ('ZIG_GLOBAL_CACHE_DIR', 'zig-global')]:
            (folder / name).mkdir(exist_ok=True);build_env[key] = str(folder / name)
        units = ['world_gi.cpp', 'world_mesh_plan.cpp', 'static_shadow_scene.cpp', 'settings_overlay.cpp', 'settings_panel.cpp',
                 'imgui.cpp', 'imgui_draw.cpp', 'imgui_tables.cpp', 'imgui_widgets.cpp', 'imgui_impl_dx9.cpp']
        exe = folder / 'pipeline.exe'
        command = [current.zig(), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                   '-Wno-return-type', '-Wno-microsoft-exception-spec', '-DIMGUI_USER_CONFIG="northlight_imconfig.h"',
                   f'-DNORTHLIGHT_COMPARE_PR={int(label != "upstream")}', '-I' + str(headers),
                   *(['-I' + str(control)] if label == 'before' else []), *fp.test_include_flags(),
                   ROOT / 'tests/upstream_pipeline_gpu.cpp', *(fp.src(n) for n in units),
                   '-luser32', '-luuid', '-o', exe]
        with (folder / 'compile.log').open('w') as log:
            subprocess.run(list(map(str, command)), cwd=repo, env=build_env, stdout=log, stderr=subprocess.STDOUT, check=True)
        data = bytearray(exe.read_bytes());at = struct.unpack_from('<I', data, 0x3c)[0] + 22
        struct.pack_into('<H', data, at, struct.unpack_from('<H', data, at)[0] | 0x20);exe.write_bytes(data)
        builds[label] = {'exe': str(exe), 'sha256': digest(exe), 'compiled': str(fp.COMPILED)}
        print('PIPELINE_BUILD_PASS', label, flush=True)
    records = []
    phases = [('upstream', mode, ('upstream', 'pr', 'pr', 'upstream')) for mode in range(3)]
    if control:phases.append(('point', 2, ('before', 'pr', 'pr', 'before')))
    for phase, mode, order in phases:
        for repeat in range(args.rounds):
            for position, label in enumerate(order):
                folder = out / f'{phase}-{mode}-{repeat}-{position}-{label}';folder.mkdir()
                run = subprocess.run([builds[label]['exe'], str(backend), str(folder), str(capture), builds[label]['compiled'], str(mode)],
                                     env={**env, 'DXVK_LOG_PATH': str(folder)}, cwd=folder, capture_output=True, text=True, timeout=120)
                (folder / 'run.log').write_text(run.stdout + run.stderr);run.check_returncode()
                assert 'COMMON_PIPELINE_PASS' in run.stdout
                frames = [dict(re.findall(r'(\w+)=([^ ]+)', line)) for line in run.stdout.splitlines()
                          if line.startswith('COMMON_FRAME ') and 'warm=0 ' in line]
                assert len(frames) == 32
                record = {'phase': phase, 'mode': mode, 'round': repeat, 'order': position, 'label': label,
                          'image': str(folder / 'output.bgra'), 'image_sha256': digest(folder / 'output.bgra'),
                          **{key: statistics.median(float(f[key]) for f in frames) for key in ('cpuMs', 'gpuMs', 'draws', 'copies', 'targets')}}
                records.append(record);print(json.dumps(record), flush=True)
    summary = []
    for phase, mode, order in phases:
        row = {'phase': phase, 'mode': mode}
        for label in dict.fromkeys(order):
            values = [r for r in records if r['phase'] == phase and r['mode'] == mode and r['label'] == label]
            assert len(values) == 2 * args.rounds
            row[label] = {k: statistics.median(v[k] for v in values) for k in ('cpuMs', 'gpuMs', 'draws', 'copies', 'targets')}
        if phase == 'point':
            before = {r['image_sha256'] for r in records if r['phase'] == phase and r['label'] == 'before'}
            after = {r['image_sha256'] for r in records if r['phase'] == phase and r['label'] == 'pr'}
            assert before == after and len(before) == 1, 'Point optimization changed the deterministic SDR output'
        summary.append(row)
    unchanged = all(source_identity(Path(repo)) == identity for repo, identity in identities.items()) and all(digest(Path(p)) == sha for p, sha in hashes.items())
    assert unchanged, 'Inputs changed during comparison'
    report = {'pass': True, 'source_identities': identities, 'inputs': hashes, 'builds': builds, 'inputs_unchanged': unchanged,
              'upstream': git(upstream, 'rev-parse', 'HEAD'), 'head': git(ROOT, 'rev-parse', 'HEAD'), 'before_ref': args.before_ref,
              'scope': __doc__, 'game_fps_measured': False, 'total_northlight_cost_measured': False,
              'order': 'ABBA', 'rounds': args.rounds, 'records': records, 'summary': summary}
    (out / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(summary, indent=2), flush=True)


if __name__ == '__main__':main()
