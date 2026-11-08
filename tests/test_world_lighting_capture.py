#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client
"""Actual diagnostic depth/state/export roundtrip; no game or timing benchmark."""
from pathlib import Path
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

out = fp.output_dir()
out.mkdir(parents=True, exist_ok=True)
folder = Path(tempfile.mkdtemp(prefix='world-lighting-capture-', dir=out))
backend = Path(os.environ['NORTHLIGHT_TEST_D3D9']) if os.environ.get('NORTHLIGHT_TEST_D3D9') else fp.client_root() / 'renderer-backends/dxvk/dxvk_d3d9.dll'
assert backend.is_file(), backend
inputs = [fp.REPO / name for name in (
    'src/world/world_lighting_capture.h', 'src/lights/local_light_selection.h',
    'src/lights/world_local_lights.h', 'src/lights/local_light_scissor.h',
    'src/generated/forwarders.h', 'tests/world_lighting_capture_gpu.cpp',
    'tests/test_world_lighting_capture.py', 'northlight_paths.py')]
guard = {str(p.relative_to(fp.REPO)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(inputs)}
backend_hash = hashlib.sha256(backend.read_bytes()).hexdigest()
adapter = int(os.environ.get('NORTHLIGHT_TEST_D3D9_ADAPTER', '0'))
assert 0 <= adapter < 16
def game_boundary():
    process = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq Wow.exe', '/FO', 'CSV', '/NH'], capture_output=True, text=True, check=True)
    running = any(line.startswith('"') and line.split(',')[0].strip('"').lower() == 'wow.exe' for line in process.stdout.splitlines())
    assert not running or os.environ.get('NORTHLIGHT_TEST_ALLOW_RUNNING_GAME') == '1', 'Set NORTHLIGHT_TEST_ALLOW_RUNNING_GAME=1 for explicitly authorized correctness-only testing'
    return process.stdout

before_game = game_boundary()
private_backend = folder / 'backend.dll'
shutil.copyfile(backend, private_backend)
assert hashlib.sha256(private_backend.read_bytes()).hexdigest() == backend_hash
temporary = folder / 'tmp'
temporary.mkdir()
config = folder / 'dxvk.conf'
config.write_text('dxvk.allowFse = False\nd3d9.presentInterval = 0\nd3d9.supportDFFormats = True\n', encoding='utf-8')
environment = dict(fp.zig_env(), TEMP=str(temporary), TMP=str(temporary),
                   ZIG_LOCAL_CACHE_DIR=str(folder / 'zig-local-cache'),
                   ZIG_GLOBAL_CACHE_DIR=str(folder / 'zig-global-cache'),
                   DXVK_LOG_PATH=str(folder), DXVK_CONFIG_FILE=str(config),
                   DXVK_SHADER_CACHE_PATH=str(folder / 'shader-cache'),
                   DXVK_STATE_CACHE_PATH=str(folder / 'state-cache'))
binary = folder / 'fixture-lighting-capture.exe'
command = [str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG',
           '-Wno-microsoft-exception-spec', '-std=c++17', *fp.test_include_flags(),
           str(fp.TESTS / 'world_lighting_capture_gpu.cpp'), '-luser32', '-o', str(binary)]
compiled = subprocess.run(command, env=environment, capture_output=True, text=True)
(folder / 'compile.log').write_text(compiled.stdout + compiled.stderr, encoding='utf-8')
assert compiled.returncode == 0, compiled.stdout + compiled.stderr
run = subprocess.run([str(binary), str(private_backend), str(folder), str(adapter)], cwd=folder, env=environment,
                     capture_output=True, text=True, timeout=180)
(folder / 'gpu.log').write_text(run.stdout + run.stderr, encoding='utf-8')
assert run.returncode == 0, run.stdout + run.stderr
assert 'NATIVE_CAPTURE_PASS draws=128 imagePairs=8 exactShaders=1 exactConstants=1 stateUnchanged=1' in run.stdout
native_journals=list(folder.glob('capture-*-native-inputs.jsonl'));assert len(native_journals)==1
native_events=[json.loads(line) for line in native_journals[0].read_text().splitlines()]
assert sum(e['event']=='native-draw' for e in native_events)==128
assert sum(e['event']=='native-image-roi' for e in native_events)==16
assert native_events[-1]['export_complete']
slots=[e for e in native_events if e['event']=='native-slot']
assert len(slots)==512
assert all(e['status']=='no-binding' and e['candidate_source_id']=='0' for e in slots if e['draw']==1)
for draw in range(1,129):
    group=[e for e in slots if e['draw']==draw]
    assert [e['native_slot'] for e in group]==[0,1,2,3]
    assert all(e['native_slot_count']==4 for e in group)
    assert group[2]['status']=='no-binding' and group[2]['candidate_source_id']=='0'
    assert group[2]['position']==[1,2,3] and group[2]['point_slot']==-1
assert not any(e.get('status')=='disabled' for e in native_events)
results=[e for e in native_events if e['event']=='native-result']
assert len(results)==128
for i,e in enumerate(results):
    assert e['draw']==i+1 and e['corrected_sources']==i%2
    assert e['renderer_counters_observed']==bool(i) and not e['fallback_cause_proven']
    assert (e['cold_pairs_delta'],e['deferred_pairs_delta'],e['fallbacks_delta'])==(i%3,i%5,i%7)


assert next(e for e in native_events if e['event']=='native-summary')['skipped_draws']==2
depth_formats = run.stdout.count('LIGHT_CAPTURE_DEPTH ')
assert depth_formats in (1, 2), run.stdout
assert f'LIGHT_CAPTURE_PASS depthFormats={depth_formats} exactDepthPixels={851*depth_formats} apiFailureCases=22 constantRegisters=116 stateVS=256 statePS=224 timing=none' in run.stdout, run.stdout
complete = sorted(folder.glob('capture-*-lighting-inputs.jsonl'))
incomplete = sorted(folder.glob('capture-*-lighting-inputs-incomplete.jsonl'))
assert len(complete) == depth_formats + 1 and len(incomplete) == 22
for path in complete + incomplete:
    events = [json.loads(line) for line in path.read_text(encoding='utf-8').splitlines()]
    assert events[0]['event'] == 'begin' and events[-1]['event'] == 'end'
    assert events[0]['constant_register_count'] == 116 and not events[0]['depth_reconstructed']
    assert not events[0]['fps_eligible'] and not events[0]['whole_scene_replay']
    assert not events[0]['material_replay_complete'] and not events[0]['per_sample_color_exported']
    assert not events[0]['stencil_image_exported'] and not events[0]['scene_boundary_changed']
    assert events[-1]['export_complete'] == (path in complete)
    directional=[e for e in events if e['event']=='directional-map']
    if directional:
        assert len(directional)==4 and [e['action'] for e in directional]==['-','-','R','U']
        assert directional[3]['product']=='shadow-far' and directional[3]['source']==1
        assert directional[3]['receiver_matrix']==[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]
    publications=[e for e in events if e['event']=='point-publication']
    for e in publications:
        assert e['slot']==0 and e['schedule_source_id']=='42' and e['cache_source_id']=='42'
        assert e['map']=='Azeroth' and e['schedule_generation']=='17' and len(e['faces'])==6
        assert e['faces'][0]['static_content']==['11','22'] and e['faces'][0]['static_serial']=='17'

    assert events[0]['renderer_frame_known'] and events[0]['renderer_frame'] and events[0]['world_frame']
assert guard == {relative: hashlib.sha256((fp.REPO / relative).read_bytes()).hexdigest() for relative in guard}, 'Source changed during focused acceptance'
assert backend_hash == hashlib.sha256(backend.read_bytes()).hexdigest(), 'Backend changed during acceptance'
assert backend_hash == hashlib.sha256(private_backend.read_bytes()).hexdigest(), 'Private backend changed during acceptance'
after_game = game_boundary()
files = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(folder.iterdir()) if p.is_file()}
(folder / 'validation.json').write_text(json.dumps(dict(status='pass', command=command, source_guard=guard,
    backend=str(backend), backend_sha256=backend_hash, adapter=adapter,
    dxvk_device_filter=environment.get('DXVK_FILTER_DEVICE_NAME'),
    files=files, captures=depth_formats+1, depth_formats=depth_formats,
    unsupported_depth_formats=re.findall(r'LIGHT_CAPTURE_UNSUPPORTED format=(\w+)', run.stdout), incomplete_controls=22,
    game_before=before_game, game_after=after_game,
    scope='Supported odd-size DF24/INTZ helper inputs, exact 0/1 pixel centers and state/constant transport; unsupported formats explicitly listed',
    authentic_source_replay=False, whole_scene_replay=False, timing_benchmark=False), indent=2) + '\n', encoding='utf-8')
print(run.stdout.strip())
