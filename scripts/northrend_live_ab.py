"""Compare live crowd optimizations on one verified Windows package."""

import argparse
from collections import defaultdict
import json
import math
import os
from pathlib import Path
import re
from statistics import median

import run_northrend_baseline as baseline
import run_release_live_gate as release


PROFILE_ROWS = (
    ('GPU frame', 'frame'), ('GPU profile', 'frame'), ('CPU profile', 'frame'),
    ('GAME_INDEXED_PAIR', 'frame'), ('GAME_PARTICLE_PAIR', 'frame'),
    ('MODEL_NORMALS palette instances', 'frame'), ('CSTREAM QUERYFRAME', 'gameFrame'),
    ('CSTREAM EVENT_CPU', 'gameFrame'), ('CSTREAM EVENT_QUEUE', 'gameFrame'),
    ('MEMORY', 'frame'),
)
NUMBER = re.compile(r'(\w+)=(-?\d+(?:\.\d+)?)(?:ms)?(?=\s|$)')


def samples(lines, windows):
    rows = defaultdict(lambda: defaultdict(list))
    for line in lines:
        for section, key in PROFILE_ROWS:
            if not line.startswith(f'{section} {key}='):
                continue
            fields = {name: float(value) for name, value in NUMBER.findall(line)}
            frame = int(fields.pop(key))
            if not any(window['first_present'] <= frame <= window['last_present'] for window in windows):
                break
            if section == 'CSTREAM QUERYFRAME':
                section += f' type={int(fields.pop("type"))}'
            for name, value in fields.items():
                rows[section][name].append(value)
            break
    return {section: {field: {'count': len(values), 'min': min(values), 'median': median(values), 'max': max(values)}
                      for field, values in fields.items()}
            for section, fields in rows.items()}


def live_quality(text):
    text = release.world_quality(text)
    text = re.sub(r'(?im)^[ \t]*RenderProfile[ \t]*=[^\r\n]*(?:\r?\n|$)', '', text)
    return re.sub(r'(?im)^\[Quality\][ \t]*$', '[Quality]\nRenderProfile=1', text, count=1)


def switch_observed(profile, feature, enabled):
    section, field = {
        'gamescene': ('GAME_INDEXED_PAIR', 'saved'),
        'particles': ('GAME_PARTICLE_PAIR', 'saved'),
        'event': ('CSTREAM EVENT_CPU', 'shortGpuTrips'),
    }[feature]
    maximum = profile.get(section, {}).get(field, {}).get('max', 0)
    return (maximum > 0) == enabled and (feature != 'event' or section in profile)


def compare(arms):
    metrics = ('p50_ms', 'p95_ms', 'p99_ms')
    result = {}
    for metric in metrics:
        off1, on1, on2, off2 = [arm['timing'][metric] for arm in arms]
        result[metric] = {'on_minus_off': [on1 - off1, on2 - off2],
                          'repeat_spread': {'off': abs(off1 - off2), 'on': abs(on1 - on2)}}
    return result


def run(args):
    stage, output, package = (path.resolve() for path in (args.stage, args.output, args.package))
    if output == stage or not output.is_relative_to(stage) or output.is_relative_to(stage / 'client'):
        raise ValueError('Put A/B results under the private stage, outside client/')
    output.mkdir(parents=True, exist_ok=False)
    background = getattr(args, 'background', False)
    report = {'status': 'INCOMPLETE', 'feature': args.feature, 'arms': [],
              'window_mode': 'background' if background else 'focused'}
    try:
        source = release.source_identity()
        archive_hash, verification = release.verify_package(package, output)
        payload, hashes, version = release.extract_payload(package, output / 'payload', archive_hash, source)
        report.update(source_identity=source, package_sha256=archive_hash, package_version=version,
                      package_verification_sha256=baseline.digest(verification))
        manifest = json.loads((stage / 'baseline.json').read_bytes())
        video = {key: manifest[key] for key in ('resolution', 'msaa')}
        config = output / 'renderer.ini'
        config.write_text(release.launch_profile((stage / release.CONFIG).read_text(encoding='utf-8'), 'dxvk'),
                          encoding='utf-8')
        quality = output / 'quality.ini'
        quality.write_text(live_quality(payload['northlight-quality.ini'].read_text(encoding='utf-8-sig')),
                           encoding='utf-8')
        runtime = {key: payload[key] for key in (*release.PROFILES, *release.BACKENDS.values())}
        runtime.update({release.ATMOSPHERE: payload.get(release.ATMOSPHERE), release.CONFIG: config,
                        'northlight-quality.ini': quality})
        expected = {key: baseline.digest(path) if path is not None else None for key, path in runtime.items()}
        expected.update({key: hashes[key] for key in ('d3d9.dll', 'northlight-assets.bin')})
        feature_switch = {'gamescene': 'NORTHLIGHT_GAME_PALETTE', 'particles': 'NORTHLIGHT_GAME_PARTICLE_TEXTURE_PAIRS',
                          'event': 'NORTHLIGHT_EVENT_ADAPTIVE'}[args.feature]
        for name, enabled in (('off1', '0'), ('on1', '1'), ('on2', '1'), ('off2', '0')):
            flags = {'NORTHLIGHT_NORMAL_PALETTE': '1', 'NORTHLIGHT_GAME_PALETTE': args.game_scene,
                     'NORTHLIGHT_GAME_PARTICLE_PAIRS': '0', 'NORTHLIGHT_GAME_PARTICLE_ATLAS': '0',
                     'NORTHLIGHT_GAME_PARTICLE_TEXTURE_PAIRS': args.particles, 'NORTHLIGHT_EVENT_ADAPTIVE': '0'}
            flags[feature_switch] = enabled
            os.environ.update(flags)
            path = output / name
            result = baseline.run_client(argparse.Namespace(stage=stage, output=path, target='world',
                scene='crowd', windows=args.windows, timeout=args.timeout, manual=False, capture_wrapper=None, background=background,
                keep_work=getattr(args, 'keep_work', False),
                package_runtime=runtime, live_dll=payload['d3d9.dll'], live_assets=payload['northlight-assets.bin']))
            receipt = path / 'validation.json'
            live = json.loads(receipt.read_bytes())
            arm = {'name': name, 'flags': flags, 'report': str(receipt), 'state': live['status']}
            report['arms'].append(arm)
            if result:
                raise ValueError(f'{name} did not complete: {live.get("error", live["status"])}')
            release.validate_live(live, expected, source, stage / 'client', 'dxvk', 'world', video, background=background)
            if live['baseline_sha256'] != baseline.digest(stage / 'baseline.json'):
                raise ValueError('The private stage changed during A/B')
            timing = live['frame_timing']
            lines = baseline.log_lines(path / baseline.LOG)
            profile = samples(lines, timing['windows'])
            if timing.get('state') != 'measured' or 'GPU frame' not in profile:
                raise ValueError(f'{name} did not provide raw Present timings and pass samples')
            normal = profile.get('MODEL_NORMALS palette instances', {}).get('enabled', {})
            if normal.get('median') != 1 or normal['max'] != 1:
                raise ValueError(f'{name} did not measure ModelNormals=1')
            camera = live['observation']['camera']
            arm.update(timing=timing, profile=profile, crowd=live['crowd'], camera=camera,
                       dxvk_adapter=live.get('dxvk_adapter'),
                       configured_background_fps_cap=live.get('configured_background_fps_cap'),
                       resets=baseline.observed_reset_frames(lines), readiness_ms=live.get('readiness_ms'),
                       receipt_sha256=baseline.digest(receipt))
            if not switch_observed(profile, args.feature, enabled == '1'):
                raise ValueError(f'{name} did not exercise the requested {args.feature} arm')
        reference = report['arms'][0]
        for arm in report['arms'][1:]:
            if ((arm['dxvk_adapter'] or {}).get('device') !=
                    (reference['dxvk_adapter'] or {}).get('device')):
                raise ValueError('All four arms must use the same DXVK adapter')
            if (arm['crowd']['spawn_sha256'] != reference['crowd']['spawn_sha256'] or
                    math.dist(arm['camera']['eye'], reference['camera']['eye']) > .25 or
                    math.dist(arm['camera']['forward'], reference['camera']['forward']) > .002):
                raise ValueError('The same crowd and camera are required in all four arms')
        report.update(status='PASS', differences=compare(report['arms']))
    except (OSError, ValueError, KeyError, RuntimeError) as error:
        report['error'] = str(error)
    finally:
        baseline.write_json(output / 'comparison.json', report)
    print(f'{report["status"]}: {output / "comparison.json"}')
    return 0 if report['status'] == 'PASS' else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('stage', 'output', 'package'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--feature', choices=('gamescene', 'particles', 'event'), required=True)
    parser.add_argument('--game-scene', choices=('0', '1'), default='1')
    parser.add_argument('--particles', choices=('0', '1'), default='0')
    parser.add_argument('--windows', type=int, choices=range(3, 11), default=5)
    parser.add_argument('--background', action='store_true', help='Keep the test client in the background for all four arms')
    parser.add_argument('--keep-work', action='store_true', help='Retain disposable client state from each arm')
    parser.add_argument('--timeout', type=int, choices=range(30, 301), default=180)
    return run(parser.parse_args())


if __name__ == '__main__':
    raise SystemExit(main())
