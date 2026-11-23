#!/usr/bin/env python3
# northlight-test:
"""Exercise exact resolved-plane acceptance without a game or GPU."""
from argparse import Namespace
from io import BytesIO
import json
from pathlib import Path
import struct
import sys
from tempfile import TemporaryDirectory

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

sys.path.insert(0, str(fp.SCRIPTS))
import northrend_frame_parity as parity
import northrend_point_shadows as points
import northrend_trace_probe as trace_probe
import run_northrend_baseline as runner

world_constants = fp.src('world_frame_constants.inl').read_text()
visual = world_constants.split('const DWORD now=visualTime();', 1)[1]
assert 'legacyFogHold.update(frameMap,legacyFog,legacyFogKnown,now)' in visual
assert 'horizonHazeState.update(frameMap,legacyFog.parameters,legacyFog.color,projection[2],double(now)*.001)' in visual
assert 'apparentHorizon.update(target,now)' in visual
weather = fp.src('renderer.cpp').read_text().split('void weatherFrame(unsigned sampleFrame)', 1)[1]
weather = weather.split('void logWeatherProbe(', 1)[0]
assert 'fixedClock||QueryPerformanceCounter(&now)' in weather
assert weather.index('NorthlightFrameParityClock::counter(sampleFrame,cpuFrequency.QuadPart)') < weather.index('now.QuadPart-weatherTick')
assert 'surfaceTime+=std::clamp(dt,0.,.1)' in weather and 'weatherTracker.frame(weatherSample,float(dt))' in weather
world = fp.src('world_renderer.h').read_text()
assert 'paletteTransition.update(target,map,camera[0],camera[1],double(visualTime())*.001)' in world
assert 'celestialLightMotion.update(orbit,celestial.dayFraction,visualTime(),' in world
held_fog = world.split('legacyFogConstants()const{', 1)[1].split('return legacyFog;', 1)[0]
assert 'DWORD(visualTime()-legacyFogHold.lastUpdate)' in held_fog

native_shader_log = ('NATIVE more lights status=active requested=1 max=8 '
                     'runtime-only=1 files-written=0 shader-path=1')
assert runner.captured_native_more_lights([native_shader_log])
assert runner.captured_native_more_lights([native_shader_log.replace('status=active', 'status=already-active')])
assert not runner.captured_native_more_lights([])
assert not runner.captured_native_more_lights(
    [native_shader_log.replace('status=active requested=1', 'status=disabled requested=0')])

settings = '[Quality]\nPointShadowAdaptiveRefresh=0\nPointShadowLimit=64\n[Other]\nPointShadowAdaptiveRefresh=0\n'
assert runner.replay_quality_config(settings, adaptive=True) == settings.replace('Refresh=0', 'Refresh=1', 1)
assert runner.replay_quality_config(settings, limit=12, adaptive=False) == (
    '[Quality]\nPointShadowLimit=12\nPointShadowAdaptiveRefresh=0\n[Other]\nPointShadowAdaptiveRefresh=0\n')


def rejected(action):
    try:
        action()
    except (ValueError, OSError):
        return
    raise AssertionError('Accepted invalid parity evidence')


def check_trace_probe():
    first = b'P6\n#17\n2 1\n255\n\x01\x02\x03\x04\x05\x06'
    second = b'P6\n#19\n2 1\n255\n\xf1\xf2\xf3\xf4\xf5\xf6'
    size, points = (2, 1), [(1, 0), (0, 0)]
    assert trace_probe.read_samples(BytesIO(first + second), [17, 19], size, points) == {
        '17': ['040506', '010203'], '19': ['f4f5f6', 'f1f2f3']}
    rejected(lambda: trace_probe.read_samples(BytesIO(first + second), [17, 18], size, points))
    rejected(lambda: trace_probe.read_samples(BytesIO(first + second[:-1]), [17, 19], size, points))
    rejected(lambda: trace_probe.read_samples(BytesIO(first + second), [17], size, points))
    log = ('info:  Effective configuration:\n'
           'info:    d3d9.cachedWriteOnlyBuffers = True\n'
           'info:    dxvk.allowFse = False\n'
           'info:  Vulkan: Found vkGetInstanceProcAddr\n')
    assert trace_probe.effective_dxvk_config(log * 2) == {
        'd3d9.cachedWriteOnlyBuffers': 'True', 'dxvk.allowFse': 'False'}
    rejected(lambda: trace_probe.effective_dxvk_config(log + log.replace('Buffers = True', 'Buffers = False')))
    rejected(lambda: trace_probe.effective_dxvk_config('info:  DXVK: v3.1.1\n'))
    rejected(lambda: runner.captured_native_more_lights([native_shader_log, native_shader_log]))
    rejected(lambda: runner.captured_native_more_lights([native_shader_log.replace('max=8', 'max=4')]))
    far_shadow_marker = 'FRAME_PARITY_FAR_SHADOWS aligned=1'
    runner.verify_far_shadow_alignment([far_shadow_marker], True)
    runner.verify_far_shadow_alignment([], False)
    rejected(lambda: runner.verify_far_shadow_alignment([], True))
    rejected(lambda: runner.verify_far_shadow_alignment([far_shadow_marker], False))
    rejected(lambda: runner.verify_far_shadow_alignment([far_shadow_marker, far_shadow_marker], True))

    # Compare both transitions on a cropped target. The unused alpha bytes
    # differ and should stay outside the RGB test.
    with TemporaryDirectory(dir=fp.output_dir()) as temporary:
        root = Path(temporary)
        dxvk_log = root / 'Wow_d3d9.log'
        enumeration = 'info:  Found device: NVIDIA GeForce RTX 4090\n'
        creation = 'info:  Creating device:\ninfo:  NVIDIA GeForce RTX 4090:\n'
        dxvk_log.write_text(enumeration, encoding='utf-8')
        rejected(lambda: runner.dxvk_adapter(dxvk_log))
        dxvk_log.write_text(enumeration + creation * 2, encoding='utf-8')
        assert runner.dxvk_adapter(dxvk_log)['device'] == 'NVIDIA GeForce RTX 4090'
        assert runner.dxvk_adapter(dxvk_log, 'RTX 4090')['filter'] == 'RTX 4090'
        rejected(lambda: runner.dxvk_adapter(dxvk_log, 'Radeon'))
        dxvk_log.write_text(enumeration + creation + creation.replace('NVIDIA GeForce RTX 4090', 'Radeon'),
                            encoding='utf-8')
        rejected(lambda: runner.dxvk_adapter(dxvk_log))
        journal, before, after = (root / name for name in parity.DRAW_WITNESS)
        row = dict(call=19, rect=[1, 0, 2, 2], size=[2, 2], format=22, samples=2, sample_quality=0,
                   before='00000000', draw='00000000', after='00000000')
        journal.write_text(json.dumps(row), encoding='utf-8')
        before.write_bytes(bytes.fromhex('030201ff 060504ff'))
        after.write_bytes(bytes.fromhex('0302ffff 09080700'))
        receipt, live = parity.draw_witness(root, (2, 2), 2)
        assert len(receipt['artifacts_sha256']) == 3
        stock = [bytes.fromhex('ffffff 010203 ffffff 000000'), bytes.fromhex('ffffff 010203 ffffff 070809')]
        assert trace_probe.compare_draw_images(live, stock, (2, 2), receipt['rect']) == dict(
            pixels=2, rgb_different_before=1, rgb_different_after=1, became_different=1,
            became_equal=1, live_changed=2, stock_changed=1)
        rejected(lambda: parity.draw_witness(root, (2, 2), 1))
        after.write_bytes(b'bad')
        rejected(lambda: parity.draw_witness(root, (2, 2), 2))


def write_capture(folder, frame, *, depth=0x3f000000, color=0x3c00000000000000, samples=2, native=None):
    prefix = folder / f'capture-{frame}'
    journal = [dict(event='begin', capture=frame, renderer_frame=frame, width=2, height=1, scope='resolved-presented-frame'),
               dict(event='depth-resolve', source_format=75, source_samples=samples, sample_quality=0, method='RESZ-INTZ'),
               dict(event='image', name='color', samples=samples, sample_quality=0),
               dict(event='color-resolve', source_samples=samples, method='StretchRect-NONE'),
               dict(event='image', name='color', samples=0, sample_quality=0),
               dict(event='color-resolve', name='native-color', source_samples=samples, method='StretchRect-NONE',
                    per_sample_image=False)]
    native_path = prefix.with_name(prefix.name + '-native-color.fgr')
    if native is not None:
        journal += [dict(event='image', name='native-color', width=2, height=1, format=22,
                         samples=count, sample_quality=0) for count in (samples, 0)]
        data = struct.pack('<4s4I2I', b'FGR1', 2, 1, 22, 4, *native)
        native_path.write_bytes(data)
        journal.append(dict(event='file', name='native-color.fgr', bytes=len(data), saved=True))
    else:
        native_path.unlink(missing_ok=True)
    journal.append(dict(event='end', export_complete=True))
    prefix.with_name(prefix.name + '-frame-parity.jsonl').write_text(
        '\n'.join(map(json.dumps, journal)), encoding='utf-8')
    for name, (fmt, stride, code) in parity.PLANES.items():
        data = struct.pack('<4s4I', b'FGR1', 2, 1, fmt, stride)
        data += struct.pack('<2' + code, 0, depth if name == 'depth' else color)
        prefix.with_name(prefix.name + f'-{name}.fgr').write_bytes(data)


def write_composition(folder, frame, second):
    prefix = folder / f'capture-{frame}'
    journal_path = prefix.with_name(prefix.name + '-frame-parity.jsonl')
    journal = [json.loads(line) for line in journal_path.read_text(encoding='utf-8').splitlines()]
    material = []
    for name, (fmt, stride, code) in parity.COMPOSITION_PRODUCTS.items():
        width = 1 if name == 'hdr-exposure' else 2
        pixels = {'material-height': (4 + second, 8 + second), 'material-occlusion': (0, 0),
                  'hdr-linear': (0, 16 + second), 'hdr-exposure': (0x3c00000000000000,)}[name]
        data = struct.pack('<4s4I', b'FGR1', width, 1, fmt, stride) + struct.pack(f'<{width}{code}', *pixels)
        prefix.with_name(prefix.name + f'-{name}.fgr').write_bytes(data)
        if name.startswith('hdr-'):
            journal[-1:-1] = [dict(event='image', name=name, width=width, height=1, format=fmt, samples=0,
                                    sample_quality=0), dict(event='file', name=f'{name}.fgr', bytes=len(data), saved=True)]
        else:
            skipped = name == 'material-occlusion'
            material.append(dict(product=name, renderer_frame=frame, state='skipped' if skipped else 'current',
                                 saved=True, zero_filled=skipped, fps_eligible=False))
    journal_path.write_text('\n'.join(map(json.dumps, journal)), encoding='utf-8')
    prefix.with_name(prefix.name + '-products.jsonl').write_text('\n'.join(map(json.dumps, material)), encoding='utf-8')
    return journal


def check_composition(root):
    live, stand = (root / name for name in ('live', 'stand'))
    frame, size = 100, (2, 1)
    for step, folder in enumerate((live, stand)):
        folder.mkdir(parents=True)
        write_capture(folder, frame, color=0x3c00000000000000 + step)
        write_composition(folder, frame, step)
    comparison = parity.compare(live, stand, size, {frame: 0}, {str(frame): 2811})
    assert not comparison['match'] and comparison['plane_match']['depth']
    products = comparison['frames'][0]['composition']
    material = products['material-height']
    assert material['changed_pixels'] == 2 and material['first_changed_xy'] == [0, 0]
    assert material['overlap_with_presented_color'] == 1
    assert material['first_presented_color_overlap'] == dict(
        xy=[1, 0], left_storage_hex='0800000000000000', right_storage_hex='0900000000000000')
    assert products['hdr-linear']['changed_pixels'] == products['hdr-linear']['overlap_with_presented_color'] == 1
    assert products['hdr-exposure']['changed_pixels'] == 0 and 'overlap_with_presented_color' not in products['hdr-exposure']
    assert products['material-occlusion']['state'] == 'skipped'
    assert comparison['frames'][0]['scene_input_color']['state'] == 'unavailable'
    for step, folder in enumerate((live, stand)):
        name = 'scene-input-color'
        data = struct.pack('<4s4I2I', b'FGR1', *size, 22, 4, 0x1000000 * step, 7 + step)
        (folder / f'capture-{frame}-{name}.fgr').write_bytes(data)
        journal = [dict(event='begin', capture=frame, renderer_frame=frame, width=2, height=1, scope='atmosphere-pass-bindings'),
                   dict(event='image', name=name, width=2, height=1, format=22, samples=0, sample_quality=0),
                   dict(event='file', name=f'{name}.fgr', bytes=len(data), saved=True),
                   dict(event='end', export_complete=True)]
        (folder / f'capture-{frame}-frame-parity-atmosphere.jsonl').write_text(
            '\n'.join(map(json.dumps, journal)), encoding='utf-8')
    comparison = parity.compare(live, stand, size, {frame: 0}, {str(frame): 2811})
    before_effects = comparison['frames'][0]['scene_input_color']
    assert before_effects['changed_pixels'] == 1 and before_effects['changed_storage_pixels'] == 2
    assert before_effects['overlap_with_presented_color'] == 1
    path = stand / 'capture-100-material-height.fgr'
    path.write_bytes(path.read_bytes()[:-1])
    rejected(lambda: parity.compare(live, stand, size, {frame: 0}, {str(frame): 2811}))


def compare_replays(root):
    root = root.resolve()
    capture, ref, candidate = (root / name for name in ('capture', 'reference', 'candidate'))
    capture.mkdir()
    trace = capture / 'wow.trace'
    trace.write_bytes(b'test evidence')
    trace_hash = runner.digest(trace)
    frames = (100, 108)
    summary = {'selected_presents': {'100': 2811, '108': 4199}}
    runner.write_json(capture / 'validation.json', dict(status='PASS', trace={'sha256': trace_hash},
                      parity_frames={str(frame): {'quality': 0} for frame in frames}))

    def write_replay(folder, dll, summary, color):
        images = folder / 'frame-parity'
        images.mkdir(parents=True, exist_ok=True)
        plan = folder / 'plan'
        plan.mkdir(exist_ok=True)
        (plan / 'validation.json').write_text(json.dumps({'pass': True, 'authored_transport_only': False,
                                            'inputs': {str(trace): trace_hash, str(folder / 'stand/northlight-stand.dll'): dll},
                                            'summary': summary}), encoding='utf-8')
        rows = []
        for frame in frames:
            write_capture(images, frame, color=color, native=(0x01020304, 0x05060708))
            native, _ = parity.native_color_plane(images, frame, (2, 1), 0)
            rows.append(dict(present_ordinal=frame, trace_call=summary['selected_presents'][str(frame)], sample_quality=0,
                             native_color={'stand': native},
                             **{name: {'stand_sha256': runner.digest(images / f'capture-{frame}-{name}.fgr')}
                                for name in parity.PLANES}))
        report = dict(status='FAIL', host_exit_code=0, host_result='NORTHLIGHT_NATIVE_BRIDGE PASS records=1',
                      trace_join_validated=True, stand_world_observed=True, stand_static_mesh_ready=True,
                      capture=str(capture), host_sha256='host', stand_assets_sha256='assets', stand_sha256=dll,
                      stand_identity=dll, stand_output={'msaa': 2, 'resolution': '2x1'}, trace_summary=summary,
                      parity={'frames': rows})
        (folder / 'validation.json').write_text(json.dumps(report), encoding='utf-8')

    def run(label, status):
        output = root / f'{label}.json'
        assert runner.compare_replays(Namespace(reference=ref, candidate=candidate, output=output)) == int(status != 'PASS')
        report = json.loads(output.read_text(encoding='utf-8'))
        assert report['status'] == status
        return report

    color = 0x3c00000000000000
    write_replay(ref, 'original', summary, color)
    write_replay(candidate, 'original', summary, color)
    assert run('same-dll', 'PASS')['mode'] == 'A/A'
    for folder, enabled in ((ref, False), (candidate, True)):
        receipt = folder / 'validation.json'
        value = json.loads(receipt.read_text(encoding='utf-8'))
        value.update(stand_point_effective_pose=enabled, point_effective_pose_samples={'100': 18 if enabled else 0})
        receipt.write_text(json.dumps(value), encoding='utf-8')
    pose_comparison = run('point-effective-pose', 'PASS')
    assert pose_comparison['mode'] == 'A/B'
    assert pose_comparison['reference']['point_effective_pose_samples'] == {'100': 0}
    assert pose_comparison['candidate']['point_effective_pose_samples'] == {'100': 18}
    for folder, enabled in ((ref, False), (candidate, True)):
        write_replay(folder, 'original', summary, color)
        receipt = folder / 'validation.json'
        value = json.loads(receipt.read_text(encoding='utf-8'))
        value.update(stand_normal_palette_instancing=enabled,
                     normal_palette_samples={'100': {'draws': 51 if enabled else 0, 'packets': 579 if enabled else 0}})
        receipt.write_text(json.dumps(value), encoding='utf-8')
    palette_comparison = run('normal-palette-instancing', 'PASS')
    assert palette_comparison['mode'] == 'A/B'
    assert palette_comparison['reference']['normal_palette_samples'] == {'100': {'draws': 0, 'packets': 0}}
    assert palette_comparison['candidate']['normal_palette_samples'] == {'100': {'draws': 51, 'packets': 579}}
    for folder, reuse in ((ref, False), (candidate, True)):
        receipt = folder / 'validation.json'
        value = json.loads(receipt.read_text(encoding='utf-8'))
        value.update(stand_normal_palette_instancing=True, stand_normal_palette_binding_reuse=reuse,
                     normal_palette_samples={'100': {'draws': 51, 'packets': 579,
                                                      'inheritedDraws': 9 if reuse else 0,
                                                      'skippedBinds': 45 if reuse else 0}})
        receipt.write_text(json.dumps(value), encoding='utf-8')
    binding_comparison = run('normal-palette-binding-reuse', 'PASS')
    assert binding_comparison['mode'] == 'A/B'
    assert binding_comparison['reference']['normal_palette_samples']['100']['skippedBinds'] == 0
    assert binding_comparison['candidate']['normal_palette_samples']['100']['skippedBinds'] == 45
    for folder, enabled in ((ref, False), (candidate, True)):
        write_replay(folder, 'original', summary, color)
        receipt = folder / 'validation.json'
        value = json.loads(receipt.read_text(encoding='utf-8'))
        value.update(stand_normal_palette_instance_rows=enabled,
                     normal_palette_instance_row_samples={'100': {'enabled': int(enabled),
                                                                   'draws': 14 if enabled else 0,
                                                                   'packets': 72 if enabled else 0}})
        receipt.write_text(json.dumps(value), encoding='utf-8')
    instance_rows = run('normal-palette-instance-rows', 'PASS')
    assert instance_rows['mode'] == 'A/B'
    assert instance_rows['reference']['normal_palette_instance_row_samples']['100']['draws'] == 0
    assert instance_rows['candidate']['normal_palette_instance_row_samples']['100']['packets'] == 72
    write_replay(ref, 'original', summary, color)
    write_replay(candidate, 'new', summary, color + 1)
    ab = run('color-mismatch', 'FAIL')
    assert ab['mode'] == 'A/B' and ab['comparison']['plane_match'] == {'color': False, 'depth': True}
    write_replay(candidate, 'new', summary, color)
    native = candidate / 'frame-parity/capture-100-native-color.fgr'
    raw = native.read_bytes()
    native.write_bytes(raw[:-1] + bytes([raw[-1] ^ 1]))
    assert 'native color image changed' in run('changed-native-image', 'INCOMPLETE')['error']
    write_replay(candidate, 'new', summary, color + 1)
    write_capture(candidate / 'frame-parity', 100, color=color + 2)
    assert 'image changed' in run('changed-image', 'INCOMPLETE')['error']
    write_replay(candidate, 'new', {'selected_presents': {'100': 2812, '108': 4199}}, color)
    assert 'Present calls' in run('different-call', 'INCOMPLETE')['error']
    write_replay(candidate, 'new', summary, color)
    candidate_plan = candidate / 'plan/validation.json'
    expected_plan = candidate_plan.read_bytes()
    candidate_plan.write_bytes((ref / 'plan/validation.json').read_bytes())
    assert 'Replay plan' in run('different-dll', 'INCOMPLETE')['error']
    candidate_plan.write_bytes(expected_plan)
    for step, folder in enumerate((ref, candidate)):
        images = folder / 'frame-parity'
        path = folder / 'validation.json'
        report = json.loads(path.read_text(encoding='utf-8'))
        for row in report['parity']['frames']:
            frame = row['present_ordinal']
            journal = write_composition(images, frame, step)
            row['composition'] = {name: {'stand': evidence} for name, evidence in
                                  parity.composition_receipts(images, frame, (2, 1), journal).items()}
        path.write_text(json.dumps(report), encoding='utf-8')
    products = run('diagnostic-only-mismatch', 'PASS')['comparison']['frames'][0]['composition']
    assert products['material-height']['changed_pixels'] == 2
    assert products['material-height']['reference']['sha256'] != products['material-height']['candidate']['sha256']
    product = candidate / 'frame-parity/capture-100-material-height.fgr'
    raw = product.read_bytes()
    product.write_bytes(raw[:-1] + bytes([raw[-1] ^ 1]))
    assert 'material-height product changed' in run('changed-composition-image', 'INCOMPLETE')['error']
    trace.write_bytes(b'changed test trace')
    assert 'trace changed' in run('changed-trace', 'INCOMPLETE')['error']


def write_history(folders):
    for index, folder in enumerate(folders):
        folder.mkdir(exist_ok=True)
        rows = []
        formats = {**parity.HISTORY_INPUTS, parity.PRE_GI_LIGHT: parity.PLANES['color'],
                   parity.RESOLVED_LIGHT: parity.PLANES['color']}
        for name, (fmt, stride, code) in formats.items():
            skipped = name == 'history-directional-visibility'
            pixels = (0, 0) if skipped else (0, index + 1)
            if name == 'history-prior-light':
                pixels = (index + 1, 0)
            (folder / f'capture-100-{name}.fgr').write_bytes(
                struct.pack('<4s4I', b'FGR1', 2, 1, fmt, stride) + struct.pack('<2' + code, *pixels))
            rows.append(dict(product=name, renderer_frame=100, state='skipped' if skipped else 'current',
                             saved=True, zero_filled=skipped, fps_eligible=False))
        (folder / 'capture-100-products.jsonl').write_text('\n'.join(map(json.dumps, rows)), encoding='utf-8')


def check_history(root):
    folders = (root / 'reference', root / 'candidate')
    size, frames = (4, 2), (100,)
    write_history(folders)
    saved = [parity.history_hashes(folder, size, frames) for folder in folders]
    results, overlap = parity.compare_history(folders, size, frames, saved)
    result = results['100']
    assert result['history-raw-light'] == {'state': 'current', 'changed_pixels': 1, 'first_changed_xy': [1, 0]}
    assert result[parity.PRE_GI_LIGHT] == result['history-raw-light']
    assert result[parity.RESOLVED_LIGHT] == result['history-raw-light']
    assert result['history-directional-visibility'] == {'state': 'skipped'}
    assert overlap['100'] == {parity.PRE_GI_LIGHT: 1, 'history-prior-light': 0}
    path = folders[1] / 'capture-100-history-raw-light.fgr'
    path.write_bytes((folders[0] / path.name).read_bytes())
    rejected(lambda: parity.compare_history(folders, size, frames, saved))
    journal = folders[1] / 'capture-100-products.jsonl'
    rows = journal.read_text(encoding='utf-8').splitlines()
    journal.write_text('\n'.join(rows + rows[:1]), encoding='utf-8')
    rejected(lambda: parity.history_hashes(folders[1], size, frames))


def write_directional(folders):
    frame = 100
    for index, folder in enumerate(folders):
        folder.mkdir(exist_ok=True)
        matrix = [0.] * 16
        matrix[14] = 0.5 + index / 8
        rows = [
            dict(event='begin', capture=frame, renderer_frame=frame, width=4, height=2,
                 scope='directional-shadow-projections'),
            dict(event='anchor', source_mask=1, shadow_pass=10, pivot=[0, 0, index],
                 cascade_anchor=[0, 0, 0], far_anchor=[0, 0, 0], far_radius=8),
            *(dict(event='projection', slot=slot, action='U', committed_pass=9, static_cache_generation=2,
                   static_cache_signature=3, planned_cache_offset=[0, 0, 0], planned_matrix=[0.] * 16,
                   receiver_matrix=matrix, cache_matrix=None) for slot in range(2)),
            dict(event='end', export_complete=True, render_completed=True),
        ]
        (folder / 'capture-100-frame-parity-directional.jsonl').write_text(
            '\n'.join(map(json.dumps, rows)), encoding='utf-8')


def check_directional(root):
    folders = tuple(root / label for label in ('reference', 'candidate'))
    frame, size = 100, (4, 2)
    write_directional(folders)
    saved = [parity.directional_inputs(folder, size, (frame,)) for folder in folders]
    result = parity.compare_directional(folders, size, (frame,), saved)['100']
    assert result['anchor_differences'] == {'pivot': [[0, 0, 0], [0, 0, 1]]}
    assert result['map_differences']['1'] == {'receiver_matrix': {'14': [.5, .625]}}
    path = folders[1] / 'capture-100-frame-parity-directional.jsonl'
    path.write_text(path.read_text(encoding='utf-8') + '\n', encoding='utf-8')
    rejected(lambda: parity.compare_directional(folders, size, (frame,), saved))
    lines = path.read_text(encoding='utf-8').splitlines()
    path.write_text('\n'.join(lines[:-2] + lines[-1:]) + '\n', encoding='utf-8')
    rejected(lambda: parity.directional_inputs(folders[1], size, (frame,)))


def write_gi(folders):
    frame = 100
    for index, folder in enumerate(folders):
        folder.mkdir(exist_ok=True)
        rows = [dict(event='begin', scope='gi-pass-inputs', renderer_frame=frame, capture=frame, width=4, height=2),
                dict(event='gi-pass', mode='separate', resident=True, world_serial=42, atlas_serial=42),
                dict(event='shader-binding', shader=0, **{'pass': 0}),
                dict(event='probe-radiance', request=10 + index, lighting_generation=2,
                     sun_direction=[0, 0, 1], sun_irradiance=[1, 1, 1], sky_radiance=[.5, .3, .2])]
        bank = bytearray(116*16)
        struct.pack_into('<f', bank, 24*16+12, index + .5)
        files = {'gi-constants.f32': bank, 'lighting-shader-0.bin': struct.pack('<2I', 0xffff0300, 0xffff)}
        for slot in (*parity.GI_PROBE_SLOTS, 11):
            base = f'lighting-input-g5-s{slot}'
            bound = slot != 11
            rows.append(dict(event='input-binding', slot=slot, group=5, snapshot_group=5, null=not bound,
                             file_base=base if bound else ''))
            if bound:
                fmt, stride = (113, 8) if slot == 13 else (116, 16)
                value = index if slot == 4 else 0
                files[base+'.fgr'] = struct.pack('<4s4I', b'FGR1', 2, 1, fmt, stride) + bytes(stride*2-1) + bytes([value])
        for name, data in files.items():
            (folder / f'capture-100-{name}').write_bytes(data)
            rows.append(dict(event='file', name=name, bytes=len(data), saved=True))
        rows.append(dict(event='end', render_completed=True, export_complete=True))
        (folder / 'capture-100-frame-parity-gi-inputs.jsonl').write_text('\n'.join(map(json.dumps, rows)), encoding='utf-8')


def check_gi(root):
    folders = tuple(root / label for label in ('reference', 'candidate'))
    frame, size = 100, (4, 2)
    write_gi(folders)
    saved = [parity.gi_inputs(folder, size, (frame,)) for folder in folders]
    result = parity.compare_gi(folders, size, (frame,), saved)['100']
    assert result['pass_differences'] == {}
    assert result['probe_radiance_differences'] == {'request': [10, 11]}
    assert result['inputs']['constants']['changed_registers'] == [24]
    assert result['inputs']['s4'] == {'match': False, 'same_layout': True, 'changed_pixels': 1, 'first_changed_xy': [1, 0]}
    assert result['inputs']['s11'] == {'match': True, 'bound': [False, False]}
    path = folders[1] / 'capture-100-lighting-input-g5-s4.fgr'
    path.write_bytes((folders[0] / path.name).read_bytes())
    rejected(lambda: parity.compare_gi(folders, size, (frame,), saved))
    path.write_bytes(path.read_bytes()[:-1])
    rejected(lambda: parity.gi_inputs(folders[1], size, (frame,)))


def check_live_lighting(root):
    folders = (root / 'live', root / 'replay')
    size, frames = (4, 2), (100,)
    write_history(folders)
    write_directional(folders)
    write_gi(folders)
    live, stand = (parity.lighting_evidence(folder, size, frames) for folder in folders)
    # Extra replay-only predecessors never substitute for a captured Present.
    for products in stand.values():
        products['99'] = products['100']
    compared = parity.compare_live_lighting(folders, size, frames, live, stand)
    assert compared['history']['100']['history-raw-light']['changed_pixels'] == 1
    assert compared['directional']['100']['anchor_differences']['pivot'][1] == [0, 0, 1]
    assert compared['gi']['100']['inputs']['s4']['changed_pixels'] == 1
    rejected(lambda: parity.compare_live_lighting(folders, size, frames, dict(live, gi_inputs={}), stand))
    (folders[0] / 'capture-100-gi-constants.f32').write_bytes((folders[1] / 'capture-100-gi-constants.f32').read_bytes())
    rejected(lambda: parity.compare_live_lighting(folders, size, frames, live, stand))


def check_sampled_points(root):
    folders = (root / 'live', root / 'replay')
    size, frames = (4, 2), (100,)
    begin = dict(event='begin', scope='local-light-contribution-inputs', capture=100, renderer_frame=100, width=4, height=2)
    end = dict(event='end', render_completed=True, export_complete=True)
    plane = struct.pack('<4s4I', b'FGR1', 2, 1, 113, 8)
    for index, folder in enumerate(folders):
        folder.mkdir()
        source_id = str(42+index)
        roster = [begin, dict(event='selection', count=1), dict(event='source', slot=0, source_id=source_id,
                  position=[0, 1, 2], rgb=[.5, .5, .5], range=[1, 12]), end]
        (folder / 'capture-100-frame-parity-lights.jsonl').write_text('\n'.join(map(json.dumps, roster)), encoding='utf-8')
        rows = [begin, dict(event='point-input-stage', active=True), dict(event='point-correction', slot=1,
                source_id=source_id, cube_source_id=source_id, complete=True, budget_held=bool(index),
                soft=True, age_ms=index, scissor=[0, 0, 2, 1])]
        products = {'local-before-point.fgr': plane + bytes(16), 'local-after-point.fgr': plane + struct.pack('<2Q', 0, index),
                    'sampled-point-1-bank.f32': bytes(14*16)}
        products.update({f'sampled-point-1-face-{face}.fgr': points.FACE_HEADER + struct.pack('<I', index if face == 3 else 0)
                         + bytes((256*256-1)*4) for face in range(6)})
        for name, data in products.items():
            (folder / f'capture-100-{name}').write_bytes(data)
            rows.append(dict(event='file', name=name, bytes=len(data), saved=True))
        (folder / 'capture-100-point-light-inputs.jsonl').write_text('\n'.join(map(json.dumps, rows + [end])), encoding='utf-8')
    saved = [points.sampled_products(folder, size, frames)[0] for folder in folders]
    result = points.compare_sampled(folders, size, frames, saved)['100']
    assert result['order_matches'] and result['comparable_sources']['0'] == {
        'source_match': True, 'bank_match': True, 'different_faces': [3], 'state_differences': {'budget_held': [False, True]}}
    assert result['planes']['before-point']['changed_pixels'] == 0 and result['planes']['after-point']['changed_pixels'] == 1
    file = folders[1] / 'capture-100-sampled-point-1-face-3.fgr'
    file.write_bytes((folders[0] / file.name).read_bytes())
    rejected(lambda: points.compare_sampled(folders, size, frames, saved))
    file.unlink()
    rejected(lambda: points.sampled_products(folders[1], size, frames))


def check_local_light_inputs():
    def row(frame, selected='01'):
        return (f'LOCAL_INPUTS rendererFrame={frame} worldFrame=3 ready=1 map=Test Map clock=1000 dt=0 continuous=0 '
                'trigger=2 draw=10 view=11 authored=12 native=13 nativeCount=1 nativeTime=345 banks=14 bankCount=2 '
                f'drawLights=15 decoded=2 equipment=16 equipmentCount=2 particles=17 immediate=18 selected={selected} selectedCount=2')

    live = [row(2), row(3), row(4), row(4), row(5)]
    stand = [row(3, '02'), row(4), row(4, '02'), row(6)]
    comparison = parity.compare_local_light_inputs(live, stand, [3, 4, 5])
    assert comparison == dict(shared_presents=2, live_only=[2, 5], stand_only=[], certified_presents_covered=False,
                              differences={'selected': dict(frames=2, first_present=3, live=['01'], stand=['02'])})
    assert parity.compare_local_light_inputs(live, [], [3]) is None
    rejected(lambda: parity.compare_local_light_inputs(live, ['LOCAL_INPUTS rendererFrame=3'], [3]))
    scene = [row(1).replace('Test Map', 'Menu:123'), row(2).replace('Test Map', '-'), *live[1:]]
    assert parity.observed_world_scene_start(scene, 5, 0) == 3
    assert parity.observed_world_scene_start(scene, 5, 4) == 4
    assert parity.observed_world_scene_start([], 5, 0) is None
    rejected(lambda: parity.observed_world_scene_start(scene, 2, 0))


def check_world_normals(root):
    live, stand = (root / name for name in ('live', 'stand'))
    frame, size = 290, (4, 4)
    row = dict(product='normals', renderer_frame=frame, state='current', saved=True,
               zero_filled=False, fps_eligible=False)
    header = struct.pack('<4s4I', b'FGR1', 2, 2, 113, 8)
    original = header + b'\x01' * 32
    journal = f'capture-{frame}-products.jsonl'
    image = f'capture-{frame}-normals.fgr'
    for folder in (live, stand):
        folder.mkdir()
        (folder / journal).write_text(json.dumps(row) + '\n', encoding='utf-8')
        (folder / image).write_bytes(original)
    frozen = {str(frame): runner.digest(live / image)}

    def compare():
        return parity.compare_world_normals(live, stand, size, [frame], frozen)

    assert compare()['match']
    (stand / image).write_bytes(original[:-1] + b'\x02')
    changed = compare()
    assert not changed['match']
    assert changed['frames'][str(frame)]['changed_pixels'] == 1
    assert changed['frames'][str(frame)]['first_changed_xy'] == [1, 1]
    (live / image).write_bytes(original[:-1] + b'\x02')
    rejected(compare)  # Matching planes still cannot replace the frozen capture.
    (live / image).write_bytes(original)
    (stand / journal).write_text(json.dumps(dict(row, state='skipped', zero_filled=True)) + '\n', encoding='utf-8')
    rejected(compare)
    (stand / journal).write_text(json.dumps(row) + '\n', encoding='utf-8')
    (stand / image).write_bytes(header + bytes(32))
    rejected(compare)
    (stand / image).unlink()
    rejected(compare)


def main():
    check_local_light_inputs()
    static_row = ('STATIC_PARITY rendererFrame=120 upload=1 placements=40 cpuModels=12 '
                  'ready=6 pending=6 bytes=65536 content=0011223344556677')
    assert parity.static_shadow_stream([static_row], [120]) == {
        '120': [dict(upload=1, placements=40, cpu_models=12, ready=6, pending=6, bytes=65536, content='0011223344556677')]}
    bounds_row = ('SHADOW_BOUNDS_PARITY rendererFrame=120 packets=50 valid=45 operations=800 vertices=20 '
                  'cascades=30 actors=10 content=0011223344556677')
    assert parity.shadow_bounds_work([bounds_row], [120]) == {
        '120': dict(packets=50, valid=45, operations=800, vertices=20, cascades=30, actors=10, content='0011223344556677')}
    rejected(lambda: parity.shadow_bounds_work([bounds_row], [121]))
    rejected(lambda: parity.shadow_bounds_work([bounds_row, bounds_row], [120]))
    rejected(lambda: parity.shadow_bounds_work([bounds_row.replace('valid=45', 'valid=55')], [120]))
    cold = bounds_row.replace('rendererFrame=120', 'rendererFrame=119').replace(
        'valid=45 operations=800', 'valid=0 operations=0')
    timeline = parity.compare_shadow_bounds_timeline(
        [cold, bounds_row], [bounds_row.replace('valid=45', 'valid=40'), cold])
    assert timeline['shared_presents'] == 2 and timeline['differing_presents'] == 1
    assert timeline['first_difference']['present'] == 120
    assert timeline['first_difference']['stand']['valid'] == 40
    actor_row = ('SHADOW_ACTOR_PARITY rendererFrame=120 source=0 matrix=0123456789abcdef '
                 'packets=1234567890abcdef count=19 held=2 carried=1')
    live_actor = parity.actor_shadow_work([actor_row])
    stand_actor = parity.actor_shadow_work([actor_row.replace('carried=1', 'carried=3')])
    routing = parity.compare_present_work(live_actor, stand_actor)
    assert routing['first_difference']['live']['0']['carried'] == 1
    assert routing['first_difference']['stand']['0']['carried'] == 3
    gi_capture = 'GI_ACTOR_CAPTURE present=120 actor=9 packets=13 vertices=113 menu=0'
    gi_request = 'GI_ACTOR_REQUEST present=121 actor=9 request=12 base=7 menu=0'
    gi_atlas = ('GI_ATLAS_PRESENT present=124 serial=5 request=12 actor=9 generation=2 '
                'processed=5600 dynamicSolved=128 coarse=1 partial=0')
    live_gi = [gi_capture, gi_request, gi_atlas]
    stand_gi = [gi_capture.replace('packets=13', 'packets=12'), gi_request.replace('present=121', 'present=122')]
    gi_work = parity.compare_gi_timeline(live_gi, stand_gi, 120, 123)
    assert gi_work['GI_ACTOR_CAPTURE']['first_difference']['stand'][0]['packets'] == 12
    assert gi_work['GI_ACTOR_REQUEST']['live_only'] == [121]
    assert gi_work['GI_ACTOR_REQUEST']['stand_only'] == [122]
    assert 'GI_ATLAS_PRESENT' not in gi_work
    assert parity.compare_gi_timeline([], [], 120, 124) is None
    rejected(lambda: parity.gi_timeline([gi_atlas.replace('coarse=1', 'coarse=')], 120, 124))
    policy = 'GI_PARITY_POLICY completed-near-and-coarse'
    assert parity.fixed_gi_publication([policy, *live_gi], 124) == {'uploads': 1, 'last_upload_present': 124}
    rejected(lambda: parity.fixed_gi_publication(live_gi, 124))
    rejected(lambda: parity.fixed_gi_publication([policy, *live_gi], 123))
    rejected(lambda: parity.fixed_gi_publication([policy, gi_atlas.replace('partial=0', 'partial=1')], 124))
    rejected(lambda: parity.fixed_gi_publication([policy, gi_atlas, 'GI_PARITY_FAILURE present=120 stage=cpu-timeout'], 124))
    quality = 'QUALITY preset=0 GpuBudgetMs=0(file) ActorShadowCpuMs=3(default)'
    runner.verify_fixed_gpu_budget([quality])
    rejected(lambda: runner.verify_fixed_gpu_budget([]))
    rejected(lambda: runner.verify_fixed_gpu_budget([quality.replace('0(file)', '4(default)')]))
    rejected(lambda: runner.verify_fixed_gpu_budget([quality, 'GPUBUDGET level 0 -> 1 smoothed=4.1 budget=4 changes=1']))
    rejected(lambda: parity.actor_shadow_work([actor_row, actor_row]))
    rejected(lambda: parity.static_shadow_stream([static_row], [121]))
    rejected(lambda: parity.static_shadow_stream([static_row.replace('upload=1', 'upload=0')], [120]))
    rejected(lambda: parity.static_shadow_stream([static_row, 'STATIC_PARITY_FAILURE rendererFrame=119 stage=cpu-timeout'], [120]))
    with TemporaryDirectory(dir=fp.output_dir()) as temporary:
        folder = Path(temporary)
        names = ('shadow-near', 'shadow-far')
        rows = [dict(product=name, renderer_frame=120, state='current', saved=True,
                     zero_filled=False, fps_eligible=False) for name in names]
        (folder / 'capture-120-products.jsonl').write_text('\n'.join(map(json.dumps, rows)), encoding='utf-8')
        image = struct.pack('<4s4I', b'FGR1', 1024, 1024, 114, 4) + bytes(1024**2 * 4)
        for name in names:
            (folder / f'capture-120-{name}.fgr').write_bytes(image)
        hashes = parity.directional_depth_hashes(folder, [120])
        assert all(row['changed_pixels'] == 0 for row in parity.compare_directional_depth(folder, folder, [120], hashes)['120'].values())
        stand = folder / 'stand'
        stand.mkdir()
        rows[0].update(state='skipped', zero_filled=True)
        (stand / 'capture-120-products.jsonl').write_text('\n'.join(map(json.dumps, rows)), encoding='utf-8')
        for name in names:
            (stand / f'capture-120-{name}.fgr').write_bytes(image)
        skipped = parity.directional_depth_hashes(stand, [120], allow_skipped=True)
        assert skipped['120']['shadow-near'] is None
        assert parity.compare_directional_depth(stand, stand, [120], skipped, allow_skipped=True)['120']['shadow-near']['state'] == 'skipped'
        assert parity.compare_directional_depth(folder, stand, [120], hashes, allow_skipped=True)['120']['shadow-near']['state'] == 'different-use'
        rejected(lambda: parity.directional_depth_hashes(stand, [120]))
        (stand / 'capture-120-shadow-near.fgr').write_bytes(image[:-1] + b'\1')
        rejected(lambda: parity.directional_depth_hashes(stand, [120], allow_skipped=True))
        (folder / 'capture-120-shadow-near.fgr').write_bytes(image[:-1] + b'\1')
        rejected(lambda: parity.compare_directional_depth(folder, folder, [120], hashes))
    line = 'FRAME_PARITY present=100 saved=1 depth_samples=2 depth_quality=0'
    assert parity.selected_frames([line]) == {100: 0}
    rejected(lambda: parity.selected_frames([line, line]))
    rejected(lambda: parity.selected_frames([line.replace('=2 ', '=1 ')]))
    discard = 'FRAME_PARITY_DISCARD first=100'
    later = [line.replace('100', str(frame)) for frame in (150, 158)]
    discarded = set()
    assert parity.selected_frames([line, discard, *later], discarded) == {150: 0, 158: 0}
    assert discarded == {100}
    rejected(lambda: parity.selected_frames([discard]))
    rejected(lambda: parity.selected_frames([line, discard, discard]))
    rejected(lambda: parity.selected_frames([line, discard.replace('100', '99')]))
    rejected(lambda: parity.selected_frames([line, discard, line]))
    active = 'CSTREAM active gameTid=1 frameSkip=0 occlusionLocal=1'
    assert parity.stream_without_skips([active])
    assert not parity.stream_without_skips([active, active])
    assert not parity.stream_without_skips([active.replace('frameSkip=0', 'frameSkip=1')])
    with TemporaryDirectory(dir=fp.output_dir()) as temporary:
        live, stand = (Path(temporary) / name for name in ('live', 'stand'))
        live.mkdir()
        stand.mkdir()
        assert parity.hdr_boundary_images(live, 100, (2, 1)) == {}
        for folder, last in ((live, 1), (stand, 2)):
            header = struct.pack('<4s4I', b'FGR1', 2, 1, 113, 8)
            (folder / 'capture-100-hdr-after-world.fgr').write_bytes(header + struct.pack('<2Q', 0, 1))
            rejected(lambda: parity.hdr_boundary_images(folder, 100, (2, 1)))
            (folder / 'capture-100-hdr-before-finish.fgr').write_bytes(header + struct.pack('<2Q', 0, last))
        frozen = {name: sha for name, (sha, _) in parity.hdr_boundary_images(live, 100, (2, 1)).items()}
        boundaries = parity.compare_hdr_boundaries((live, stand), 100, (2, 1), frozen)
        assert boundaries['hdr-after-world']['changed_pixels'] == 0
        assert boundaries['hdr-before-finish']['first_changed_xy'] == [1, 0]
        write_capture(live, 100)
        write_capture(stand, 100)

        def compare():
            return parity.compare(live, stand, (2, 1), {100: 0}, {'100': 2811})

        assert compare()['match']
        write_capture(stand, 100, depth=0x3f000001)
        report = compare()
        assert not report['match']
        assert report['plane_match'] == {'color': True, 'depth': False}
        frame = report['frames'][0]
        assert frame['trace_call'] == 2811 and frame['color']['changed_pixels'] == 0
        assert frame['depth']['changed_pixels'] == 1 and frame['depth']['first_changed_xy'] == [1, 0]
        assert frame['native_color']['state'] == 'unavailable'
        write_capture(live, 100, native=(0x00020304, 0x05060708))
        write_capture(stand, 100, depth=0x3f000001, color=0x3c00000000000001,
                      native=(0xff020304, 0x05060709))
        scene = compare()['frames'][0]['native_color']
        assert scene['state'] == 'compared' and scene['live']['source_samples'] == 2
        assert scene['changed_storage_pixels'] == 2 and scene['changed_pixels'] == 1
        assert scene['changed_bounds_xyxy'] == [1, 0, 1, 0]
        assert scene['overlap_with_depth'] == scene['overlap_with_presented_color'] == 1
        write_capture(stand, 100, native=(0xff020304, 0x05060709))
        assert compare()['match'] and compare()['frames'][0]['native_color']['changed_pixels'] == 1
        (stand / 'capture-100-native-color.fgr').write_bytes(b'FGR1')
        rejected(compare)
        write_capture(stand, 100, depth=0)
        rejected(compare)
        write_capture(stand, 100, samples=1)
        rejected(compare)
        write_capture(stand, 100)
        (stand / 'capture-100-color.fgr').write_bytes(b'FGR1')
        rejected(compare)
        for name, size in (('model-normals', 4), ('normals', 2)):
            header = struct.pack('<4s4I', b'FGR1', size, size, 113, 8)
            path = stand / f'capture-100-{name}.fgr'
            path.write_bytes(header + b'\x01' * (size*size*8))
        products = parity.normal_hashes(stand, (4, 4), {100: 0})
        assert products['100']['model-normals'] == runner.digest(stand / 'capture-100-model-normals.fgr')
        path.write_bytes(header + bytes(size*size*8))
        rejected(lambda: parity.normal_hashes(stand, (4, 4), {100: 0}))
    with TemporaryDirectory(dir=fp.output_dir()) as temporary:
        compare_replays(Path(temporary))
    with TemporaryDirectory(dir=fp.output_dir()) as temporary:
        check_composition(Path(temporary))
    with TemporaryDirectory(dir=fp.output_dir()) as temporary:
        check_history(Path(temporary))
    with TemporaryDirectory(dir=fp.output_dir()) as temporary:
        check_directional(Path(temporary))
    with TemporaryDirectory(dir=fp.output_dir()) as temporary:
        check_gi(Path(temporary))
    with TemporaryDirectory(dir=fp.output_dir()) as temporary:
        check_live_lighting(Path(temporary))
    with TemporaryDirectory(dir=fp.output_dir()) as temporary:
        check_sampled_points(Path(temporary))
    with TemporaryDirectory(dir=fp.output_dir()) as temporary:
        check_world_normals(Path(temporary))
    print('PASS exact 2x MSAA parity, changed-pixel localization and evidence rejection')


if __name__ == '__main__':
    check_trace_probe()
    main()
