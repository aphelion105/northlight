#!/usr/bin/env python3
# northlight-test:
"""Protect the loopback fixture's packet layout and warm baseline admission."""
from argparse import Namespace
from io import BytesIO
import json
import struct
from hashlib import sha256
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tempfile import TemporaryDirectory
from unittest.mock import Mock, patch
import northlight_paths as fp

sys.path.insert(0, str(fp.SCRIPTS))
import northrend_fixture_server as server
import northrend_live_ab as live_ab
import run_northrend_baseline as runner
from run_northrend_baseline import graphics_profile, renderer_profile, scan, warm_windows


def packets():
    data = server.character_list()
    assert data[:9] == struct.pack('<BQ', 1, 1)
    assert data[9:20] == b'Northlight\0'
    assert struct.unpack_from('<II3f', data, 29)[:2] == (65, 571)
    assert len(data) == 281
    update = server.player_create()
    assert update[:10] == bytes.fromhex('01000000030101042100')
    assert struct.unpack_from('<4f', update, 20) == struct.unpack('<4f', struct.pack('<4f', *server.SPAWN))
    assert update[76] == 3  # Update-mask block count follows the header and movement speeds.
    assert len(update) == 77 + 12 + 13 * 4
    key = bytes(range(40))
    initial = server.cipher(key, server.SERVER_KEY)
    # Independent known answer for ARC4-drop1024 with HMAC-SHA1 header key.
    assert initial(bytes(4)).hex() == 'da4770c4'
    packed, speed, holiday = struct.unpack('<IfI', server.login_clock())
    assert ((packed >> 6) & 31) * 60 + (packed & 63) == server.WORLD_MINUTE == 720
    assert speed == holiday == 0
    requests = BytesIO(b'\x10\0\0\0\0\x10\x01\x02\x03\x04')
    connection = Mock()
    with patch('builtins.print'):
        server.send_realm_lists(requests, connection, 8085)
    a, b = [call.args[0] for call in connection.sendall.call_args_list]
    assert a == b and a[0] == 0x10 and b'127.0.0.1:8085\0' in a
    with patch.object(runner, 'login_input'), patch.object(runner, 'press_key') as enter:
        actions = {}
        runner.advance_login(1, ['LOGIN_OK'], actions, 0, 4)
        runner.advance_login(1, ['LOGIN_OK', 'REALM_LIST_SENT'], actions, 0, 5)
        enter.assert_not_called()
        runner.advance_login(1, ['LOGIN_OK', 'REALM_LIST_SENT'], actions, 0, 6)
        enter.assert_called_once_with(1, 0x0d)


def crowd_packets():
    def field_values(packet, offset):
        blocks = packet[offset]
        mask = int.from_bytes(packet[offset + 1:offset + 1 + 4 * blocks], 'little')
        offset += 1 + 4 * blocks
        fields = {}
        for field in range(32 * blocks):
            if mask & (1 << field):
                fields[field], = struct.unpack_from('<I', packet, offset)
                offset += 4
        return fields, offset

    assert len(server.crowd_roster()) == 128
    for size, batches in ((50, [32, 18]), (100, [32, 32, 32, 4]), (128, [32, 32, 32, 32])):
        roster = server.crowd_roster(size)
        packets = server.crowd_packets(roster)
        assert [struct.unpack_from('<I', packet)[0] for packet in packets] == batches
        assert len({row[0] for row in roster.values()}) == size
        assert len({row[1] for row in roster.values()}) == size
        assert len({row[2][4] for row in roster.values()}) == 8
        assert len({row[3] for row in roster.values()}) == 8
        players = iter(roster.items())
        for packet, count in zip(packets, batches):
            assert len(packet) + 2 < 8192
            offset = 4
            for _ in range(count):
                guid, (_, position, appearance, outfit) = next(players)
                assert struct.unpack_from('<BBBBH', packet, offset) == (3, 1, guid, 4, 0x20)
                assert struct.unpack_from('<4f', packet, offset + 16) == struct.unpack('<4f', struct.pack('<4f', *position))
                fields, offset = field_values(packet, offset + 72)
                assert fields[0] == guid and fields[0x7A] == 1
                assert fields[0x43] == appearance[4] and fields[0x44] == appearance[2]
                assert fields[0x43] != fields[0x44]
                assert fields[0x139] == outfit[0] and fields.get(0x13B, 0) == outfit[1] and fields[0x13A] == outfit[2]
            assert offset == len(packet)
        assert next(players, None) is None
    payload = b''.join(packets)
    spawn = f'CROWD_SPAWN players=128 outfits=8 sha256={sha256(payload).hexdigest()}'
    assert spawn == server.spawn_record(roster, payload)
    names = [f'CROWD_NAME guid={guid}' for guid in roster]
    world = 'WORLD probe atlas ready map=Northrend frame=130 tick=10 staticMesh=0'
    lamps = [world, 'LOCAL_SOURCE frame=220 nativePublished=32 particleAccepted=9 particlePublished=8']
    crowd = runner.observe_crowd(lamps, [spawn, *names, names[0]], roster, spawn)
    assert crowd['ready'] and crowd['client_name_requests'] == 128
    assert crowd['peak_lights'] == {'native_published': 32, 'particle_accepted': 9, 'particle_published': 8}
    assert not runner.observe_crowd(lamps, [spawn, *names[:-1]], roster, spawn)['ready']
    assert not runner.observe_crowd(lamps, names, roster, spawn)['ready']
    assert not runner.observe_crowd([], [spawn, *names], roster, spawn)['ready']
    diagnostic_lamps = ['EQUIPMENT lights nativePoint=1 active=48 output=32',
                        'PARTICLELIGHT probes=128 draws=16 accepted=9 output=8']
    observed = runner.observe_crowd([world, *diagnostic_lamps], [spawn, *names], roster, spawn)
    assert observed['ready'] and observed['peak_lights'] == crowd['peak_lights']
    assert not runner.observe_crowd([*diagnostic_lamps, world], [spawn, *names], roster, spawn)['ready']


def clock():
    def packet(day):
        record = struct.pack('<QII', 0xd38b00, 0x388, 1) + struct.pack('<If', 0, day) + bytes(0x380)
        return bytes(24) + struct.pack('<I', 2) + bytes(12) + record * 2

    rows = [{'frame': n, 'record': n + 10, 'trigger': 2} for n in range(3)]
    frames = {0: 0, 2: 0}
    fixed = [packet(.5)] * 3
    proof = runner.world_clock(rows, fixed, frames, 720)
    assert proof == {'minute_of_day': 720, 'samples': 3, 'frames': {
        '0': {'record': 10, 'day_fraction': .5}, '2': {'record': 12, 'day_fraction': .5}}}
    assert runner.world_clock_window(rows, fixed, range(1, 2), 720) == {
        'minute_of_day': 720, 'samples': 1, 'frames': {'1': {'record': 11, 'day_fraction': .5}}}
    for packets, journal, error in (([fixed[0], packet(.5001), fixed[2]], rows, 'not fixed'),
                                   (fixed[:2], (rows[0], rows[2]), 'missing')):
        try:
            runner.world_clock(journal, packets, frames, 720)
        except ValueError as problem:
            assert error in str(problem)
        else:
            raise AssertionError('Invalid clock evidence was accepted')

    frames = {282: 0, 290: 0}
    clock_lines = ['FRAME_PARITY_CLOCK present=282 hz=60 tick=5700',
                   'FRAME_PARITY_CLOCK present=290 hz=60 tick=5833']
    assert runner.visual_clock(clock_lines, frames) == {'hz': 60, 'tick_ms': {'282': 5700, '290': 5833}}
    discarded = ['FRAME_PARITY_CLOCK present=240 hz=60 tick=5000',
                 'FRAME_PARITY present=240 saved=1 depth_samples=2 depth_quality=0',
                 'FRAME_PARITY_DISCARD first=240']
    assert runner.visual_clock(discarded + clock_lines, frames) == runner.visual_clock(clock_lines, frames)
    for invalid in (clock_lines[:1], clock_lines + clock_lines[:1],
                    [clock_lines[0], clock_lines[1].replace('5833', '5834')],
                    [clock_lines[0], clock_lines[1].replace('hz=60', 'hz=30')],
                    discarded[1:] + clock_lines):
        try:
            runner.visual_clock(invalid, frames)
        except ValueError:
            pass
        else:
            raise AssertionError('Invalid visual clock evidence was accepted')


def intervals():
    def window(tick, **updates):
        fields = dict(tick=tick, samples=120, meanMs=10.0, enabled=1, focus=1)
        fields.update(updates)
        return 'FRAME interval ' + ' '.join(f'{k}={v}' for k, v in fields.items())

    lines = ['WORLD geometry transition tick=100 ready=1',
             'WORLD camera tick=400 rendered=10 eye=(5846.67 604.13 659.00) forward=(1 0 0)',
             'WORLD probe atlas ready map=Northrend tick=500', 'WORLD geometry transition tick=1000 ready=1',
             window(4199), window(4200, focus=0), window(4300, enabled=0),
             window(4400, complete=0), window(8000), window(9500)]
    assert warm_windows(scan(lines[:2]), 2) == []
    state = scan(lines)
    assert state['first_camera_tick'] == 400 and state['gi_tick'] == 500 and state['static_tick'] == 1000
    assert [row['tick'] for row in warm_windows(state, 2)] == ['8000', '9500']
    assert warm_windows(state, 0) == []
    assert runner.frame_timing.summarize(warm_windows(state, 2)) == {'state': 'unavailable'}
    closed_log = lines + ['WORLD geometry transition tick=9700 ready=0',
                          'WORLD geometry transition tick=9900 ready=1']
    assert warm_windows(scan(closed_log), 2) == []
    assert warm_windows(scan(closed_log[:len(lines)]), 2) == warm_windows(state, 2)
    arguments = ['fixture', 'run', '--stage', 'stage', '--output', 'stage/smoke', '--windows', '0']
    with patch.object(sys, 'argv', arguments), patch.object(runner, 'run_client', return_value=0) as run:
        assert runner.main() == 0 and run.call_args.args[0].windows == 0
    assert not runner.parity_settled(state)
    camera = lines[1].replace('tick=400', 'tick=2800')
    assert runner.parity_settled(scan(lines + [camera]))
    assert not runner.parity_settled(scan(lines + [camera, 'WORLD geometry transition tick=2800 ready=0']))


def resize_checkpoints():
    def begin(frame, before, after):
        return (f'CSTREAM RESET begin frame={frame} caller=Wow.exe+0x1 previous={before}/22 '
                f'windowed=1 msaa=2/0 requested={after}/22 windowed=1 msaa=2/0')

    def end(frame):
        return f'CSTREAM RESET end frame={frame} ran=1 result=0x00000000 coop=0x00000000'

    full, reduced = (3840, 2160), (3680, 2070)
    lines = [begin(7, '3840x2160', '3680x2070'),
             'CSTREAM RESET end frame=7 ran=0 result=0x8876086c coop=0x00000000',
             begin(8, '3840x2160', '3680x2070'), end(8),
             begin(16, '3680x2070', '3840x2160'), end(16)]
    shrink = runner.size_reset(lines, 0, full, reduced)
    restore = runner.size_reset(lines, shrink['epoch'], reduced, full)
    steps = [shrink, restore]
    assert (shrink['present'], restore['present']) == (8, 16)
    assert runner.resize_evidence(lines, (25, 33), steps) == {
        'steps': steps, 'selected_epoch': 2, 'epoch_first_present': 16}
    same_size = lines + [begin(24, '3840x2160', '3840x2160'), end(24)]
    assert runner.resize_evidence(same_size, (25, 33), steps) == {
        'steps': steps, 'selected_epoch': 3, 'epoch_first_present': 24}
    for invalid in ((lines, (16, 24)), (lines, (12, 20)), (lines + [end(24)], (25, 33)),
                    (lines[:-1] + [end(17)], (25, 33))):
        try:
            runner.resize_evidence(*invalid, steps)
        except ValueError:
            pass
        else:
            raise AssertionError('Accepted an unverified resize epoch')


def replay_geometry_checkpoint():
    menu = 'WORLD geometry transition frame=0 rendererFrame=112 tick=100 ready=1 screen=1 mesh=1 targets=1'
    cold = 'WORLD geometry transition frame=1 rendererFrame=125 tick=200 ready=0 screen=1 mesh=0 targets=0'
    ready = 'WORLD geometry transition frame=116 rendererFrame=240 tick=300 ready=1 screen=1 mesh=1 targets=1'
    frames = (282, 290)
    assert runner.observed_geometry_start([menu, cold, ready], frames) == 240
    assert runner.observed_geometry_start([cold, ready], frames) == 240
    reset = 'CSTREAM RESET end frame=250 ran=1 result=0x00000000 coop=0x00000000'
    next_cold, next_ready = cold.replace('125', '250'), ready.replace('240', '270')
    post_reset = [menu, cold, ready, reset, next_cold, next_ready]
    epoch, epoch_first = runner.observed_reset_epoch(post_reset, frames)
    assert (epoch, epoch_first) == (1, 250)
    assert runner.observed_geometry_start(post_reset, frames, epoch_first) == 270
    assert runner.observed_reset_epoch(post_reset, (242, 249)) == (0, 0)
    timeline = runner.observed_world_checkpoints(post_reset, frames, False)
    assert timeline == [dict(epoch=0, first=0, end=250, cold=125, scene=None, geometry=240),
                        dict(epoch=1, first=250, end=0xffffffff, cold=250, scene=None, geometry=270)]
    inputs = ' '.join(f'{field}=0' for field in runner.parity.LOCAL_LIGHT_INPUT_FIELDS if field != 'map')
    scenes = [f'LOCAL_INPUTS rendererFrame={frame} map=Northrend {inputs}' for frame in (140, 253)]
    markers = [f'REPLAY_GEOMETRY_CHECKPOINT present={frame} ready=1' for frame in (240, 270)]
    modern_replay = post_reset + scenes + markers
    runner.verify_world_checkpoints(modern_replay, frames, timeline)
    audited_timeline = [timeline[0], dict(timeline[1], scene=253)]
    runner.verify_world_checkpoints(modern_replay, frames, audited_timeline)
    # Geometry can stay cold across a short Reset epoch. Its first World
    # audit certifies readiness there even when the transition logger is idle.
    short_reset = reset.replace('250', '252')
    earlier_audit = scenes[1].replace('253', '250')
    resumed = [menu, cold, reset, earlier_audit, short_reset, scenes[1], next_ready]
    assert runner.observed_world_checkpoints(resumed, frames, True) == [
        dict(epoch=0, first=0, end=250, cold=125, scene=None, geometry=None),
        dict(epoch=1, first=250, end=252, cold=250, scene=250, geometry=None),
        dict(epoch=2, first=252, end=0xffffffff, cold=253, scene=253, geometry=270),
    ]
    no_scene_audit = [menu, cold, reset, short_reset, next_ready]
    fixed_profile_checkpoints = [
        dict(epoch=0, first=0, end=250, cold=125, scene=None, geometry=None),
        dict(epoch=1, first=250, end=252, cold=250, scene=None, geometry=None),
        dict(epoch=2, first=252, end=0xffffffff, cold=252, scene=None, geometry=270),
    ]
    assert runner.observed_world_checkpoints(no_scene_audit, frames, False, carry_cold=True) == fixed_profile_checkpoints
    runner.verify_world_checkpoints(no_scene_audit + markers[1:], frames, fixed_profile_checkpoints, carry_cold=True)
    try:
        runner.observed_world_checkpoints([menu, cold, ready, reset, next_ready], frames, False, carry_cold=True)
    except ValueError:
        pass
    else:
        raise AssertionError('Fixed profiling inferred a cold epoch after a ready edge')
    try:
        runner.observed_world_checkpoints([line for line in resumed if line != scenes[1]], frames, True)
    except ValueError:
        pass
    else:
        raise AssertionError('Accepted a cold Reset epoch without matching transition or audit evidence')
    for wrong in (post_reset + scenes + markers[:1], post_reset + scenes[:1] + markers,
                  post_reset + scenes[:1] + [scenes[1].replace('rendererFrame=253', 'rendererFrame=254')] + markers):
        try:
            runner.verify_world_checkpoints(wrong, frames, audited_timeline)
        except ValueError:
            pass
        else:
            raise AssertionError('Accepted mismatched geometry or certified scene start')
    assert runner.observed_world_checkpoints([menu, cold, reset, next_cold, next_ready], frames, False)[0] == dict(
        epoch=0, first=0, end=250, cold=125, scene=None, geometry=None)
    startup_reset = reset.replace('frame=250', 'frame=0')
    assert runner.observed_world_checkpoints([startup_reset, menu, cold, ready], frames, False) == [
        dict(epoch=1, first=0, end=0xffffffff, cold=125, scene=None, geometry=240)]
    joined = {'world_checkpoints': {'240': dict(trace_call=411, epoch=0, epoch_first_present=0),
                                    '270': dict(trace_call=512, epoch=1, epoch_first_present=250)}}
    bound = runner.certify_world_checkpoints(timeline, joined, 'frozen')
    assert [point['trace_calls'] for point in bound] == [{'geometry': 411}, {'geometry': 512}]
    joined['world_checkpoints']['240']['epoch'] = 1
    try:
        runner.certify_world_checkpoints(timeline, joined, 'frozen')
    except ValueError:
        pass
    else:
        raise AssertionError('Accepted a World checkpoint in the wrong Reset epoch')
    try:
        runner.observed_reset_epoch(post_reset, (249, 252))
    except ValueError:
        pass
    else:
        raise AssertionError('Accepted parity frames across a Reset')
    for invalid in ([menu, ready], [cold, ready, ready], [ready, cold], [cold, ready[:-1]],
                    [cold, ready.replace('mesh=1', 'mesh=0')], [cold, ready.replace('240', '282')],
                    [menu, cold, ready, cold.replace('125', '287')],
                    [cold, ready, cold.replace('125', '250'), ready.replace('240', '281')]):
        try:
            runner.observed_geometry_start(invalid, frames)
        except ValueError:
            pass
        else:
            raise AssertionError('Accepted an invalid World geometry checkpoint')


def captured_shader_bundle():
    with TemporaryDirectory(dir=fp.output_dir()) as folder:
        stage = Path(folder).resolve()
        capture = stage / 'capture'
        (capture / 'downstream').mkdir(parents=True)
        bundle = capture / 'downstream/northlight-assets.bin'
        bundle.write_bytes(b'original bundle')
        asset_hash = runner.digest(bundle)
        trace = capture / 'wow.trace'
        trace.write_bytes(b'capture fixture')
        baseline = stage / 'baseline.json'
        runner.write_json(baseline, {})
        runner.write_json(capture / 'validation.json', {
            'mode': 'capture', 'status': 'PASS', 'sidecars': ['fixture'],
            'baseline_sha256': runner.digest(baseline), 'trace': {'sha256': runner.digest(trace)},
            'runtime_inputs': {'northlight-assets.bin': asset_hash}, 'capture_shader_assets_sha256': asset_hash,
        })
        bundle.write_bytes(b'changed bundle')
        try:
            runner.replay(Namespace(capture=capture, output=stage / 'replay'))
        except ValueError as error:
            assert 'captured live shader bundle changed' in str(error)
        else:
            raise AssertionError('Accepted changed live shader assets')


def profile():
    assert all(runner.shared_asset(Path(name)) for name in ('Data/enGB/base-enGB.MPQ', 'world-cache/models/a.fgs'))
    assert not any(runner.shared_asset(Path(name)) for name in ('Data/enGB/realmlist.wtf', 'Wow.exe', 'WTF/Config.wtf'))
    incoming = ('SET realmList "example.test"\nSET accountName "personal"\nSET farclip "1277"\n'
                'SET Sound_EnableAllSound "1"\n')
    staged = graphics_profile(incoming, '3840x2160', 2)
    assert 'example.test' not in staged and 'personal' not in staged
    for setting in ('realmlist "127.0.0.1"', 'farclip "1277"', 'videooptionsversion "3"', 'showtutorials "0"',
                    'gxresolution "3840x2160"', 'gxmultisample "2"', 'gxmaximize "1"'):
        assert 'SET ' + setting + '\n' in staged
    assert 'SET gxmaximize "0"\n' in graphics_profile(incoming, '1920x1080', 1)
    assert staged.endswith('SET Sound_EnableAllSound "0"\n')
    endpoint = '127.0.0.1:49152'
    legacy = (b'SET gxVSync "0"\r\nset sound_enableallsound "1"\r\n'
              b'SET Realmlist "example.test"\r\nSET Sound_EnableMusic "1"')
    muted = runner.muted_client_config(legacy)
    assert muted == (b'SET gxVSync "0"\r\nSET Realmlist "example.test"\r\n'
                     b'SET Sound_EnableMusic "1"\r\nSET Sound_EnableAllSound "0"\r\n')
    assert runner.muted_client_config(muted) == muted
    assert runner.muted_client_config(b'SET Sound_EnableAllSound "1"\n') == b'SET Sound_EnableAllSound "0"\n'
    launch = runner.runtime_client_config(legacy, endpoint)
    assert launch == (b'SET gxVSync "0"\r\nSET Sound_EnableMusic "1"\r\n'
                      b'SET realmList "127.0.0.1:49152"\r\nSET Sound_EnableAllSound "0"\r\n')
    assert runner.runtime_client_config(launch, endpoint) == launch
    assert runner.realmlist_config(endpoint) == 'set realmlist 127.0.0.1:49152\nset patchlist 127.0.0.1\n'
    renderer = renderer_profile('''[Renderer]
Backend=native
BackendPath=custom.dll
DxvkEngineNameWorkaround=1
DxvkPresentWaitWorkaround=0
ExperimentalHDR=0
HDRLegacyExpansion=0.50
HDRWideBloom=1
HDRPipelineVersion=2
[HDR.ab12]
HDRPaperWhite=260
[Other]
HDRExposure=4
''')
    assert renderer == ('[renderer]\nBackend=dxvk\nBackendPath=backend_d3d9.dll\nExperimentalHDR=1\nSettingsOverlay=0\n'
                        'DxvkEngineNameWorkaround=1\nDxvkPresentWaitWorkaround=0\n'
                        'HDRLegacyExpansion=0.50\nHDRWideBloom=1\nHDRPipelineVersion=2\n'
                        '[hdr.ab12]\nHDRPaperWhite=260\n')


def end_to_end():
    identity = '1' * 64
    with TemporaryDirectory(dir=fp.output_dir()) as folder:
        stage = Path(folder)
        (stage / 'client').mkdir()
        dll = stage / 'client/d3d9.dll'
        stand = stage / 'stand.dll'
        assets = stage / 'live.assets.bin'
        assets.write_bytes(b'frozen shaders')
        runner.write_json(stage / 'baseline.json', {'frame_parity': True, 'msaa': 2,
                          'input_sha256': {'northlight-assets.bin': runner.digest(assets)}})
        for binary in (dll, stand):
            binary.write_bytes(identity.encode() + b'\0')
        args = Namespace(stage=stage, output=stage / 'paired', live_timeout=5, timeout=10,
                         stand_dll=stand, stand_assets=stage / 'assets', capture_wrapper=stage / 'wrapper', scene='crowd',
                         host=stage / 'host', tool_root=stage, live_dll=None, live_assets=None, crowd_size=128)
        calls = []
        wrong_assets = False

        def live(options):
            assert options.stage == stage and options.target == 'world' and options.capture_wrapper == args.capture_wrapper
            assert options.live_dll == args.live_dll and options.live_assets == args.live_assets
            assert options.scene == 'crowd' and options.crowd_size == 128
            assert options.fixed_visual_clock == getattr(args, 'fixed_visual_clock', False)
            assert options.light_history_audit == getattr(args, 'light_history_audit', False)
            calls.append('live')
            options.output.mkdir()
            captured_assets = runner.digest(options.live_assets) if options.live_assets else runner.digest(assets)
            runner.write_json(options.output / 'validation.json', {'status': 'PASS', 'observation': {'identity': identity},
                              'runtime_inputs': {'d3d9.dll': runner.digest(options.live_dll or dll),
                                                 'northlight-assets.bin': 'wrong' if wrong_assets else captured_assets}})
            return 0

        def replay(options):
            assert calls == ['live'] and options.stage == stage and options.capture == args.output / 'capture'
            assert options.match_geometry_start == getattr(args, 'match_geometry_start', False)
            calls.append('replay')
            options.output.mkdir()
            report = {
                'status': 'FAIL', 'stand_identity': identity, 'trace_join_validated': True, 'stand_world_observed': True,
                'host_exit_code': 0, 'parity': {'plane_match': {'color': False, 'depth': True}}, 'error': 'Pixel mismatch'}
            if getattr(args, 'light_history_audit', False):
                report['live_lighting_comparison'] = {'history': {}}
            runner.write_json(options.output / 'validation.json', report)
            return 1

        with patch('build_renderer.source_identity', return_value=identity), patch.object(runner, 'run_client', live), patch.object(runner, 'replay', replay):
            assert runner.end_to_end(args) == 1 and calls == ['live', 'replay']
            result = json.loads((args.output / 'validation.json').read_text())
            assert result['status'] == 'FAIL' and result['capture']['status'] == 'PASS'
            assert result['scene'] == 'crowd'
            assert result['replay']['trace_join_validated'] and result['replay']['plane_match'] == {'color': False, 'depth': True}
            assert result['error'] == 'Pixel mismatch'
            assert result['replay']['sha256'] == runner.digest(args.output / result['replay']['report'])

            calls.clear()
            args.output = stage / 'updated'
            args.live_dll, args.live_assets = stage / 'candidate.dll', assets
            args.live_dll.write_bytes(dll.read_bytes())
            dll.write_bytes(b'stale staged DLL')
            assert runner.end_to_end(args) == 1 and calls == ['live', 'replay']
            result = json.loads((args.output / 'validation.json').read_text())
            assert result['live_dll_sha256'] == runner.digest(args.live_dll)
            assert dll.read_bytes() == b'stale staged DLL'

            assets.write_bytes(b'different shaders')
            calls.clear()
            args.output = stage / 'different-assets'
            args.fixed_visual_clock = args.match_geometry_start = True
            args.light_history_audit = True
            assert runner.end_to_end(args) == 1 and calls == ['live', 'replay']
            result = json.loads((args.output / 'validation.json').read_text())
            assert result['status'] == 'FAIL' and result['live_assets_sha256'] == runner.digest(assets)
            assert result['replay']['live_lighting_compared']

            calls.clear()
            wrong_assets = True
            args.output = stage / 'unexpected-runtime-assets'
            assert runner.end_to_end(args) == 1 and calls == ['live']
            result = json.loads((args.output / 'validation.json').read_text())
            assert result['status'] == 'INCOMPLETE' and 'shader assets differ' in result['error']
            wrong_assets = False

            calls.clear()
            args.fixed_visual_clock = False
            args.output = stage / 'geometry-without-clock'
            assert runner.end_to_end(args) == 1 and not calls
            result = json.loads((args.output / 'validation.json').read_text())
            assert result['status'] == 'INCOMPLETE' and 'fixed visual clock' in result['error']
            args.match_geometry_start = False

            assets.write_bytes(b'frozen shaders')
            stand.write_bytes(b'stale')
            args.output = stage / 'stale'
            assert runner.end_to_end(args) == 1 and not calls
            result = json.loads((args.output / 'validation.json').read_text())
            assert result['status'] == 'INCOMPLETE' and 'current source tree' in result['error']


def replay_profile():
    frozen_renderer = b'[renderer]\r\nExperimentalHDR=1\r\nHDRPaperWhite=203\r\n'
    assert runner.sdr_stand_profile(frozen_renderer) == frozen_renderer.replace(b'ExperimentalHDR=1', b'ExperimentalHDR=0')
    for invalid in (b'ExperimentalHDR=0\n', b'ExperimentalHDR=1\nExperimentalHDR=1\n'):
        try:
            runner.sdr_stand_profile(invalid)
        except ValueError as error:
            assert 'one enabled HDR key' in str(error)
        else:
            raise AssertionError('Accepted an ambiguous frozen HDR setting')
    sdr_output = {'resolution': '3840x2160', 'msaa': 2}
    observation = runner.scan([
        'HDR experimental requested=1 active=1 status=0x00000000 reason=active',
        'HDR additional target 3840x2160 msaa=2 active=1',
        'HDR experimental requested=1 active=0 status=0x8876086a reason=hdr-additional-swapchain-failed',
        'SDR primary target 3840x2160 format=22 msaa=2',
    ])
    assert runner.measured_output(observation, 'hdr') is None
    assert runner.measured_output(observation, 'sdr') == sdr_output
    assert observation['hdr_state']['reason'] == 'hdr-additional-swapchain-failed'
    with TemporaryDirectory() as folder:
        stage = Path(folder)
        capture = stage / 'capture'
        capture.mkdir()
        (stage / 'baseline.json').write_text('{}')
        (capture / 'validation.json').write_text('{}')
        try:
            runner.replay(Namespace(capture=capture, output=stage / 'stand', stage=stage, profile_sdr=True))
        except ValueError as error:
            assert 'SDR requires --profile-frames' in str(error)
        else:
            raise AssertionError('Accepted SDR for a parity replay')
        (stage / 'baseline.json').write_text(json.dumps({'frame_parity': True, 'fixed_world_minute': 720}))
        (capture / 'validation.json').write_text(json.dumps({'mode': 'incomplete'}))
        args = Namespace(capture=capture, output=stage / 'stand', stage=stage, profile_frames='2:6:2',
                         fixed_visual_clock=True)
        for fixed_work, expected in ((False, 'requires an image parity replay or --profile-fixed-work'),
                                     (True, 'requires one complete closed capture')):
            args.profile_fixed_work = fixed_work
            try:
                runner.replay(args)
            except ValueError as error:
                assert expected in str(error), error
            else:
                raise AssertionError('Accepted an incomplete profile capture')
    wanted = runner.profile_frames('2:6:3')
    assert runner.light_history_frames({282: 0, 290: 0}, 2) == [280, 281, 282, 288, 289, 290]
    assert runner.light_history_frames([4, 6], 3) == [1, 2, 3, 4, 5, 6]
    assert runner.light_history_interval({282: 0, 290: 0}) == list(range(282, 291))
    for frames, predecessors in (([0, 8], 1), ([10, 18], 9)):
        try:
            runner.light_history_frames(frames, predecessors)
        except ValueError:
            continue
        raise AssertionError('Accepted a light history window outside the supported range')
    assert list(wanted) == [2, 5]
    for invalid in ('2:1:1', '2:3:0', '0:120:1', '2,3'):
        try:
            runner.profile_frames(invalid)
        except ValueError:
            continue
        raise AssertionError(f'Accepted invalid profile window {invalid}')
    assert runner.stand_profile([
        'GPU frame frame=2 total=5.0ms GameScene=3.0ms',
        'DRAWGATE frame=2 draws=82 sceneDraws=80',
        'CPU profile frame=2 effects=1.5ms',
        'GPU profile frame=2 PointShadow=0.768ms',
        'POINT_PHASE rendererFrame=2 submitMs=0.398',
        'POINT_ORDER rendererFrame=2 cpuMs=0.125 packets=49 alphaScopes=3',
        'POINT workload rendererFrame=2 replayDraws=47 casterTriangles=19604',
        'POINT_INDEX_BATCHES rendererFrame=2 enabled=1 draws=3 packets=11',
        'POINT_INDEX_ADMISSION rendererFrame=2 reuse=1 tests=24 hits=67',
        'POINT_EFFECTIVE_POSE rendererFrame=2 enabled=1 merged=18',
        'POINT_MIXED_BAKE rendererFrame=2 enabled=1 faces=6 packets=432 triangles=346088 bakeDraws=0 failures=0',
        'POINT_REPLAY_STATE rendererFrame=2 reuse=1 faces=4 geometryCalls=38 constantCalls=21 constantBytes=1088',
        'POINT palette rendererFrame=2 draws=2 packets=5',
        'POINT_COLD rendererFrame=2 replayDraws=47',
        'CSTREAM QUERYFRAME gameFrame=2 type=8 calls=3 tripMs=12.5',
        'CSTREAM QUERYFRAME gameFrame=2 type=9 calls=6 tripMs=1.3',
        'CSTREAM EVENT_CPU gameFrame=2 cycles=7850 samples=2 legacyGpuSpin=0',
        'CSTREAM EVENT_QUEUE gameFrame=2 samples=1 publishMs=0.0125 retiredCommands=43',
        'MODEL_PREPARE rendererFrame=2 opened=1 worker=1 published=66 workerRecords=48 joinInlineRecords=18 joinWaitMs=0.004',
        'WORLD directional terrain rendererFrame=2 batches=29 draws=7 materialBinds=6 legacy=0',
        'WORLD directional replay rendererFrame=2 cascadePackets=17 cascadeDraws=9 cascadeScopes=2 fallbacks=0 legacy=0',
        'WORLD directional index pairs rendererFrame=2 cascadeEligible=7 cascadePairs=5 cacheHits=4 enabled=1',
        'MODEL_NORMALS range bank frame=2 reads=0 borrowed=245 legacy=0',
        'MODEL_NORMALS pose layout frame=2 gets=0 peeks=178 legacy=0',
        'MODEL_NORMALS palette instances frame=2 enabled=1 draws=51 packets=579',
        'MODEL_NORMALS coverage frame=2 packets=579 draws=579 order=0xfedcba9876543210',
        'REPLAY COPY work frame=2 legacy=0 readLocks=4 readBytes=1024 fills=2 fillBytes=768',
        'REPLAY COPY memory frame=2 valid=1 availableBytes=934593536 largestFreeBytes=615514112',
        'GPU profile frame=5 discarded: disjoint',
        'GPU frame frame=5 total=5.0ms marks_truncated=1',
        'CPU profile frame=6 effects=12.5ms',
    ], wanted) == {2: {'GPU frame': {'total': 5.0, 'GameScene': 3.0},
                      'DRAWGATE': {'draws': 82, 'sceneDraws': 80},
                      'CPU profile': {'effects': 1.5}, 'GPU profile': {'PointShadow': 0.768},
                      'POINT_PHASE': {'submitMs': 0.398}, 'POINT_COLD': {'replayDraws': 47},
                      'POINT_ORDER': {'cpuMs': 0.125, 'packets': 49, 'alphaScopes': 3},
                      'POINT workload': {'replayDraws': 47, 'casterTriangles': 19604},
                      'POINT_INDEX_BATCHES': {'enabled': 1, 'draws': 3, 'packets': 11},
                      'POINT_INDEX_ADMISSION': {'reuse': 1, 'tests': 24, 'hits': 67},
                      'POINT_EFFECTIVE_POSE': {'enabled': 1, 'merged': 18},
                      'POINT_MIXED_BAKE': {'enabled': 1, 'faces': 6, 'packets': 432, 'triangles': 346088,
                                           'bakeDraws': 0, 'failures': 0},
                      'POINT_REPLAY_STATE': {'reuse': 1, 'faces': 4, 'geometryCalls': 38,
                                             'constantCalls': 21, 'constantBytes': 1088},
                      'CSTREAM QUERYFRAME': {'type': 8, 'calls': 3, 'tripMs': 12.5},
                      'CSTREAM EVENT_CPU': {'cycles': 7850, 'samples': 2, 'legacyGpuSpin': 0},
                      'CSTREAM EVENT_QUEUE': {'samples': 1, 'publishMs': 0.0125, 'retiredCommands': 43},
                      'MODEL_PREPARE': {'opened': 1, 'worker': 1, 'published': 66, 'workerRecords': 48,
                                        'joinInlineRecords': 18, 'joinWaitMs': 0.004},
                      'WORLD directional terrain': {'batches': 29, 'draws': 7, 'materialBinds': 6, 'legacy': 0},
                      'WORLD directional replay': {'cascadePackets': 17, 'cascadeDraws': 9, 'cascadeScopes': 2, 'fallbacks': 0, 'legacy': 0},
                      'WORLD directional index pairs': {'cascadeEligible': 7, 'cascadePairs': 5, 'cacheHits': 4, 'enabled': 1},
                      'MODEL_NORMALS range bank': {'reads': 0, 'borrowed': 245, 'legacy': 0},
                      'MODEL_NORMALS pose layout': {'gets': 0, 'peeks': 178, 'legacy': 0},
                      'MODEL_NORMALS palette instances': {'enabled': 1, 'draws': 51, 'packets': 579},
                      'MODEL_NORMALS coverage': {'packets': 579, 'draws': 579, 'order': '0xfedcba9876543210'},
                      'REPLAY COPY work': {'legacy': 0, 'readLocks': 4, 'readBytes': 1024, 'fills': 2, 'fillBytes': 768},
                      'REPLAY COPY memory': {'valid': 1, 'availableBytes': 934593536, 'largestFreeBytes': 615514112},
                      'POINT palette': {'draws': 2, 'packets': 5}}, 5: {}}
    coverage = 'MODEL_NORMALS coverage frame=2 packets=579 draws=579 order=0xfedcba9876543210'
    for invalid in ([coverage, coverage], [coverage[:-1]]):
        try:
            runner.stand_profile(invalid, wanted)
        except ValueError:
            continue
        raise AssertionError('Accepted an ambiguous ModelNormals coverage receipt')
    settings = '[Quality]\nLocalLightLimit=64\n pointshadowlimit = 16\n;PointShadowLimit=64\n[Other]\nPointShadowLimit=32\n'
    assert runner.replay_quality_config(settings, 4) == (
        '[Quality]\nPointShadowLimit=4\nLocalLightLimit=64\n;PointShadowLimit=64\n[Other]\nPointShadowLimit=32\n')
    assert runner.replay_quality_config('[Quality]', 6) == '[Quality]\nPointShadowLimit=6\n'
    budget_config = '[Quality]\n gpuBudgetMS = 4\nActorShadowCpuMs=3\nWorldFog=1\n[Other]\nGpuBudgetMs=8\n'
    assert runner.replay_quality_config(budget_config) == budget_config
    assert runner.replay_quality_config(budget_config, fixed_gpu_budget=True) == (
        '[Quality]\nGpuBudgetMs=0\nActorShadowCpuMs=3\nWorldFog=1\n[Other]\nGpuBudgetMs=8\n')
    assert runner.replay_quality_config(budget_config, fixed_profile_work=True) == (
        '[Quality]\nGpuBudgetMs=0\nActorShadowCpuMs=0\nWorldFog=1\n[Other]\nGpuBudgetMs=8\n')
    assert runner.replay_quality_config('[Quality]\nShadowRefreshCpuMs=3\n', shadow_refresh_cpu_ms=0) == (
        '[Quality]\nShadowRefreshCpuMs=0\n')
    assert runner.replay_quality_config('[Quality]\nFarShadowInterval=4\n[Other]\nFarShadowInterval=9\n',
                                        far_shadow_interval=1) == '[Quality]\nFarShadowInterval=1\n[Other]\nFarShadowInterval=9\n'
    assert runner.replay_quality_config('[Quality]\nWorldFog=1\n[Other]\nWorldShadows=1\n',
                                        world_shadows=False, world_fog=False) == (
        '[Quality]\nWorldShadows=0\nWorldFog=0\n[Other]\nWorldShadows=1\n')
    for interval in (0, 17):
        try:
            runner.replay_quality_config('[Quality]\n', far_shadow_interval=interval)
        except ValueError as error:
            assert 'FarShadowInterval' in str(error)
        else:
            raise AssertionError('Accepted a far shadow interval outside 1..16')
    assert runner.replay_quality_config('[Quality]\nAutoBumpModels=3\n[Other]\nAutoBumpModels=2\n', model_bump=0) == (
        '[Quality]\nAutoBumpModels=0\n[Other]\nAutoBumpModels=2\n')
    assert runner.replay_quality_config('[Quality]\nGI=1\nGIDynamicProbes=1\n', gi_dynamic_probes=False) == (
        '[Quality]\nGIDynamicProbes=0\nGI=1\n')
    for mode in (0, 1):
        ini = '[Quality]\nAutoNormalFilter=0\nAutoBumpModels=3\n[Other]\nAutoNormalFilter=1\n'
        assert runner.replay_quality_config(ini, normal_filter=mode) == ini.replace(
            'AutoNormalFilter=0', f'AutoNormalFilter={mode}', 1)
    census_line = ('POINT_BATCH_AUDIT rendererFrame=2 slot=1 face=0 packets=5 opaque=3 cutout=1 '
                   'unknownBounds=1 single=2 singleRuns=1 weighted=1 reindexPairs=0 reindexTailWords=0')
    assert runner.point_shadows.census([census_line], (2,))['2'][0]['singleRuns'] == 1
    try:
        runner.point_shadows.census([census_line, census_line], (2,))
    except ValueError:
        pass
    else:
        raise AssertionError('Accepted a duplicated cube-face census')
    cube = dict(origin=[0, 1, 2], range=[1, 12], map='Northrend', mesh_generation='1',
                complete=True, faces=['a'] * 6)
    comparison = runner.point_shadows.compare_cubes(
        {'2': {'1': cube, '2': cube}},
        {'2': {'1': {**cube, 'faces': ['a'] * 5 + ['b']}, '2': {**cube, 'range': [1, 13]}}})
    assert comparison['2']['different_faces'] == {'1': [5]}
    assert comparison['2']['incompatible_publications'] == ['2']


def capture_work():
    prefix = ('CAPTURE_WORK_DRAW device=1 epoch=0 frame=282 draw=103 route=7 up=0 indexed=1 topology=4 '
              'base=-2 minimum=10 vertices=16 start=3 primitives=12 source=0000000000000012 sourceBytes=384 '
              'pose=1 bank=1 ')
    draw = (prefix + 'material=384,1,0,1,0,1,1,0,0 normals=0,0,0,1,1,1,1,2,15 '
            'world=384,1,0,1,1,0,0,0,0')
    marker = 'CAPTURE_WORK_FRAME device=1 epoch=0 frame=282 rows=1 fpsEligible=0 cpuSourceBytes=384 gpuSceneBytes=4096'
    result = runner.capture_work.census([draw, marker], 282)
    summary = result['summary']
    assert summary['multi_consumer_packet_draws'] == summary['multi_consumer_charge_draws'] == summary['identified_draws'] == 1
    assert summary['consumers']['normals']['float_handoff_registers'] == 15
    assert summary['consumers']['world']['float_get_registers'] == 0
    assert result['draws'][0]['base'] == -2 and not result['fps_eligible']
    material, normals, world = draw[len(prefix):].split()
    draw2 = f'{prefix}{material},0,0 {normals},3,7 {world},0,0'
    result2 = runner.capture_work.census([draw2, marker], 282)
    assert result['format'] == 1 and result2['format'] == 2
    normals2 = result2['summary']['consumers']['normals']
    assert normals2['range_read_registers'] == 3 and normals2['range_handoff_registers'] == 7
    packet_rows = runner.capture_work.census([draw2.replace(f'{world},0,0', f'{world},32,16'), marker], 282)
    assert packet_rows['summary']['consumers']['world']['range_read_registers'] == 32
    assert packet_rows['draws'][0]['world']['range_handoff_registers'] == 16
    assert 'range_read_registers' not in summary['consumers']['world']
    for via_get, via_bank in ((4, 0), (0, 4)):
        observed = f'world=384,1,0,1,1,{int(bool(via_bank))},0,{via_get},{via_bank}'
        work = runner.capture_work.census([draw.replace(world, observed), marker], 282)
        assert work['draws'][0]['world']['float_get_registers'] == via_get
        assert work['summary']['consumers']['world']['float_handoff_registers'] == via_bank
    marker3 = marker + ' bufferRows=5'
    vb = 'CAPTURE_WORK_BUFFER device=1 epoch=0 frame=282 draw=103 lane=1 buffer=12 index=0 path=B offset=10 size=128 ok=1'
    ib = 'CAPTURE_WORK_BUFFER device=1 epoch=0 frame=282 draw=103 lane=4 buffer=ff index=1 path=F offset=0 size=256 ok=1'
    copy = ib.replace('path=F offset=0 size=256', 'path=C offset=2 size=6')
    failed = vb.replace('path=B offset=10 size=128 ok=1', 'path=B offset=20 size=64 ok=0')
    reads = [vb, vb.replace('lane=1', 'lane=2'), ib, copy, failed]
    result3 = runner.capture_work.census(reads + [draw2, marker3], 282)
    coverage = result3['summary']['buffer_reads']
    assert result3['format'] == 3 and len(result3['draws'][0]['buffer_reads']) == 5
    assert coverage['shared_exact_span_draws'] == coverage['shared_exact_lock_span_draws'] == 1
    world_ib, material_vb = coverage['consumers']['world']['ib'], coverage['consumers']['material']['vb']
    assert world_ib['lock_attempts'] == world_ib['fill_attempts'] == world_ib['copy_views'] == 1
    assert world_ib['lock_span_bytes'] == world_ib['fill_span_bytes'] == 256 and world_ib['copy_span_bytes'] == 6
    assert material_vb['lock_attempts'] == 2 and material_vb['lock_span_bytes'] == 128 and material_vb['failed_locks'] == 1
    no_reads = runner.capture_work.census([draw2, marker3.replace('bufferRows=5', 'bufferRows=0')], 282)
    assert no_reads['format'] == 3 and not no_reads['draws'][0]['buffer_reads']
    skin = draw2.replace('material=', 'shaderClass=m2_skinning alphaTest=1 material=')
    terrain = skin.replace('draw=103', 'draw=104').replace('m2_skinning alphaTest=1', 'terrain alphaTest=-1')
    classified_marker = marker3.replace('rows=1', 'rows=2')
    result4 = runner.capture_work.census(reads + [skin, terrain, classified_marker], 282)
    model, ground = result4['summary']['capture_classes']
    assert result4['format'] == 4 and (model['shader_class'], model['alpha_test']) == ('m2_skinning', 'on')
    assert (ground['shader_class'], ground['alpha_test']) == ('terrain', 'unknown')
    assert model['draws'] == ground['draws'] == 1 and model['primitives'] == ground['primitives'] == 12
    assert model['buffer_reads'] == coverage
    assert ground['buffer_reads']['consumers']['material']['vb']['lock_span_bytes'] == 0
    for consumer in ('material', 'normals', 'world'):
        for field, total in result4['summary']['consumers'][consumer].items():
            assert total == model['consumers'][consumer][field] + ground['consumers'][consumer][field]
    for invalid in ([draw], [marker], [draw, draw, marker], [draw, marker.replace('rows=1', 'rows=2')],
                    [draw.replace('epoch=0', 'epoch=1'), marker], [draw.replace('frame=282', 'frame=290'), marker],
                    [draw.replace('material=384', 'material=-1'), marker],
                    [draw2.replace(f'{material},0,0', material), marker],
                    [draw2.replace(f'{material},0,0', f'{material},0,1'), marker],
                    [draw, draw2.replace('draw=103', 'draw=104'), marker.replace('rows=1', 'rows=2')],
                    reads + [draw2, marker], reads[1:] + [draw2, marker3],
                    reads + [draw2.replace('route=7', 'route=6'), marker3],
                    [vb.replace('draw=103', 'draw=104')] + reads[1:] + [draw2, marker3],
                    [draw2] + reads + [marker3],
                    [skin, draw2.replace('draw=103', 'draw=104'), classified_marker.replace('bufferRows=5', 'bufferRows=0')],
                    reads + [skin.replace('alphaTest=1', 'alphaTest=2'), marker3],
                    reads + [skin.replace('m2_skinning', 'grouped_unverified'), marker3],
                    [vb.replace('path=B', 'path=F')] + reads[1:] + [draw2, marker3]):
        try:
            runner.capture_work.census(invalid, 282)
        except ValueError:
            pass
        else:
            raise AssertionError('Accepted incomplete or inconsistent capture work evidence')


def raw_frame_intervals():
    ticks = [20] * 116 + [80, 40, 20, 60]
    window = dict(samples='120', device='1', qpcHz='1000', focus='1', enabled='1',
                  gi='1', shadows='1', fog='0', meanMs=f'{sum(ticks)/120:.3f}',
                  p50Ms='20.000', p95Ms='20.000', maxMs='80.000', deltas=','.join(map(str, ticks)))
    first = dict(window, frame='120', lastPresent='120')
    second = dict(window, frame='240', lastPresent='240')
    timing = runner.frame_timing.summarize([first, second])
    assert timing['state'] == 'measured' and timing['samples'] == 240
    assert (timing['p50_ms'], timing['p95_ms'], timing['p99_ms'], timing['max_ms']) == (20, 20, 60, 80)
    assert timing['windows'][1] == {'first_present': 121, 'last_present': 240, 'qpc_deltas': ticks}
    assert [row['present'] for row in timing['worst']] == [117, 237, 120, 240, 118]
    assert timing['slow_frames']['60']['frames_over_budget'] == 240
    assert timing['slow_frames']['30']['fraction'] == 6 / 240
    background = [dict(row, focus='0') for row in (first, second)]
    timing = runner.frame_timing.summarize(background, background=True)
    assert timing['window_mode'] == 'background' and timing['samples'] == 240
    for broken in ({}, dict(second, maxMs='20'), dict(second, focus='0'), dict(second, complete='0'),
                   dict(second, fog='1'), dict(second, frame='239', lastPresent='239')):
        try:
            runner.frame_timing.summarize([first, broken])
        except ValueError:
            pass
        else:
            raise AssertionError('Invalid raw Present timing was accepted')
    try:
        runner.frame_timing.summarize([background[0], second], background=True)
    except ValueError:
        pass
    else:
        raise AssertionError('Accepted a focused interval in the background measurement')


def native_m2_audit():
    client = 'NATIVE_M2 client12340=1'
    producer = ('NATIVE_M2 producerFrame=127 gxDraws=3200 ordinary=2900 matched=2800 '
                'rangeMismatch=0 paletteChecks=180 paletteMismatch=0 decodeUs=510.4')
    normals = ('NATIVE_M2 normals frame=127 producerFrame=127 domain=world-ready seen=2700 '
               'eligible=2700 queued=2400 immutable=2400 fallback=300 rangeMismatch=0 '
               'coverageMismatch=0 paletteChecks=150 paletteMismatch=0 paletteUnavailable=0 auditUs=423.1')
    assert runner.observe_native_m2([client, producer, normals], 1)['ready']
    grouped_client = 'NATIVE_M2 groupedClient12340=1'
    grouped = ('NATIVE_M2 grouped producerFrame=127 shaderDraws=2 softwareDraws=1 alphaTested=1 '
               'matched=1 multiInstance=1 instances=3 triangles=448 rangeMismatch=0 paletteUnavailable=1 paletteMismatch=0')
    observed = runner.observe_native_m2([client, grouped_client, producer, grouped, normals], 1)['grouped']
    assert observed['verified_client'] and observed['samples'][0]['world_ready']
    assert observed['samples'][0]['instances'] == 3
    full = grouped.replace('matched=1', 'matched=2').replace('paletteUnavailable=1', 'paletteUnavailable=0')
    checked = runner.observe_native_m2([client, grouped_client, producer, full, normals], 1, True)
    assert checked['ready'] and checked['grouped']['ready'] and checked['grouped']['warm_samples'] == 1
    assert not runner.observe_native_m2([client, producer, normals], 1, True)['ready']
    assert not runner.observe_native_m2([client, grouped_client, producer, grouped, normals], 1, True)['ready']
    world_client = 'NATIVE_WORLD_BATCH wmo12340=1 adt12340=1'
    adt_client = 'NATIVE_ADT_BOUNDS client12340=1'
    world = ('NATIVE_WORLD_BATCH producerFrame=127 wmo=497 wmoRanges=497 wmoBounds=497 '
             'adt=2 adtRanges=2 adtOrigins=2 unattributed=1022 adtBounds=2')
    coverage = 'NATIVE_ADT_COVERAGE producerFrame=127 match=1'
    world_lines = [client, world_client, adt_client, producer, coverage, coverage, world, normals]
    checked = runner.observe_native_m2(world_lines, 1, require_world_batches=True)
    assert checked['ready'] and checked['world_batches']['warm_samples'] == 1
    summary = 'NATIVE_ADT_COVERAGE producerFrame=127 compared=2 contained=2'
    summary_lines = world_lines[:4] + [summary, world, normals]
    assert runner.observe_native_m2(summary_lines, 1, require_world_batches=True)['world_batches'] == checked['world_batches']
    assert not runner.observe_native_m2(world_lines[:4] + [summary.replace('contained=2', 'contained=1'), world, normals],
                                       1, require_world_batches=True)['ready']
    assert not runner.observe_native_m2([client, producer, world, normals], 1, require_world_batches=True)['ready']
    assert not runner.observe_native_m2(world_lines[:4] + [world, normals], 1, require_world_batches=True)['ready']
    assert not runner.observe_native_m2(world_lines[:5] + [coverage.replace('match=1', 'match=0'), world, normals],
                                       1, require_world_batches=True)['ready']
    assert not runner.observe_native_m2(world_lines[:4] + [world.removesuffix(' adtBounds=2'), normals],
                                       1, require_world_batches=True)['ready']
    for lines, required in (([producer, normals], 1), ([client, producer, normals], 2),
                            ([client, producer, normals.replace('world-ready', 'world-loading')], 1),
                            ([client, producer, normals.replace('paletteUnavailable=0', 'paletteUnavailable=1')], 1)):
        assert not runner.observe_native_m2(lines, required)['ready']
    for lines in ([client, normals], [client, producer, producer, normals],
                  [client, producer, normals, normals],
                  [client, producer, grouped, normals],
                  [client, grouped_client, producer, grouped, grouped, normals],
                  [client, grouped_client, producer, grouped.replace('matched=1', 'matched=2'), normals],
                  world_lines[:-2] + [world, world, normals],
                  world_lines[:-2] + [coverage, world, normals],
                  world_lines[:4] + [summary.replace('contained=2', 'contained=3'), world, normals],
                  world_lines[:-2] + [world.replace('adtBounds=2', 'adtBounds=3'), normals],
                  world_lines[:-2] + [world.replace('wmoBounds=497', 'wmoBounds=498'), normals],
                  [client, producer, normals.replace('fallback=300', 'fallback=301')],
                  [client, producer, normals.replace('paletteMismatch=0', 'paletteMismatch=1')]):
        try:
            runner.observe_native_m2(lines, 1)
        except ValueError:
            pass
        else:
            raise AssertionError('Accepted broken M2 command or palette evidence')


def live_comparison():
    quality = live_ab.live_quality('[Quality]\nDiagnostics=0\nRenderProfile=0\nWorldFog=1\n')
    assert quality == '[Quality]\nRenderProfile=1\nDiagnostics=1\nWorldFog=1\n'
    rows = live_ab.samples([
        'GPU frame frame=99 GameScene=100.0ms',
        'GPU frame frame=100 GameScene=12.0ms',
        'GPU frame frame=101 GameScene=8.0ms',
        'GPU frame frame=105 GameScene=100.0ms',
        'CSTREAM QUERYFRAME gameFrame=100 type=8 errors=0 tripMs=4.5',
        'CSTREAM QUERYFRAME gameFrame=100 type=9 errors=0 tripMs=0.5',
        'CSTREAM EVENT_CPU gameFrame=101 cycles=4000 samples=2 shortGpuTrips=1',
        'GAME_INDEXED_PAIR frame=101 saved=3',
        'MEMORY frame=104 availableVirtualMiB=900 largestFreeMiB=439',
        'MEMORY frame=105 availableVirtualMiB=920 largestFreeMiB=426',
    ], [{'first_present': 100, 'last_present': 104}])
    assert rows['GPU frame']['GameScene'] == {'count': 2, 'min': 8.0, 'median': 10.0, 'max': 12.0}
    assert rows['MEMORY']['largestFreeMiB']['min'] == 439
    assert rows['CSTREAM QUERYFRAME type=8']['tripMs']['median'] == 4.5
    assert rows['CSTREAM QUERYFRAME type=9']['tripMs']['median'] == .5
    assert live_ab.switch_observed(rows, 'event', True)
    assert not live_ab.switch_observed(rows, 'event', False)
    assert live_ab.switch_observed(rows, 'gamescene', True)
    assert not live_ab.switch_observed(rows, 'particles', True)
    assert live_ab.compare([{'timing': dict.fromkeys(('p50_ms', 'p95_ms', 'p99_ms'), v)} for v in (11, 9, 10, 13)])['p95_ms'] == {
        'on_minus_off': [-2, -3], 'repeat_spread': {'off': 2, 'on': 1}}


def private_window_focus():
    user = Mock()
    user.GetForegroundWindow.side_effect = [17, 17, 42]
    user.GetWindowThreadProcessId.return_value = 5
    user.AttachThreadInput.return_value = True
    kernel = Mock()
    kernel.GetCurrentThreadId.return_value = 7
    with patch.object(runner.ctypes, 'windll', Mock(user32=user, kernel32=kernel), create=True):
        assert runner.focus_private_window(42)
    assert [call.args for call in user.AttachThreadInput.call_args_list] == [(7, 5, True), (7, 5, False)]
    assert [call.args for call in user.SetForegroundWindow.call_args_list] == [(42,), (42,)]
    user.GetForegroundWindow.side_effect = [17, 17]
    user.AttachThreadInput.reset_mock(return_value=True)
    user.AttachThreadInput.return_value = False
    with patch.object(runner.ctypes, 'windll', Mock(user32=user, kernel32=kernel), create=True):
        assert not runner.focus_private_window(42)
    user.AttachThreadInput.assert_called_once_with(7, 5, True)


def menu_return():
    menu = 'WORLD context validated: map="Menu:one"'
    world = 'WORLD context validated: map="Northrend"'
    events = ['CHAR_LIST', 'PLAYER_SPAWN', 'PLAYER_LOGOUT', 'CHAR_LIST']
    assert runner.observed_menu_return([menu, world, menu], events, 2)
    assert not runner.observed_menu_return([menu, world], events, 2)
    assert not runner.observed_menu_return([menu, world, menu], events[:-1], 2)
    assert not runner.observed_menu_return([menu, world, menu], ['CHAR_LIST'], 2)
    admission = 'MENU scene admitted frame=120 effects=screen-only depthRange=0..1'
    screen = 'WORLD screen geometry=1 meshReason=ready material=1 map="Menu:two"'
    assert runner.observed_menu_return([menu, world, admission, screen], events, 2)
    assert not runner.observed_menu_return([menu, world, screen], events, 2)
    assert not runner.observed_menu_return([menu, world, admission], events, 2)
    assert not runner.observed_menu_return([menu, world, screen, admission], events, 2)
    first = 'FRAME_PARITY present=101 saved=1 depth_samples=2 depth_quality=0'
    last = 'FRAME_PARITY present=109 saved=1 depth_samples=2 depth_quality=0'
    frames = {101: 0, 109: 0}
    complete = {'initial_menu': True, 'world_presents': [101, 109], 'returned_to_menu': True}
    assert runner.menu_parity_lifecycle([menu, world, first, last, admission, screen], frames) == complete
    assert runner.menu_parity_lifecycle([world, first, last, menu], frames) == dict(complete, initial_menu=False)
    assert runner.menu_parity_lifecycle([menu, world, first, admission, screen, last], frames) == dict(
        complete, returned_to_menu=False)


def rain_readiness():
    assert server.rain_packet() == bytes.fromhex('050000009a99593f01')
    events = ['WEATHER_SENT kind=rain source=1f4 packet=050000009a99593f01']
    wet = 'WEATHER kind=rain prims=220 draws=5 intensity=0.850 blend=0.980 rainMask=2 rainMrtRuns=1'
    assert runner.observed_rain([wet], events)['ready']
    assert not runner.observed_rain([wet], [])['ready']
    assert not runner.observed_rain([wet.replace('rainMask=2', 'rainMask=0')], events)['ready']
    assert not runner.observed_rain([wet, 'WEATHER kind=none prims=0 draws=0'], events)['ready']
    assert server.spawn_for('shoreline') != server.spawn_for('rain')
    assert server.character_list(server.spawn_for('shoreline')) != server.character_list()
    water = 'WATER mask draws=7 deferred=0 clears=1 mrt=6 redraw=1 requested=1 targets=4'
    assert runner.observed_water([water])['ready']
    assert not runner.observed_water([water, water.replace('targets=4', 'targets=0')])['ready']


def cold_camera_turn():
    packet = server.turn_packet(server.SPAWN, 1234)
    assert packet[:2] == b'\x01\x01'
    sequence, flags, extra, tick, x, y, z, heading, fall = struct.unpack('<IIHI4fI', packet[2:])
    assert (sequence, flags, extra, tick, fall) == (1, 0, 0, 1234, 0)
    assert sum((x - origin) ** 2 for x, origin in zip((x, y, z), server.SPAWN)) < 1e-6
    assert .9 < heading - server.SPAWN[3] < 1.2
    before = {'tick': 1000, 'eye': (1, 2, 3), 'forward': (1, 0, 0)}
    after = {'tick': 1250, 'eye': (1, 2, 3), 'forward': (0, 1, 0)}
    assert runner.observed_camera_turn(before, after)
    assert not runner.observed_camera_turn(before, dict(after, tick=before['tick']))
    assert not runner.observed_camera_turn(before, dict(after, forward=before['forward']))
    assert not runner.observed_camera_turn(before, dict(after, eye=(41, 42, 43)))


def interior_refresh():
    world = 'WORLD probe atlas ready map=Northrend generation=1'
    pool = 'POINT pool limit=64 selected=4 ready=3 cubeSlots=4'
    update = 'POINT schedule updates=18 reuses=120 reused=0 crowd=0 periodMs=33'
    assert runner.observed_interior_lamps([world, pool, update, update])['ready']
    assert not runner.observed_interior_lamps([world, pool, update])['ready']
    assert not runner.observed_interior_lamps([pool, update, world, update])['ready']


def terrain_capture():
    captured = 'TERRAIN_SWAP frame=508 setVS=0 setPS=1314 apiFailures=0 totalNative=1314'
    assert server.spawn_for('terrain') != server.SPAWN
    assert runner.observed_terrain_capture([captured])['ready']
    assert not runner.observed_terrain_capture([captured.replace('setPS=1314', 'setPS=0')])['ready']
    assert not runner.observed_terrain_capture([captured, captured.replace('apiFailures=0', 'apiFailures=1')])['ready']


def dxvk_adapter_evidence():
    adapter = 'NVIDIA GeForce RTX 4090'
    available = f'info:  Found device: {adapter} (NVIDIA 621.14.0)\r\n'
    selected = f'info:  Creating device:\r\ninfo:  {adapter}:\r\n'
    amd = 'info:  Creating device:\r\ninfo:  AMD Radeon(TM) Graphics:\r\n'
    with TemporaryDirectory(dir=fp.output_dir()) as temporary:
        run = Path(temporary)
        log = run / runner.DXVK_LOG
        with patch.dict(runner.os.environ, {runner.DXVK_ADAPTER_ENV: adapter, 'DXVK_CONFIG': 'unrelated override'}):
            env = runner.child_environment(run)
        assert env[runner.DXVK_ADAPTER_ENV] == adapter and 'DXVK_CONFIG' not in env
        log.write_bytes((available + selected + selected).encode())
        assert runner.dxvk_adapter(log, adapter) == {
            'filter': adapter, 'device': adapter, 'log_sha256': sha256(log.read_bytes()).hexdigest()}
        for bad in (available, available + amd, available + selected + amd):
            log.write_bytes(bad.encode())
            try:
                runner.dxvk_adapter(log, adapter)
            except ValueError:
                continue
            raise AssertionError('Accepted DXVK enumeration without an exclusive selected adapter')


def artifact_lifecycle():
    with TemporaryDirectory(dir=fp.output_dir()) as temporary:
        root = Path(temporary).resolve()
        run = root / 'run'
        client = root / 'client'
        run.mkdir()
        client.mkdir()
        (run / 'prior-WTF').mkdir()
        (run / 'prior-WTF/Config.wtf').write_bytes(b'original')
        (client / 'WTF').mkdir()
        (client / 'WTF/Config.wtf').write_bytes(b'generated')
        (client / 'Cache').mkdir()
        (client / 'Cache/material.bin').write_bytes(b'cached')
        shared = client / 'world-cache'
        shared.mkdir()
        original = shared / 'model.fgs'
        original.write_bytes(b'fixture input')
        stand = run / 'stand'
        (stand / 'world-cache').mkdir(parents=True)
        runner.os.link(original, stand / 'world-cache/model.fgs')
        (stand / runner.LOG).write_bytes(b'replay log')
        (run / 'plan').mkdir()
        (run / 'plan/scene.nljoin').write_bytes(b'joined binary')
        (run / 'plan/trace.dump').write_bytes(b'regenerable text')

        report = {}
        runner.restore_client_state(client, run, ('WTF', 'Cache'), report, keep=False)
        runner.discard_work(report, run, ('stand/world-cache', 'plan/trace.dump'))
        assert not report, report
        assert (client / 'WTF/Config.wtf').read_bytes() == b'original'
        assert not (client / 'Cache').exists()
        assert not (run / 'prior-WTF').exists()
        assert not (stand / 'world-cache').exists()
        assert not (run / 'plan/trace.dump').exists()
        assert original.read_bytes() == b'fixture input'
        assert (stand / runner.LOG).read_bytes() == b'replay log'
        assert (run / 'plan/scene.nljoin').read_bytes() == b'joined binary'

        (client / 'Cache').mkdir()
        runner.restore_client_state(client, run, ('Cache',), report, keep=True)
        assert not report and (run / 'generated-Cache').is_dir()
        runner.discard_work(report, run, ('../client',))
        assert report['scratch_cleanup_errors'] and original.is_file()
        runner.prepare_parity_images(run / 'frame-parity', raw=False)
        runner.prepare_parity_images(run / 'external-reader', raw=True)
        assert (run / 'frame-parity' / runner.BLOBS).is_dir()
        assert not (run / 'external-reader' / runner.BLOBS).exists()


if __name__ == '__main__':
    packets()
    crowd_packets()
    clock()
    intervals()
    raw_frame_intervals()
    resize_checkpoints()
    replay_geometry_checkpoint()
    captured_shader_bundle()
    profile()
    replay_profile()
    capture_work()
    native_m2_audit()
    live_comparison()
    private_window_focus()
    menu_return()
    rain_readiness()
    cold_camera_turn()
    interior_refresh()
    terrain_capture()
    dxvk_adapter_evidence()
    artifact_lifecycle()
    end_to_end()
    print('PASS Northrend fixture packet layout and baseline gates')
