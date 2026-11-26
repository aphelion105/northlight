#!/usr/bin/env python3
# northlight-test:
"""Check the gameplay benchmark's roster, timing and backend evidence gates."""
from argparse import Namespace
from pathlib import Path
import struct
import sys
from tempfile import TemporaryDirectory
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import benchmark_gameplay as bench
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

    def test_gameplay_messages_do_not_change_ratchet_pan_markers(self):
        say = struct.pack('<II', 1, 0)
        self.assertEqual(fixture.gameplay_message(say + b'NorthlightBench:arm\0'), 'arm')
        self.assertIsNone(fixture.gameplay_message(say + b'NorthlightBench:arm\n\0'))
        pan = say + b'NorthlightRatchet:left\0'
        self.assertIsNone(fixture.gameplay_message(pan))
        self.assertEqual(fixture.ratchet_camera_marker(pan), 'left')

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


if __name__ == '__main__':
    unittest.main()
