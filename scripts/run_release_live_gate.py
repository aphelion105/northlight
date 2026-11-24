"""Verify a Windows ZIP and exercise its exact renderer payload in private WoW."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import zipfile

import run_northrend_baseline as baseline
from build_renderer import source_identity
from build_renderer_assets import verify_pair


ROOT = Path(__file__).resolve().parents[1]
BACKENDS = {'dxvk': 'renderer-backends/dxvk/dxvk_d3d9.dll',
            'dxvk2': 'renderer-backends/dxvk2/dxvk2_d3d9.dll'}
PROFILES = ('northlight-quality.ini', 'celestial-profiles.ini', 'shadow-range-profiles.ini')
REQUIRED = ('d3d9.dll', 'northlight-assets.bin', *PROFILES, *BACKENDS.values())
ATMOSPHERE = 'forever-atmosphere.bin'
CONFIG = 'northlight-renderer.ini'
CASES = (('menu-dxvk', 'menu', 'dxvk'), ('menu-dxvk2', 'menu', 'dxvk2'),
         ('dalaran-dxvk', 'world', 'dxvk'), ('dalaran-dxvk2', 'world', 'dxvk2'),
         ('menu-native', 'menu', 'native'), ('dalaran-native', 'world', 'native'))
NATIVE_CAPS = ('INTZ', 'RESZ', 'floatRT', 'SM3')


def verify_package(package, run):
    original = baseline.digest(package)
    verification = run / 'package-verification'
    verification.mkdir()
    env = dict(os.environ, NORTHLIGHT_TEST_OUTPUT_DIR=str(verification), PYTHONIOENCODING='utf-8')
    command = [sys.executable, '-u', str(ROOT / 'tests/verify_packages.py'), str(package)]
    with (verification / 'stdout.log').open('wb') as stdout, (verification / 'stderr.log').open('wb') as stderr:
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=stdout, stderr=stderr, timeout=120)
    if result.returncode:
        raise ValueError('Windows ZIP verification failed; see package-verification')
    receipt_path = verification / 'verification.json'
    reports = json.loads(receipt_path.read_bytes())['packages']
    if (len(reports) != 1 or reports[0].get('platform') != 'windows' or reports[0].get('problems') != [] or
            reports[0].get('sha256') != original or baseline.digest(package) != original):
        raise ValueError('The verified Windows ZIP receipt differs from the requested archive')
    return original, receipt_path


def extract_payload(package, target, archive_hash, source):
    # The verifier checks the archive structure and every payload member. The
    # gate stages only the named runtime files and rechecks their bytes here.
    baseline.fp.use_source_modules()
    from build_packages import check_dll

    with zipfile.ZipFile(package) as archive:
        info_name, = (name for name in archive.namelist() if name.endswith('/BUILD-INFO.json'))
        root = info_name.rsplit('/', 1)[0]
        info = json.loads(archive.read(info_name))
        manifest = json.loads(archive.read(f'{root}/payload-manifest.json'))
        hashes = {entry['path']: entry['sha256'] for entry in manifest}
        if info.get('platform') != 'windows' or len(hashes) != len(manifest):
            raise ValueError('Invalid Windows runtime payload manifest')
        names = (*REQUIRED, ATMOSPHERE) if ATMOSPHERE in hashes else REQUIRED
        paths = {}
        for name in names:
            data = archive.read(f'{root}/payload/{name}')
            if hashlib.sha256(data).hexdigest() != hashes.get(name):
                raise ValueError(f'Payload member changed after ZIP verification: {name}')
            path = target / name
            path.parent.mkdir(parents=True, exist_ok=True)
            with path.open('xb') as output:
                output.write(data)
            paths[name] = path
    candidate = paths['d3d9.dll'].read_bytes()
    try:
        check_dll(candidate)
    except SystemExit as error:
        raise ValueError(str(error)) from error
    if source.encode('ascii') + b'\0' not in candidate:
        raise ValueError('The package renderer must be built from the current source tree')
    assets = verify_pair(candidate, paths['northlight-assets.bin'].read_bytes())
    if assets != hashes['northlight-assets.bin'] or baseline.digest(package) != archive_hash:
        raise ValueError('The ZIP or its DLL/assets pair changed while staging')
    return paths, {name: hashes[name] for name in names}, info['version']


def launch_profile(text, backend):
    profile = re.sub(r'(?im)^[ \t]*BackendPath[ \t]*=[^\r\n]*(?:\r?\n|$)', '', text)
    profile, count = re.subn(r'(?im)^[ \t]*Backend[ \t]*=[^\r\n]*', f'Backend={backend}', profile)
    if count != 1:
        raise ValueError('The private renderer profile needs exactly one Backend setting')
    return profile


def world_quality(text):
    if not re.search(r'(?im)^\[Quality\][ \t]*$', text):
        raise ValueError('The package is missing its Quality profile')
    text = re.sub(r'(?im)^[ \t]*Diagnostics[ \t]*=[^\r\n]*(?:\r?\n|$)', '', text)
    return re.sub(r'(?im)^\[Quality\][ \t]*$', '[Quality]\nDiagnostics=1', text, count=1)


def parse_native_capabilities(text):
    match = re.fullmatch(r'NATIVE_D3D9_CAPS adapter=0 vendor=([0-9a-f]{4,8}) device=([0-9a-f]{4,8}) '
                         r'INTZ=([01]) RESZ=([01]) floatRT=([01]) SM3=([01])\s*', text)
    if not match:
        raise ValueError('System D3D9 capability probe did not return a complete result')
    vendor, device, *bits = match.groups()
    caps = {name: bit == '1' for name, bit in zip(NATIVE_CAPS, bits, strict=True)}
    return {'status': 'SUPPORTED' if all(caps.values()) else 'UNSUPPORTED', 'adapter': 0,
            'vendor': vendor, 'device': device, 'capabilities': caps}


def qualify_native(run):
    folder = run / 'native-qualification'
    folder.mkdir()
    probe = folder / 'd3d9-caps.exe'
    command = [baseline.fp.zig(), 'c++', '-target', 'x86-windows-gnu', '-O2', '-std=c++17',
               ROOT / 'tests/windows_gpu_probe.cpp', '-luser32', '-o', probe]
    if baseline.run_logged(command, folder, baseline.fp.zig_env(), 'compile', 120):
        raise ValueError('Failed to build the native D3D9 capability probe')
    if baseline.run_logged([probe, '--native-capabilities'], folder, baseline.child_environment(folder), 'probe', 30):
        raise ValueError('System D3D9 did not complete its capability probe')
    observed = parse_native_capabilities((folder / 'probe.stdout').read_text(encoding='ascii'))
    observed['probe_sha256'] = baseline.digest(probe)
    return observed


def validate_live(report, expected, source, client, backend, target, video, *, background=False):
    evidence = report.get('observation', {})
    selected = evidence.get('backend_path')
    if (report.get('status') != 'PASS' or report.get('exit_code') != 0 or
            report.get('forced_shutdown') is not False or report.get('crash_files') or
            evidence.get('identity') != source or evidence.get('assets') != expected['northlight-assets.bin'] or
            evidence.get('quality_problems') != 0):
        raise ValueError(f'{target} did not validate the packaged renderer and quality profile')
    if backend == 'native':
        windows = Path(os.environ['WINDIR'])
        allowed = {(windows / system / 'd3d9.dll').resolve() for system in ('System32', 'SysWOW64')}
        if evidence.get('backend') != 'non-DXVK':
            raise ValueError(f'{target} did not report the native D3D9 runtime')
    else:
        allowed = {(client / BACKENDS[backend]).resolve()}
    if not selected or evidence.get('backend_fallback') is not False or Path(selected).resolve() not in allowed:
        raise ValueError(f'{target} did not load the selected {backend} from its expected path')
    staged, after = report.get('runtime_inputs', {}), report.get('runtime_payload_after', {})
    if any(name not in staged or staged[name] != digest or name not in after or after[name] != digest
           for name, digest in expected.items()):
        raise ValueError(f'{target} runtime payload differs from the verified package launch')
    mode = 'sdr' if backend == 'native' else 'hdr'
    if baseline.measured_output(evidence, mode) != video:
        raise ValueError(f'{target} did not produce the requested {mode.upper()} resolution and MSAA')
    if background and (report.get('window_mode') != 'background' or report.get('foreground_attempts', 0)):
        raise ValueError(f'{target} did not run in the requested background mode')
    if target == 'menu':
        ready = report.get('checkpoint') == 'menu-character-ready' and evidence.get('menu_seen') is True
    else:
        crowd = report.get('crowd', {})
        windows = report.get('windows', ())
        timing = report.get('frame_timing', {})
        warm = (bool(windows) and all(window.get('focus') == '0' for window in windows) and
                timing.get('window_mode') == 'background' and timing.get('state') == 'measured' and
                timing.get('samples', 0) >= 120) if background else any(window.get('focus') == '1' for window in windows)
        ready = (report.get('checkpoint') == 'northrend-static-ready' and evidence.get('world_seen') is True and
                 evidence.get('gi_tick') and evidence.get('static_tick') and crowd.get('ready') is True and
                 crowd.get('players') == crowd.get('client_name_requests') == 128 and
                 warm)
    if not ready:
        raise ValueError(f'{target} is missing its verified scene checkpoint')


def gate(args):
    root, run = args.stage.resolve(), args.output.resolve()
    if run == root or not run.is_relative_to(root) or run.is_relative_to(root / 'client'):
        raise ValueError('Put release gate results under the private stage outside client/')
    run.mkdir(parents=True, exist_ok=False)
    background = getattr(args, 'background', False)
    report = {'format': 1, 'status': 'INCOMPLETE', 'fps_eligible': False, 'cases': [],
              'window_mode': 'background' if background else 'focused'}
    try:
        package = args.package.resolve()
        source = source_identity()
        archive_hash, receipt = verify_package(package, run)
        paths, hashes, version = extract_payload(package, run / 'payload', archive_hash, source)
        manifest = json.loads((root / 'baseline.json').read_bytes())
        video = {key: manifest[key] for key in ('resolution', 'msaa')}
        report.update(source_identity=source, package_sha256=archive_hash, package_version=version,
                      package_verification_sha256=baseline.digest(receipt), payload_sha256=hashes,
                      baseline_sha256=baseline.digest(root / 'baseline.json'))
        renderer = (root / CONFIG).read_text(encoding='utf-8-sig')
        for name, target, backend in CASES:
            output_mode = 'sdr' if backend == 'native' else 'hdr'
            if backend == 'native':
                if 'native_qualification' not in report:
                    report['native_qualification'] = qualify_native(run)
                if report['native_qualification']['status'] == 'UNSUPPORTED':
                    report['cases'].append({'name': name, 'backend': backend, 'output_mode': output_mode,
                                            'status': 'UNSUPPORTED', 'capability_log': 'native-qualification/probe.stdout'})
                    continue
            config = run / (name + '-renderer.ini')
            config.write_text(launch_profile(renderer, backend), encoding='utf-8')
            runtime = {key: paths[key] for key in (*PROFILES, *BACKENDS.values())}
            runtime.update({ATMOSPHERE: paths.get(ATMOSPHERE), CONFIG: config})
            expected = {key: hashes.get(key) for key in runtime}
            expected[CONFIG] = baseline.digest(config)
            if target == 'world':
                quality = run / 'world-quality.ini'
                data = paths['northlight-quality.ini'].read_bytes()
                if hashlib.sha256(data).hexdigest() != hashes['northlight-quality.ini']:
                    raise ValueError('The verified package quality profile changed while staging World')
                quality.write_text(world_quality(data.decode('utf-8-sig').replace('\r\n', '\n')), encoding='utf-8')
                runtime['northlight-quality.ini'] = quality
                expected['northlight-quality.ini'] = baseline.digest(quality)
            expected.update({key: hashes[key] for key in ('d3d9.dll', 'northlight-assets.bin')})
            path = run / name
            result = baseline.run_client(argparse.Namespace(stage=root, output=path, target=target,
                scene='crowd' if target == 'world' else 'solo', windows=int(target == 'world'),
                timeout=args.live_timeout, manual=False, capture_wrapper=None, package_runtime=runtime, background=background,
                keep_work=getattr(args, 'keep_work', False),
                live_dll=paths['d3d9.dll'], live_assets=paths['northlight-assets.bin'], output_mode=output_mode))
            live_path = path / 'validation.json'
            live = json.loads(live_path.read_bytes())
            report['cases'].append({'name': name, 'backend': backend, 'output_mode': output_mode, 'report': f'{name}/validation.json',
                                    'sha256': baseline.digest(live_path), 'status': live['status']})
            if 'dxvk_adapter' in live:
                report['cases'][-1]['dxvk_adapter'] = live['dxvk_adapter']
            if result:
                raise ValueError(f'{name} did not complete; see {name}/validation.json')
            if live['baseline_sha256'] != report['baseline_sha256']:
                raise ValueError('Private stage manifest changed between release gate cases')
            validate_live(live, expected, source, root / 'client', backend, target, video, background=background)
        if source_identity() != source or baseline.digest(package) != archive_hash:
            raise ValueError('Renderer source or package changed during the live gate')
        report['status'] = 'PASS'
    except (OSError, ValueError, KeyError, RuntimeError, zipfile.BadZipFile, subprocess.SubprocessError) as error:
        report['error'] = str(error)
    finally:
        baseline.write_json(run / 'validation.json', report)
    print(f'{report["status"]}: {run / "validation.json"}', flush=True)
    return 0 if report['status'] == 'PASS' else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('package', 'stage', 'output'):
        parser.add_argument('--' + name, required=True, type=Path)
    parser.add_argument('--live-timeout', type=int, choices=range(5, 301), default=120, metavar='SECONDS')
    parser.add_argument('--background', action='store_true', help='Qualify packaged output and a background World timing interval')
    parser.add_argument('--keep-work', action='store_true', help='Retain disposable client state from each case')
    try:
        return gate(parser.parse_args())
    except (OSError, ValueError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    sys.exit(main())
