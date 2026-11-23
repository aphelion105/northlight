#!/usr/bin/env python3
# northlight-test:
"""Check release live receipts without starting WoW or a GPU."""
from copy import deepcopy
import os
from pathlib import Path
import sys
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import run_northrend_baseline as baseline
import run_release_live_gate as gate


def rejected(check):
    try:
        check()
    except ValueError:
        return
    raise AssertionError('Accepted incomplete or mismatched release evidence')


def main():
    assert gate.launch_profile('[renderer]\nBackend=dxvk\nBackendPath=backend_d3d9.dll\nExperimentalHDR=1\n', 'dxvk2') == (
        '[renderer]\nBackend=dxvk2\nExperimentalHDR=1\n')
    assert gate.world_quality('[Quality]\nPreset=Quality\nDiagnostics=0\n;RenderProfile=0\n') == (
        '[Quality]\nDiagnostics=1\nPreset=Quality\n;RenderProfile=0\n')
    with patch.object(baseline, 'press_key') as key:
        events = ['REALM_LIST_SENT', 'WORLD_AUTH_OK', 'CHAR_LIST']
        actions = {'login': 0, 'character': 0}
        baseline.advance_login(123, events, actions, 0, 5, enter_world=False)
        key.assert_not_called()
        baseline.advance_login(123, events, actions, 0, 5)
        key.assert_called_once_with(123, 0x0d)
    client = Path('private client with spaces').resolve()
    backend = gate.BACKENDS['dxvk']
    video = {'resolution': '3840x2160', 'msaa': 2}
    assert {(target, selected) for _, target, selected in gate.CASES} == {
        (target, selected) for target in ('menu', 'world') for selected in ('dxvk', 'dxvk2', 'native')}
    observed = baseline.scan([
        f'BACKEND selected={client / backend} runtime=v3.1.1 rules=dxvk BackendPath=(default) fallback=0',
        'BUILD identity source=current built=fixture', 'RESOURCES NORTHLIGHT_ASSETS_V1: assets loaded=1',
        'QUALITY preset=Quality file=northlight-quality.ini problems=0',
        'WORLD context validated: map="Menu:fixture" camera=(0 0 0)',
        'HDR experimental requested=1 active=1',
        'HDR additional target 3840x2160 format=22 msaa=2 active=1',
    ])
    expected = {'d3d9.dll': 'dll', 'northlight-assets.bin': 'assets', backend: 'backend', gate.ATMOSPHERE: None}
    menu = dict(status='PASS', checkpoint='menu-character-ready', exit_code=0, forced_shutdown=False, observation=observed,
                runtime_inputs=expected.copy(), runtime_payload_after=expected.copy())
    gate.validate_live(menu, expected, 'current', client, 'dxvk', 'menu', video)
    for section, key, value in [
        ('observation', 'backend_fallback', True), ('observation', 'backend_path', str(client / 'other.dll')),
        ('observation', 'identity', 'old'), ('observation', 'menu_seen', False), ('observation', 'quality_problems', 1),
        ('observation', 'hdr_active', False), ('observation', 'output', {'resolution': '3840x2160', 'msaa': 1}),
        ('runtime_inputs', 'd3d9.dll', 'old'), ('runtime_payload_after', backend, 'changed'),
        ('runtime_payload_after', gate.ATMOSPHERE, 'from another install'),
    ]:
        bad = deepcopy(menu)
        bad[section][key] = value
        rejected(lambda: gate.validate_live(bad, expected, 'current', client, 'dxvk', 'menu', video))
    world = deepcopy(menu)
    world.update(checkpoint='northrend-static-ready', crowd={'ready': True, 'players': 128, 'client_name_requests': 128},
                 windows=[{'focus': '1'}])
    world['observation'].update(world_seen=True, gi_tick=50, static_tick=100)
    gate.validate_live(world, expected, 'current', client, 'dxvk', 'world', video)
    world['windows'][0]['focus'] = '0'
    rejected(lambda: gate.validate_live(world, expected, 'current', client, 'dxvk', 'world', video))
    world.update(window_mode='background', frame_timing={'window_mode': 'background', 'state': 'measured', 'samples': 120})
    gate.validate_live(world, expected, 'current', client, 'dxvk', 'world', video, background=True)
    world['foreground_attempts'] = 1
    rejected(lambda: gate.validate_live(world, expected, 'current', client, 'dxvk', 'world', video, background=True))
    world['foreground_attempts'] = 0
    world['windows'][0]['focus'] = '1'
    rejected(lambda: gate.validate_live(world, expected, 'current', client, 'dxvk', 'world', video, background=True))
    windows = Path('fixture Windows').resolve()
    with patch.dict(os.environ, WINDIR=str(windows)):
        native = deepcopy(menu)
        native['observation'].update(baseline.scan([
            f'BACKEND selected={windows / "System32/d3d9.dll"} runtime=non-DXVK rules=native BackendPath=(default) fallback=0',
            'BUILD identity source=current built=fixture',
            'HDR experimental requested=1 active=0 reason=backend',
            'SDR primary target 3840x2160 format=22 msaa=2',
        ]))
        gate.validate_live(native, expected, 'current', client, 'native', 'menu', video)
        for key, value in [('backend', 'v2.7.1'), ('backend_path', str(client / 'd3d9.dll')),
                           ('sdr_output', None), ('hdr_active', True)]:
            bad = deepcopy(native)
            bad['observation'][key] = value
            rejected(lambda: gate.validate_live(bad, expected, 'current', client, 'native', 'menu', video))
        for key, value in [('status', 'UNSUPPORTED'), ('exit_code', 1), ('forced_shutdown', True),
                           ('crash_files', ['crash.dmp'])]:
            bad = deepcopy(native)
            bad[key] = value
            rejected(lambda: gate.validate_live(bad, expected, 'current', client, 'native', 'menu', video))
    caps = 'NATIVE_D3D9_CAPS adapter=0 vendor=10de device=2684 INTZ=1 RESZ=0 floatRT=1 SM3=1\n'
    assert gate.parse_native_capabilities(caps) == {
        'status': 'UNSUPPORTED', 'adapter': 0, 'vendor': '10de', 'device': '2684',
        'capabilities': {'INTZ': True, 'RESZ': False, 'floatRT': True, 'SM3': True}}
    assert gate.parse_native_capabilities(caps.replace('RESZ=0', 'RESZ=1'))['status'] == 'SUPPORTED'
    for invalid in (caps.replace(' RESZ=0', ''), caps.replace('RESZ=0', 'RESZ=2'), caps.replace('adapter=0', 'adapter=1')):
        rejected(lambda: gate.parse_native_capabilities(invalid))
    switch = baseline.scan(['HDR experimental requested=0 active=0', 'SDR primary target 3840x2160 format=22 msaa=2',
                            'HDR experimental requested=1 active=1'])
    assert baseline.measured_output(switch, 'sdr') is None
    assert baseline.measured_output(switch, 'hdr') is None
    print('PASS release ZIP identity, unchanged payload, menu readiness and focused/background World evidence')


if __name__ == '__main__':
    main()
