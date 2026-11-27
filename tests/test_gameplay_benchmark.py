#!/usr/bin/env python3
# northlight-test:
"""Check the gameplay benchmark's roster, timing and backend evidence gates."""
from argparse import Namespace
from io import StringIO
from pathlib import Path
import struct
import sys
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import benchmark_gameplay as bench
import gameplay_runtime as runtime
import northrend_fixture_server as fixture


def events(scene):
    world, count = bench.SCENES[scene]
    players = fixture.scene_roster(world, count)
    spawn = fixture.spawn_record(players, b''.join(fixture.crowd_packets(players)))
    lines = [spawn, f'BENCH_ARM actors={count}', 'BENCH phase=start tick_ns=1000000000']
    if scene == 'ratchet':
        lines.append('BENCH phase=right tick_ns=6000000000')
    lines += ['BENCH phase=done tick_ns=11000000000',
              f'BENCH_PAYLOAD header:1000:{500 if scene == "ratchet" else 0}:10000.000']
    for first in range(1, 1001, 20):
        lines.append(f'BENCH_PAYLOAD data:{first}:' + ','.join(['10.000'] * 20))
    return lines + ['BENCH_PAYLOAD complete']


class GameplayBenchmarkTests(unittest.TestCase):
    def test_client_locale_follows_the_installed_locale_archive(self):
        locale_of = bench.live.data_locale
        with TemporaryDirectory() as temp:
            data = Path(temp)
            with self.assertRaisesRegex(ValueError, 'no <locale>'):
                locale_of(data)
            (data / 'dede').mkdir()
            (data / 'dede' / 'locale-dede.MPQ').write_bytes(b'MPQ')
            self.assertEqual(locale_of(data), 'deDE')
            self.assertEqual(locale_of(data, 'DEDE'), 'deDE')
            with self.assertRaisesRegex(ValueError, r'locale frFR is not installed \(found: dede\)'):
                locale_of(data, 'frFR')
            (data / 'ruRU').mkdir()
            (data / 'ruRU' / 'locale-ruRU.MPQ').write_bytes(b'MPQ')
            with self.assertRaisesRegex(ValueError, 'several locales'):
                locale_of(data)
            self.assertEqual(locale_of(data, 'ruRU'), 'ruRU')
        self.assertEqual(bench.live.realmlist_path('ruRU'), 'Data/ruRU/realmlist.wtf')
        self.assertIn('SET locale "ruRU"\n', bench.live.graphics_profile('', '1920x1080', 1, 'ruRU'))
        self.assertIn('SET locale "enGB"\n', bench.live.graphics_profile('', '1920x1080', 1))

    def test_display_sized_resolution_is_maximized(self):
        profile = bench.live.graphics_profile
        self.assertIn('SET gxmaximize "1"\n', profile('', '2560x1440', 1, 'enUS', True))
        self.assertIn('SET gxmaximize "0"\n', profile('', '3840x2160', 1, 'enUS', False))
        self.assertIn('SET gxmaximize "1"\n', profile('', '3840x2160', 1))

    def test_primary_display_uses_physical_pixels(self):
        def current_mode(device, settings, mode):
            self.assertIsNone(device)
            self.assertEqual(settings.value, 0xffffffff)
            mode._obj.width, mode._obj.height = 3840, 2160
            return True

        with patch.object(bench.ctypes, 'windll', create=True) as api:
            api.user32.GetSystemMetrics.side_effect = (2560, 1440)
            api.user32.EnumDisplaySettingsW.side_effect = current_mode
            self.assertEqual(bench.primary_display_pixels(), (3840, 2160))

    def test_all_scene_receipts_match_the_fixture_roster(self):
        for scene, (_, count) in bench.SCENES.items():
            with self.subTest(scene=scene):
                result, frames = bench.read_timing(events(scene), scene)
                self.assertEqual(result['actors_queried'], count)
                self.assertEqual(result['timing'], bench.stats([10.] * 1000, 10_000))
                self.assertEqual(frames, [10.] * 1000)
                if scene == 'ratchet':
                    self.assertEqual(result['sweeps']['right']['frames'], 500)
                    self.assertEqual(result['sweeps']['left']['frames'], 500)

    def test_incomplete_or_mismatched_runs_are_rejected(self):
        complete = events('ratchet')
        for index in (0, 1, 3, 6, -1):
            with self.subTest(removed=complete[index]):
                missing = complete.copy()
                del missing[index]
                with self.assertRaises(ValueError):
                    bench.read_timing(missing, 'ratchet')
        early_turn = complete.copy()
        early_turn[5] = early_turn[5].replace(':500:', ':400:')
        with self.assertRaisesRegex(ValueError, 'midpoint'):
            bench.read_timing(early_turn, 'ratchet')

    def test_worst_point_one_percent_rounds_up(self):
        values = [10.] * 999 + [22., 28.]
        timing = bench.stats(values, sum(values))
        self.assertEqual(timing['low_sample_count'], 2)
        self.assertEqual(timing['low_0_1_fps'], 40.)
        self.assertEqual(timing['worst_ms'], 28.)

    def test_both_focus_modes_reject_changes_during_timing(self):
        size = (3840, 2160)
        for foreground in (False, True):
            observations = [(foreground, size)] * 20
            report = {'focus_mode': 'foreground' if foreground else 'background'}
            bench.check_window(report, observations, size, foreground=foreground)
            self.assertEqual(report['foreground_samples'], 20 if foreground else 0)
            observations[-1] = (not foreground, size)
            with self.assertRaisesRegex(ValueError, f'foreground in {19 if foreground else 1}/20 samples'):
                bench.check_window(report, observations, size, foreground=foreground)

    def test_window_report_identifies_size_changes_and_disappearance(self):
        size = (3840, 2160)
        observations = [(False, size)] * 18 + [(False, (1920, 1080)), (False, None)]
        report = {'focus_mode': 'background'}
        with self.assertRaisesRegex(ValueError, 'missing in 1/20 samples.*expected client size 3840x2160, '
                                    'observed 1920x1080, 3840x2160'):
            bench.check_window(report, observations, size, foreground=False)

    def test_gameplay_messages_do_not_change_ratchet_pan_markers(self):
        say = struct.pack('<II', 1, 0)
        self.assertEqual(fixture.gameplay_message(say + b'NorthlightBench:arm\0'), 'arm')
        self.assertIsNone(fixture.gameplay_message(say + b'NorthlightBench:arm\n\0'))
        pan = say + b'NorthlightRatchet:left\0'
        self.assertIsNone(fixture.gameplay_message(pan))
        self.assertEqual(fixture.ratchet_camera_marker(pan), 'left')

    def test_benchmark_overrides_the_staged_quality_file(self):
        with (TemporaryDirectory(dir=bench.live.fp.output_dir()) as temporary,
              patch.object(bench.live, 'stage_pe'), patch.object(bench.live, 'link_asset')):
            root = Path(temporary)
            data = root / 'Data'
            (data / 'enGB').mkdir(parents=True)
            (data / 'enGB/locale-enGB.MPQ').touch()
            cache = root / 'world-cache/Northrend'
            cache.mkdir(parents=True)
            (cache / '0_0.fg3').write_bytes(b'FGS3' + bytes(8))
            for name in ('Wow.exe', 'd3d9.dll', 'northlight-assets.bin'):
                (root / name).touch()
            template = root / 'northlight-quality.ini'
            original = '[Quality]\nEffects=0\nDiagnostics=0\nRenderProfile=1\nDiagReplayProbe=1\n'
            template.write_text(original, encoding='ascii')
            args = Namespace(case='x86-dxvk', client=root, data=data, runtime=root, backend=root / 'd3d9.dll')
            for requested, effects in ((True, 1), (False, 0), (False, None)):
                with self.subTest(render_profile=requested, effects=effects):
                    args.render_profile, args.effects = requested, effects
                    client = root / str(effects)
                    bench.stage(args, 'crowd8', client, {'runtime': {}})
                    settings = dict(line.split('=') for line in
                                    (client / template.name).read_text(encoding='ascii').splitlines()[1:])
                    self.assertEqual(settings['RenderProfile'], str(int(requested)))
                    self.assertEqual(settings['Effects'], str(0 if effects is None else effects))
                    self.assertEqual(settings['Diagnostics'], str(int(requested)))
                    self.assertEqual(settings['DiagReplayProbe'], str(int(not requested)))
            self.assertEqual(template.read_text(encoding='ascii'), original)

    def test_effect_options_require_a_supported_northlight_d3d9_case(self):
        with patch.dict(bench.CASES, {'future-backend': bench.CASES['native-d3d9ex']}):
            for case in ('stock-x86', 'native-vulkan', 'future-backend'):
                for option in (('--effects', '0'), ('--effects', '1'), ('--render-profile',)):
                    argv = ['benchmark_gameplay.py', '--case', case, '--output', 'unused', *option]
                    with (self.subTest(case=case, option=option), patch.object(sys, 'argv', argv),
                          patch.object(sys, 'stderr', new_callable=StringIO) as stderr):
                        with self.assertRaises(SystemExit) as error:
                            bench.main()
                        self.assertEqual(error.exception.code, 2)
                        self.assertIn(f'{option[0]} requires a Northlight D3D9 case', stderr.getvalue())

    def test_effect_override_is_verified_against_the_renderer_log(self):
        with TemporaryDirectory(dir=bench.live.fp.output_dir()) as temporary:
            output = Path(temporary)
            log = output / bench.live.LOG
            factory = 'NORTHLIGHT native client: D3D9 factory attached\n'
            wrapped = factory + 'D3D9 device wrapped.\n'
            quality_off = 'QUALITY preset=Quality Effects=0(file)\n'
            quality_on = 'QUALITY preset=Quality Effects=1(file)\n'
            for effects, quality in ((0, quality_off), (1, quality_on)):
                log.write_text(wrapped + quality, encoding='ascii')
                args = Namespace(case='native-d3d9ex', effects=effects)
                self.assertTrue(bench.backend_evidence(args, output)['renderer_active'])

            args = Namespace(case='native-d3d9ex', effects=0)
            for lines in (wrapped, wrapped + quality_on, factory + quality_off,
                          wrapped + quality_off + 'SETTINGS applied device=1 key=Effects previous=0 value=1\n',
                          wrapped + quality_off + 'EXTENSION fault frame=2\n'):
                with self.subTest(log=lines):
                    log.write_text(lines, encoding='ascii')
                    with self.assertRaisesRegex(ValueError, 'Effects=0'):
                        bench.backend_evidence(args, output)

    def test_native_sdr_reports_passthrough_and_hdr_requires_gpu_transfer(self):
        with TemporaryDirectory() as temporary:
            output = Path(temporary)
            log = output / bench.live.LOG
            factory = 'NORTHLIGHT native client: D3D9 factory attached\n'
            fallback = 'X64 D3D9 passthrough: required effect capabilities unavailable\n'
            log.write_text(factory + fallback, encoding='ascii')
            evidence = bench.backend_evidence(Namespace(case='native-d3d9ex'), output)
            self.assertFalse(evidence['renderer_active'])
            self.assertEqual(evidence['passthrough'], fallback.strip())
            args = Namespace(case='native-d3d9ex-hdr')
            with self.assertRaisesRegex(ValueError, 'scRGB'):
                bench.backend_evidence(args, output)
            log.write_text(factory + 'D3D9 device wrapped.\nHDR experimental requested=1 active=1 mode=scRGB\n'
                           'HDR native DXGI Present frame=2 color=scRGB transfer=D3D9On12 GPU\n', encoding='ascii')
            self.assertTrue(bench.backend_evidence(args, output)['renderer_active'])

            args.render_profile = True
            with self.assertRaisesRegex(ValueError, 'RenderProfile'):
                bench.backend_evidence(args, output)
            with log.open('a', encoding='ascii') as stream:
                stream.write('PROFILE effective diagnostics=1 renderProfile=1 replayProbe=0 period=127 cpuGpuAdditive=0\n')
            self.assertTrue(bench.backend_evidence(args, output)['renderer_active'])

    def test_stock_runtime_log_records_bits_and_backend(self):
        with TemporaryDirectory() as temporary:
            output = Path(temporary)
            report = {'runtime': {'case': 'stock-x86', 'client_executable': 'Wow.exe', 'client_bits': 32,
                                  'window_class': 'GxWindowClassD3d', 'd3d9on12_loaded': False}}
            bench.write_runtime(output, report)
            text = (output / 'client-runtime.log').read_text(encoding='utf-8')
            for marker in ('client_bits=32', 'renderer=D3D9', 'dxvk=off', 'd3d9on12=off', 'northlight=off'):
                self.assertIn(marker + '\n', text)

    def test_dxvk_report_uses_live_quality_and_component_switches(self):
        lines = ['BACKEND selected=backend_d3d9.dll runtime=v3.1.1 rules=dxvk BackendPath=backend_d3d9.dll fallback=0',
                 'QUALITY preset=Quality Effects=1(default) GI=1(file) WorldShadows=1(default) ContactAO=1(default)',
                 'D3D9 device wrapped.', 'Effects components tick=30 master=1 GI=OFF shadows=ON fog=ON',
                 'SETTINGS applied device=1 key=ContactAO previous=1 value=0 boundary=after-frame-cleanup']
        observed = {'case': 'x64-dxvk', 'client_bits': 64, 'window_class': 'GxWindowClassD3d', 'd3d9on12_loaded': False}
        result = runtime.evidence(observed, lines, [], [], [], [])
        self.assertEqual(result['dxvk_version'], 'v3.1.1')
        self.assertEqual(result['northlight'], 'effects-enabled')
        self.assertIs(result['effects']['GI'], False)
        self.assertIs(result['effects']['WorldShadows'], True)
        self.assertIs(result['effects']['ContactAO'], False)
        self.assertIs(result['effects']['Weather'], None)

    def test_dxvk_version_ignores_later_driver_messages_and_system_fallback(self):
        driver = ['info:  DXVK: v3.1.1', 'info:  DXVK: Using 32 compiler threads']
        observed = {'case': 'x64-dxvk', 'window_class': 'GxWindowClassD3d'}
        result = runtime.evidence(observed, [], [], [], driver, [])
        self.assertEqual((result['dxvk'], result['dxvk_version']), ('active', 'v3.1.1'))
        selected = ['BACKEND selected=System32/d3d9.dll runtime=non-DXVK rules=native BackendPath=(default) fallback=1']
        result = runtime.evidence(observed, selected, [], [], driver, [])
        self.assertEqual((result['dxvk'], result['dxvk_version']), ('off', None))

    def test_d3d9on12_requires_transfer_evidence_to_report_active(self):
        observed = {'window_class': 'GxWindowClassD3d9Ex', 'd3d9on12_loaded': True}
        lines = ['X64 D3D9 passthrough: required effect capabilities unavailable']
        result = runtime.evidence(observed, lines, [], [], [], [])
        self.assertEqual((result['renderer'], result['d3d9on12'], result['northlight']), ('D3D9Ex', 'loaded', 'passthrough'))
        lines = ['HDR experimental requested=1 active=1 stream=0',
                 'HDR native DXGI Present frame=2 color=scRGB transfer=D3D9On12 GPU']
        result = runtime.evidence(observed, lines, [], [], [], [])
        self.assertEqual((result['presentation'], result['d3d9on12']), ('HDR scRGB', 'active'))

    def test_vulkan_report_prefers_in_game_status_and_preserves_unavailability(self):
        observed = {'window_class': 'GxWindowClassVulkan'}
        gx = ['11/26 10:42:15.103  Northlight Vulkan bloom: active']
        result = runtime.evidence(observed, [], gx, [], [], ['BENCH_EFFECTS active:disabled'])
        self.assertEqual(result['effects'], {'ContactAO': 'active', 'LocalColorBounce': 'active', 'Bloom': 'disabled'})
        result = runtime.evidence(observed, [], [], [], [], ['BENCH_EFFECTS unavailable:unavailable'])
        self.assertEqual(result['northlight'], 'unavailable')
        self.assertEqual(result['d3d9on12'], 'unknown')


if __name__ == '__main__':
    unittest.main()
