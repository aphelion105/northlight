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
    parser.add_argument('--baseline', type=Path, help='Optional clean PR checkout for a complete before/after comparison.')
    parser.add_argument('--profile-stages', action='store_true', help='After uninstrumented ABBA timings, collect separate CPU/GPU stage profiles.')
    parser.add_argument('--branch-features', action='store_true', help='Also run the current world graph with HDR, rain/snow/sand optics and wide bloom; excludes native material/draw capture.')
    parser.add_argument('--reuse-builds',type=Path,help='Reuse verified executables whose source identities and harness/backend hashes still match an earlier report.')
    args = parser.parse_args()
    assert 1 <= args.rounds <= 10
    assert not (args.before_ref and args.baseline), 'Choose a complete baseline or a point-only control'
    upstream, capture, backend, out = (p.resolve() for p in (args.upstream, args.capture, args.backend, args.output))
    out.mkdir(parents=True, exist_ok=False)
    git = lambda repo, *a: subprocess.check_output(['git', '-C', str(repo), *a], text=True).strip()
    assert not git(upstream, 'diff', 'HEAD', '--', 'src', 'shaders'), 'Upstream production sources must be unchanged'
    baseline = args.baseline.resolve() if args.baseline else None
    if baseline:assert not git(baseline, 'diff', 'HEAD', '--', 'src', 'shaders'), 'Baseline sources must be unchanged'
    identities = {str(repo): source_identity(repo) for repo in (ROOT, upstream, *([baseline] if baseline else []))}
    digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    inputs = [Path(__file__).resolve(), ROOT / 'tests/upstream_pipeline_gpu.cpp', ROOT / 'tests/upstream_stage_probe.h', ROOT / 'tests/branch_feature_graph.h', backend,
              *(capture / name for name in ('capture-1-scene.fgr', 'capture-1-depth.fgr', 'capture-1-constants.f32'))]
    hashes = {str(p): digest(p) for p in inputs}
    reuse=json.loads((args.reuse_builds.resolve()/'validation.json').read_text()) if args.reuse_builds else None
    if reuse:assert reuse['pass'] and (not args.profile_stages or reuse['stage_records'])
    control = None
    if args.before_ref:
        control = out / 'control';control.mkdir()
        (control / 'world_point_rendering.inl').write_bytes(subprocess.check_output(
            ['git', 'show', args.before_ref + ':src/world/world_point_rendering.inl'], cwd=ROOT))
    env = {**current.zig_env(), 'NORTHLIGHT_ZIG': str(current.zig()), 'DISABLE_VULKAN_OBS_CAPTURE': '1'}
    builds = {}
    for label, repo in [('upstream', upstream), ('pr', ROOT)] + ([('before', baseline or ROOT)] if control or baseline else []):
        spec = importlib.util.spec_from_file_location('_pipeline_paths', repo / 'northlight_paths.py')
        fp = importlib.util.module_from_spec(spec);spec.loader.exec_module(fp)
        if reuse and label in reuse['builds'] and reuse['source_identities'].get(str(repo))==identities[str(repo)]:
            shared_inputs=[ROOT/'tests/upstream_pipeline_gpu.cpp',ROOT/'tests/upstream_stage_probe.h',ROOT/'tests/branch_feature_graph.h',backend]
            assert all(reuse['inputs'].get(str(p))==digest(p) for p in shared_inputs),'Reused harness/backend changed'
            cached=reuse['builds'][label];assert digest(Path(cached['exe']))==cached['sha256'] and cached['compiled']==str(fp.COMPILED)
            builds[label]={**cached,'reused_from':str(args.reuse_builds.resolve())};print('PIPELINE_BUILD_REUSED',label,flush=True);continue
        folder = out / ('build-' + label);folder.mkdir();headers = folder / 'headers';headers.mkdir()
        text = fp.src('world_renderer.h').read_text()
        assert text.count('class WorldRenderer {') == 1
        (headers / 'world_renderer.h').write_text(text.replace('class WorldRenderer {', 'class WorldRenderer {\nfriend struct PipelineCompareTest;', 1))
        if args.profile_stages:
            profile = fp.src('gpu_profile.h').read_text()
            replacements = {
                'return issue(s.stamps[0],D3DISSUE_END,"start timestamp");': 'const bool started=issue(s.stamps[0],D3DISSUE_END,"start timestamp");if(started)northlightCompareStage(nullptr);return started;',
                'label(s.labels[s.marks],name);': 'northlightCompareStage(name);label(s.labels[s.marks],name);',
                'Slot& s=slots_[active_];label(s.labels[s.marks],"tail");': 'Slot& s=slots_[active_];northlightCompareStage("tail");label(s.labels[s.marks],"tail");',
            }
            for marker, replacement in replacements.items():
                assert profile.count(marker) == 1, (label, marker)
                profile = profile.replace(marker, replacement)
            (headers / 'gpu_profile.h').write_text(profile.replace('%.3fms','%.6fms'), encoding='utf-8', newline='\n')
        build_env = env.copy()
        for key, name in [('TEMP', 'tmp'), ('TMP', 'tmp'), ('ZIG_LOCAL_CACHE_DIR', 'zig-local'), ('ZIG_GLOBAL_CACHE_DIR', 'zig-global')]:
            (folder / name).mkdir(exist_ok=True);build_env[key] = str(folder / name)
        units = ['world_gi.cpp', 'world_mesh_plan.cpp', 'static_shadow_scene.cpp', 'settings_overlay.cpp', 'settings_panel.cpp',
                 'imgui.cpp', 'imgui_draw.cpp', 'imgui_tables.cpp', 'imgui_widgets.cpp', 'imgui_impl_dx9.cpp']
        available = {p.name for p in (repo / 'src').rglob('*') if p.is_file()}
        assert all(name in available for name in units[:3])
        units = [name for name in units if name in available]
        exe = folder / 'pipeline.exe'
        command = [current.zig(), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                   '-Wno-return-type', '-Wno-microsoft-exception-spec', '-DIMGUI_USER_CONFIG="northlight_imconfig.h"',
                   f'-DNORTHLIGHT_COMPARE_PR={int(label != "upstream")}',
                   f'-DNORTHLIGHT_COMPARE_HAS_EQUIPMENT={int("equipmentLights" in fp.src("quality_settings.h").read_text())}', '-I' + str(headers),
                   *(['-DNORTHLIGHT_COMPARE_STAGES=1'] if args.profile_stages else []),
                   *(['-I' + str(control)] if label == 'before' and control else []), *fp.test_include_flags(),
                   ROOT / 'tests/upstream_pipeline_gpu.cpp', *(fp.src(n) for n in units),
                   '-luser32', '-luuid', '-o', exe]
        with (folder / 'compile.log').open('w') as log:
            subprocess.run(list(map(str, command)), cwd=repo, env=build_env, stdout=log, stderr=subprocess.STDOUT, check=True)
        data = bytearray(exe.read_bytes());at = struct.unpack_from('<I', data, 0x3c)[0] + 22
        struct.pack_into('<H', data, at, struct.unpack_from('<H', data, at)[0] | 0x20);exe.write_bytes(data)
        builds[label] = {'exe': str(exe), 'sha256': digest(exe), 'compiled': str(fp.COMPILED)}
        smoke = folder / 'smoke';smoke.mkdir()
        result = subprocess.run([str(exe), str(backend), str(smoke), str(capture), str(fp.COMPILED), '2'],
                                env={**env, 'DXVK_LOG_PATH': str(smoke)}, cwd=smoke, capture_output=True, text=True, timeout=120)
        (smoke / 'run.log').write_text(result.stdout + result.stderr);result.check_returncode()
        assert 'COMMON_PIPELINE_PASS' in result.stdout
        print('PIPELINE_BUILD_PASS', label, flush=True)
        if args.branch_features and label != 'upstream':
            for features in (3,19):
                folder=folder.parent/f'feature-smoke-{label}-{features}';folder.mkdir()
                run=subprocess.run([str(exe),str(backend),str(folder),str(capture),str(fp.COMPILED),'2',str(features)],env={**env,'DXVK_LOG_PATH':str(folder)},cwd=folder,capture_output=True,text=True,timeout=120)
                (folder/'run.log').write_text(run.stdout+run.stderr);run.check_returncode();assert 'COMMON_PIPELINE_PASS' in run.stdout
    records = []
    phases = [('upstream', mode, ('upstream', 'pr', 'pr', 'upstream')) for mode in range(4)]
    if control:phases.append(('point', 2, ('before', 'pr', 'pr', 'before')))
    if baseline:phases += [('baseline', mode, ('before', 'pr', 'pr', 'before')) for mode in range(4)]
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
    feature_records=[];feature_summary=[]
    if args.branch_features:
        order=('before','pr','pr','before') if baseline else ('pr',)
        for features in (1,2,3,19,5,9):
            for repeat in range(args.rounds):
                for position,label in enumerate(order):
                    folder=out/f'features-{features}-{repeat}-{position}-{label}';folder.mkdir()
                    run=subprocess.run([builds[label]['exe'],str(backend),str(folder),str(capture),builds[label]['compiled'],'2',str(features)],env={**env,'DXVK_LOG_PATH':str(folder)},cwd=folder,capture_output=True,text=True,timeout=120)
                    (folder/'run.log').write_text(run.stdout+run.stderr);run.check_returncode();assert 'COMMON_PIPELINE_PASS' in run.stdout
                    frames=[dict(re.findall(r'(\w+)=([^ ]+)',line)) for line in run.stdout.splitlines() if line.startswith('COMMON_FRAME ') and 'warm=0 ' in line];assert len(frames)==32
                    record=dict(features=features,round=repeat,order=position,label=label,image_sha256=digest(folder/'output.bgra'),**{key:statistics.median(float(f[key]) for f in frames) for key in ('cpuMs','gpuMs','draws','copies','targets')})
                    feature_records.append(record);print('FEATURE_RECORD '+json.dumps(record),flush=True)
            feature_summary.append(dict(features=features,**{label:{key:statistics.median(r[key] for r in feature_records if r['features']==features and r['label']==label) for key in ('cpuMs','gpuMs','draws','copies','targets')} for label in dict.fromkeys(order)}))
    stage_records = []
    if args.profile_stages:
        for mode in range(4):
            for repeat in range(2):
                for position, label in enumerate(('upstream','pr','pr','upstream')):
                    folder = out / f'stages-{mode}-{repeat}-{position}-{label}';folder.mkdir()
                    run = subprocess.run([builds[label]['exe'], str(backend), str(folder), str(capture), builds[label]['compiled'], str(mode), 'stages'],
                                         env={**env, 'DXVK_LOG_PATH': str(folder)}, cwd=folder, capture_output=True, text=True, timeout=120)
                    (folder / 'run.log').write_text(run.stdout + run.stderr);run.check_returncode();assert 'COMMON_PIPELINE_PASS' in run.stdout
                    cpu = [dict(re.findall(r'(\w+)=([^ ]+)', line)) for line in run.stdout.splitlines() if line.startswith('COMMON_STAGE_CPU ')]
                    gpu = []
                    for line in (folder / 'stages.log').read_text().splitlines():
                        if not re.search(r'GPU profile frame=\d+ total=', line):continue
                        frame = int(re.search(r'frame=(\d+)', line)[1]) // 120
                        gpu += [dict(frame=frame, stage=stage, ms=float(ms)) for stage, ms in re.findall(r'(\w+)=([0-9.]+)ms', line) if stage != 'total']
                    for domain, rows in [('cpu',cpu),('gpu',gpu)]:
                        rows = [r for r in rows if int(r['frame']) >= 16];assert rows,(label,mode,domain)
                        for stage in dict.fromkeys(r['stage'] for r in rows):
                            values = [float(r['ms']) for r in rows if r['stage'] == stage]
                            stage_records.append(dict(mode=mode, repeat=repeat, order=position, label=label, domain=domain, stage=stage, median_ms=statistics.median(values), maximum_ms=max(values), samples=len(values)))
                    print('PIPELINE_STAGE_PASS', mode, repeat, position, label, flush=True)
        if args.branch_features:
            for features in (1,3,19):
                for repeat in range(2):
                    folder=out/f'feature-stages-{features}-{repeat}';folder.mkdir()
                    run=subprocess.run([builds['pr']['exe'],str(backend),str(folder),str(capture),builds['pr']['compiled'],'2',str(features),'stages'],env={**env,'DXVK_LOG_PATH':str(folder)},cwd=folder,capture_output=True,text=True,timeout=120)
                    (folder/'run.log').write_text(run.stdout+run.stderr);run.check_returncode();assert 'COMMON_PIPELINE_PASS' in run.stdout
                    for domain in ('cpu','gpu'):
                        if domain=='cpu':rows=[dict(re.findall(r'(\w+)=([^ ]+)',line)) for line in run.stdout.splitlines() if line.startswith('COMMON_STAGE_CPU ')]
                        else:
                            rows=[]
                            for line in (folder/'stages.log').read_text().splitlines():
                                if not re.search(r'GPU profile frame=\d+ total=',line):continue
                                frame=int(re.search(r'frame=(\d+)',line)[1])//120
                                rows += [dict(frame=frame,stage=stage,ms=float(ms)) for stage,ms in re.findall(r'(\w+)=([0-9.]+)ms',line) if stage!='total']
                        rows=[r for r in rows if int(r['frame'])>=16];assert rows
                        for stage in dict.fromkeys(r['stage'] for r in rows):
                            values=[float(r['ms']) for r in rows if r['stage']==stage]
                            stage_records.append(dict(mode=2,features=features,repeat=repeat,label='pr',domain=domain,stage=stage,median_ms=statistics.median(values),maximum_ms=max(values),samples=len(values)))
                    print('FEATURE_STAGE_PASS',features,repeat,flush=True)
    unchanged = all(source_identity(Path(repo)) == identity for repo, identity in identities.items()) and all(digest(Path(p)) == sha for p, sha in hashes.items())
    assert unchanged, 'Inputs changed during comparison'
    report = {'pass': True, 'source_identities': identities, 'inputs': hashes, 'builds': builds, 'inputs_unchanged': unchanged,
              'upstream': git(upstream, 'rev-parse', 'HEAD'), 'head': git(ROOT, 'rev-parse', 'HEAD'), 'before_ref': args.before_ref,
              'baseline': git(baseline, 'rev-parse', 'HEAD') if baseline else None,
              'scope': __doc__, 'game_fps_measured': False, 'total_northlight_cost_measured': False,
              'order': 'ABBA', 'rounds': args.rounds, 'records': records, 'summary': summary, 'stage_records': stage_records,
              'feature_records': feature_records, 'feature_summary': feature_summary,
              'feature_scope': 'World/AO plus actual HDR and weather post passes on captured inputs. Native game draw recording, material/normal capture, scene streaming, water capture, UI, monitor presentation and game FPS are not measured.',
              'stage_scope': 'Separate intrusive QPC/timestamp profile; excluded from primary ABBA totals. Stage GPU intervals include command supply gaps.'}
    (out / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(summary, indent=2), flush=True)


if __name__ == '__main__':main()
