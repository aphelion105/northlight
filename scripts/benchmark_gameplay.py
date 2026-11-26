"""Measure the same live Dalaran and Ratchet workloads on Windows backends."""

import argparse
from contextlib import ExitStack
import csv
import ctypes
from ctypes import wintypes
import math
import os
from pathlib import Path
import re
import shutil
from statistics import mean
import subprocess
import sys
import time

import run_northrend_baseline as live
import northrend_fixture_server as fixture

SCENES = {
    'crowd8': ('crowd', fixture.SMALL_BENCHMARK_CROWD),
    'crowd128': ('crowd', fixture.DEFAULT_CROWD_SIZE),
    'crowd512': ('m2-stress', fixture.M2_STRESS_SIZE),
    'ratchet': (fixture.RATCHET_PAN, fixture.RATCHET_CROWD_SIZE),
}
CASES = {
    'stock-x86': ('Wow.exe', '', 'GxWindowClassD3d'),
    'stock-x64': ('WowClient64.exe', '-d3d9', 'GxWindowClassD3d'),
    'x86-dxvk': ('Wow.exe', '', 'GxWindowClassD3d'),
    'x64-dxvk': ('WowClient64.exe', '-d3d9', 'GxWindowClassD3d'),
    'native-d3d9ex': ('WowClientNorthlight64.exe', '-d3d9ex', 'GxWindowClassD3d9Ex'),
    'native-d3d9ex-hdr': ('WowClientNorthlight64.exe', '-d3d9ex', 'GxWindowClassD3d9Ex'),
    'native-vulkan': ('WowClientNorthlight64.exe', '-vulkan', 'GxWindowClassVulkan'),
}
ADDON = 'NorthlightGameplayBenchmark'
FIXTURES = Path(__file__).with_name('fixtures') / 'gameplay-benchmark'
RUNTIME_FILES = ('northlight-assets.bin', 'northlight-quality.ini', 'celestial-profiles.ini',
                 'shadow-range-profiles.ini', 'water-core.dll', 'water.ini', 'water-host.ini')
LOGS = (live.LOG, 'water-core.log', 'Logs/gx.log', 'Logs/vulkan.log', 'Logs/wow_client.log', 'Logs/GlueXML.log',
        'Wow_d3d9.log', 'WowClient64_d3d9.log')


def stats(frames, elapsed_ms):
    slowest = sorted(frames, reverse=True)[:math.ceil(len(frames) / 1000)]
    return {'avg_fps': len(frames) * 1000 / elapsed_ms, 'low_0_1_fps': 1000 / mean(slowest),
            'low_sample_count': len(slowest), 'frames': len(frames), 'window_seconds': elapsed_ms / 1000,
            'worst_ms': slowest[0]}


def read_timing(events, scene):
    fixture_scene, count = SCENES[scene]
    roster = fixture.scene_roster(fixture_scene, count)
    spawn = fixture.spawn_record(roster, b''.join(fixture.crowd_packets(roster)))
    arm = f'BENCH_ARM actors={count}'
    if events.count(spawn) != 1 or events.count(arm) != 1 or events.index(spawn) >= events.index(arm):
        raise ValueError('The loopback server did not arm the expected equipped roster')
    evidence = [line for line in events[events.index(arm) + 1:] if line.startswith(('BENCH phase=', 'BENCH_PAYLOAD '))]
    phases = ('start', 'right', 'done') if scene == 'ratchet' else ('start', 'done')
    ticks = {}
    for phase, line in zip(phases, evidence):
        marker = re.fullmatch(rf'BENCH phase={phase} tick_ns=(\d+)', line)
        if not marker:
            raise ValueError(f'Missing or reordered phase: {phase}')
        ticks[phase] = int(marker[1])
    if (len(ticks) != len(phases) or sorted(set(ticks.values())) != list(ticks.values()) or
            not 9.8e9 <= ticks['done'] - ticks['start'] <= 10.6e9):
        raise ValueError('Invalid timing markers for the ten-second window')
    payload = evidence[len(phases):]
    header = re.fullmatch(r'BENCH_PAYLOAD header:(\d+):(\d+):(\d+\.\d+)', payload[0]) if payload else None
    if not header or payload[-1] != 'BENCH_PAYLOAD complete':
        raise ValueError('Incomplete game-frame stream')
    expected, turn = map(int, header.groups()[:2])
    elapsed = float(header[3])
    frames = []
    for line in payload[1:-1]:
        batch = re.fullmatch(r'BENCH_PAYLOAD data:(\d+):(\d+\.\d+(?:,\d+\.\d+)*)', line)
        if not batch or int(batch[1]) != len(frames) + 1:
            raise ValueError('Missing, invalid or reordered game-frame batch')
        frames.extend(float(value) for value in batch[2].split(','))
    if (not frames or len(frames) != expected or min(frames) <= 0 or
            not 9999 <= elapsed <= 10400 or abs(elapsed - sum(frames)) > 5):
        raise ValueError('Frame count or elapsed time differs from the timing payload')
    report = {'roster_sha256': spawn.rsplit('=', 1)[1], 'actors_queried': count,
              'phase_tick_ns': ticks, 'timing': stats(frames, elapsed)}
    if scene == 'ratchet':
        if not 0 < turn < len(frames) or not 4900 <= sum(frames[:turn]) <= 5400:
            raise ValueError('Ratchet rotation did not reverse around the midpoint')
        report['sweeps'] = {name: stats(part, sum(part)) for name, part in
                            (('left', frames[:turn]), ('right', frames[turn:]))}
    elif turn:
        raise ValueError('A stationary scene reported a rotation')
    return report, frames


def stage(args, scene, client, report):
    game, _, _ = CASES[args.case]
    stock = args.case.startswith('stock-')
    proxy = args.case.endswith('-dxvk')
    native = args.case.startswith('native-')
    runtime = args.runtime
    base_game = game if not native else 'Wow.exe'
    scene_id, _ = SCENES[scene]
    world = fixture.map_for(scene_id)[2]
    tiles = live.RATCHET_TILES if scene == 'ratchet' else None
    if not (args.data / 'enGB/base-enGB.MPQ').is_file():
        raise ValueError('The benchmark needs the enGB 3.3.5a client archives')
    files = {}
    for relative in live.copies(args.client, None if stock else world, base_game, tiles, data=args.data):
        if stock and relative.parts[0] != 'Data' and relative.name not in (
                game, 'Wow.exe', 'WardenWorker32.exe', 'Battle.net.dll', 'DivxDecoder.dll',
                'XvidDecoder.dll', 'msvcr80.dll', 'Microsoft.VC80.CRT.manifest'):
            continue
        if native and relative.name == base_game:
            continue
        files[relative] = args.data / relative.relative_to('Data') if relative.parts[0] == 'Data' else args.client / relative
    if not stock:
        if not any(path.parts[:2] == ('world-cache', world) and path.suffix == '.fg3' for path in files):
            raise ValueError(f'The benchmark needs the installed {world} static geometry cache')
        for name in (*RUNTIME_FILES, game if native else 'd3d9.dll'):
            path = runtime / name
            if path.is_file() or name in ('northlight-assets.bin', 'd3d9.dll', game):
                files[Path(name)] = path
    if proxy:
        files[Path('backend_d3d9.dll')] = args.backend
    if Path(game) not in files:
        raise ValueError(f'Missing {game} in the input client')
    for name in (game, 'd3d9.dll', 'backend_d3d9.dll'):
        if Path(name) in files:
            live.stage_pe(files[Path(name)], 'Wow.exe' if game == 'Wow.exe' else 'WowClient64.exe')

    inputs = report['runtime_sha256'] = {}
    for relative, source in files.items():
        target = client / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if live.shared_asset(relative):
            live.link_asset(source, target)
        else:
            shutil.copyfile(source, target)
            inputs[relative.as_posix()] = live.digest(target)

    if not stock:
        config = '[Renderer]\nHDRPipelineVersion=2\nSettingsOverlay=0\nDiagnostics=0\n'
        config += f'ExperimentalHDR={int(args.case.endswith("-hdr"))}\n'
        if proxy:
            config += 'Backend=dxvk\nBackendPath=backend_d3d9.dll\n'
            (client / 'dxvk.conf').write_text('d3d9.presentInterval = 0\ndxvk.allowFse = False\n'
                                             'd3d9.enumerateByDisplays = False\n', encoding='ascii')
            inputs['dxvk.conf'] = live.digest(client / 'dxvk.conf')
        renderer = client / 'northlight-renderer.ini'
        renderer.write_text(config, encoding='ascii')
        inputs[renderer.name] = live.digest(renderer)
        quality = client / 'northlight-quality.ini'
        template = quality if quality.is_file() else live.fp.RENDERER / 'windows-package' / quality.name
        profile = template.read_text(encoding='utf-8-sig')
        settings = {'CommandStream': int(proxy), 'StreamFrameSkip': 0, 'RenderProfile': 0}
        for key, value in settings.items():
            profile = re.sub(rf'(?im)^[;#]?[ \t]*{key}[ \t]*=[^\r\n]*(?:\r?\n|$)', '', profile)
        profile, count = re.subn(r'(?im)^\[Quality\][ \t]*$',
                                 '[Quality]\n' + '\n'.join(f'{key}={value}' for key, value in settings.items()), profile)
        if count != 1:
            raise ValueError('The renderer quality profile needs one [Quality] section')
        quality.write_text(profile, encoding='utf-8')
        inputs[quality.name] = live.digest(quality)
    addon = client / 'Interface' / 'AddOns' / ADDON
    addon.mkdir(parents=True)
    for suffix in ('toc', 'lua'):
        path = addon / f'{ADDON}.{suffix}'
        shutil.copyfile(FIXTURES / path.name, path)
        inputs[path.name] = live.digest(path)
    (addon / 'Scene.lua').write_text(f'NorthlightBenchmarkRotate = {str(scene == "ratchet").lower()}\n', encoding='ascii')


def backend_evidence(args, output):
    lines = live.log_lines(output / live.LOG)
    active = any(line.startswith('D3D9 device wrapped.') for line in lines)
    hdr = any(line.startswith('HDR experimental requested=1 active=1 ') for line in lines)
    if args.case.endswith('-dxvk'):
        backend = [line for line in lines if line.startswith('BACKEND selected=') and ' rules=dxvk ' in line
                   and line.endswith(' fallback=0')]
        stream = any(line.startswith('CSTREAM active ') and ' frameSkip=0 ' in line for line in lines)
        if not (backend and stream and active) or hdr:
            raise ValueError('Expected active DXVK SDR effects and CommandStream with frame skipping off')
        log = next((output / name for name in LOGS[-2:] if (output / name).is_file()), output / LOGS[-2])
        return {'renderer_active': active, 'adapter': live.dxvk_adapter(log, args.dxvk_adapter), 'backend': backend[-1]}
    if args.case == 'native-vulkan':
        lines = live.log_lines(output / 'vulkan.log')
        swapchain = next((line for line in reversed(lines) if 'swapchain created' in line), '')
        if ('B8G8R8A8_UNORM / SRGB_NONLINEAR, IMMEDIATE' not in swapchain or
                not any('first frame presented' in line for line in lines)):
            raise ValueError('Expected the native Vulkan SDR swapchain and a completed Present')
        return {'swapchain': swapchain, 'adapter': next((line for line in lines if 'chose adapter' in line), None)}
    if args.case.startswith('native-'):
        if 'NORTHLIGHT native client: D3D9 factory attached' not in lines:
            raise ValueError('The source-linked client did not attach its D3D9 factory')
        present = next((line for line in lines if line.startswith('HDR native DXGI Present ') and
                        'transfer=D3D9On12 GPU' in line), None)
        passthrough = next((line for line in lines if line.startswith('X64 D3D9 passthrough:')), None)
        if args.case.endswith('-hdr') and not (active and hdr and present):
            raise ValueError('Expected active native D3D9On12 scRGB presentation')
        if not args.case.endswith('-hdr') and (hdr or not (active or passthrough)):
            raise ValueError('Expected an SDR Northlight device or a logged capability fallback')
        caps = next((line for line in lines if line.startswith('Backend capabilities:')), None)
        return {'renderer_active': active, 'adapter': caps, 'hdr_present': present, 'passthrough': passthrough}
    lines = live.log_lines(output / 'gx.log')
    if not any('CGxDeviceD3d::DeviceSetFormat()' in line for line in lines):
        raise ValueError('This client did not start its stock D3D9 engine')
    return {'renderer_active': False, 'adapter': next((line for line in lines if 'DeviceAdapterID()' in line), None)}


def stop_server(server):
    if server.poll() is None:
        server.terminate()
        server.wait(timeout=5)


def park_window(user, hwnd):
    # Character selection is complete. Keep the owned window visible in the
    # background without letting it activate again during a sweep.
    window = wintypes.HWND(hwnd)
    user.EnableWindow(window, False)
    if user.GetForegroundWindow() == hwnd:
        user.ShowWindowAsync(window, 0)  # SW_HIDE
        user.ShowWindowAsync(window, 4)  # SW_SHOWNOACTIVATE
    user.SetWindowPos(window, wintypes.HWND(1), 0, 0, 0, 0, 0x4013)  # HWND_BOTTOM, async, no activation or resize


def check_window(report, observations, size, *, foreground):
    report['focus_samples'] = len(observations)
    report['foreground_samples'] = sum(focused for focused, _ in observations)
    report['observed_sizes'] = sorted({extent for _, extent in observations})
    expected_focus = len(observations) if foreground else 0
    if (len(observations) < 20 or report['foreground_samples'] != expected_focus or
            report['observed_sizes'] != [size]):
        raise ValueError(f'The timing window needs stable {report["focus_mode"]} focus and the requested client size')


def measure(args, scene, output, client, report):
    game, launch_flag, window_class = CASES[args.case]
    world, count = SCENES[scene]
    command = [sys.executable, '-u', fixture.__file__, '--auth-port', '0', '--scene', world, '--gameplay-benchmark']
    if world == 'crowd':
        command += ['--crowd-size', str(count)]
    env = {key: value for key, value in os.environ.items()
           if not key.upper().startswith(('DXVK_', 'GX_VULKAN_', 'WOW_DEV_', 'NORTHLIGHT_'))}
    if args.dxvk_adapter:
        env['DXVK_FILTER_DEVICE_NAME'] = args.dxvk_adapter
    if args.case == 'native-vulkan':
        env.update(GX_VULKAN_SHADERS=str(args.vulkan_shaders), NORTHLIGHT_VULKAN_SCRGB='0')
        report['runtime_sha256']['Vulkan manifest.txt'] = live.digest(args.vulkan_shaders / 'manifest.txt')
    graphics = args.graphics.read_text(encoding='utf-8-sig')
    installed_config = args.client / 'WTF' / 'Config.wtf'
    consent = re.compile(r'SET (?:readTOS|readEULA|readTerminationWithoutNotice) "[01]"', re.I)
    profile_lines = [line for line in graphics.splitlines() if not consent.fullmatch(line)]
    config = live.graphics_profile('\n'.join(profile_lines), args.resolution, 1)
    if installed_config.is_file():
        config += ''.join(line + '\n' for line in installed_config.read_text(encoding='utf-8-sig').splitlines()
                          if consent.fullmatch(line))
    config += 'SET lastCharacterIndex "1"\nSET checkAddonVersion "0"\n'
    setup = output / 'graphics.wtf'
    setup.write_bytes(config.encode('utf-8'))
    report['graphics_sha256'] = live.digest(setup)

    server_log = output / 'server.log'
    with ExitStack() as cleanup:
        with server_log.open('xb') as log:
            server = subprocess.Popen(command, cwd=output, stdout=log, stderr=subprocess.STDOUT,
                                      creationflags=subprocess.CREATE_NO_WINDOW)
        cleanup.callback(stop_server, server)
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline and server.poll() is None:
            if ready := re.search(r'^READY auth=(\d+) world=(\d+)$', '\n'.join(live.log_lines(server_log)), re.M):
                break
            time.sleep(.1)
        else:
            raise ValueError('The loopback fixture did not start. See server.log')
        endpoint = f'127.0.0.1:{ready[1]}'
        config_path = client / 'WTF' / 'Config.wtf'
        config_path.parent.mkdir()
        launch_config = live.runtime_client_config(config.encode('utf-8'), endpoint)
        config_path.write_bytes(launch_config)
        (output / 'launch-Config.wtf').write_bytes(launch_config)
        (client / live.REALMLIST).write_text(live.realmlist_config(endpoint), encoding='ascii')

        argv = [str(client / game)] + ([launch_flag] if launch_flag else [])
        console = output / 'client.stdout'
        if args.desktop == 'private':
            from northrend_private_desktop import Desktop
            desktop = Desktop()
            cleanup.callback(desktop.close)
            process = desktop.launch(argv, client, env, console)
            cleanup.callback(process.close)
            report['private_desktop'] = desktop.name
        else:
            desktop = None
            startup = subprocess.STARTUPINFO(dwFlags=subprocess.STARTF_USESHOWWINDOW,
                                             wShowWindow=5 if args.desktop == 'foreground' else 4)
            with console.open('xb') as log:
                process = subprocess.Popen(argv, cwd=client, env=env, stdout=log, stderr=subprocess.STDOUT,
                                           creationflags=subprocess.CREATE_NO_WINDOW, startupinfo=startup)
        cleanup.callback(live.close_client, process)
        start = time.monotonic()
        user = ctypes.windll.user32
        user.GetForegroundWindow.restype = wintypes.HWND
        actions, focus, events = {}, [], []
        report['login_actions'] = actions
        expected_size = tuple(map(int, args.resolution.split('x')))
        placed = None
        while process.poll() is None and time.monotonic() - start < args.timeout:
            if desktop:
                desktop.confirm_inactive()
            hwnd = next(iter(live.process_windows(process.pid, window_class)), None)
            events = live.log_lines(server_log)
            if hwnd:
                if not desktop and hwnd != placed and any(line.startswith('PLAYER_SPAWN ') for line in events):
                    if args.desktop == 'foreground':
                        live.focus_private_window(hwnd)
                    else:
                        park_window(user, hwnd)
                    placed = hwnd
                focus.append((time.monotonic_ns(), user.GetForegroundWindow() == hwnd,
                              live.window_extent(hwnd, client=True)))
                live.advance_login(hwnd, events, actions, start, time.monotonic(), slow_login=True)
                if 'BENCH_PAYLOAD complete' in events:
                    break
            time.sleep(.1)
        else:
            last_event = events[-1] if events else 'none'
            raise ValueError(f'Client exited or did not finish timing within {args.timeout}s (last event: {last_event})')
        timing, frames = read_timing(events, scene)
        ticks = timing['phase_tick_ns']
        observed = [(focused, size) for tick, focused, size in focus if ticks['start'] <= tick <= ticks['done']]
        check_window(report, observed, expected_size, foreground=args.desktop == 'foreground')
        report.update(timing)
        with (output / 'frame-times.csv').open('x', encoding='ascii', newline='') as file:
            rows = csv.writer(file)
            rows.writerow(('frame', 'game_frame_ms'))
            rows.writerows(enumerate(frames, 1))


def attempt(args, scene):
    output = args.output / args.case / scene
    output.mkdir(parents=True)
    client = output / 'client'
    report = {'status': 'FAIL', 'case': args.case, 'scene': scene, 'resolution': args.resolution,
              'msaa': 1, 'desktop': args.desktop,
              'focus_mode': 'foreground' if args.desktop == 'foreground' else 'background'}
    try:
        stage(args, scene, client, report)
        measure(args, scene, output, client, report)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        report['error'] = str(error)
    finally:
        for name in LOGS:
            if (client / name).is_file():
                shutil.copyfile(client / name, output / Path(name).name)
        if not args.keep_client:
            live.discard_work(report, output, ('client',))
    if 'error' not in report:
        try:
            report['backend'] = backend_evidence(args, output)
            report['status'] = 'PASS'
        except (OSError, ValueError) as error:
            report['error'] = str(error)
    live.write_json(output / 'result.json', report)
    print(f'{report["status"]}: {output / "result.json"}' +
          (f' ({report["error"]})' if 'error' in report else ''), flush=True)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--case', choices=CASES, required=True)
    parser.add_argument('--scene', choices=SCENES, nargs='+', default=tuple(SCENES))
    parser.add_argument('--client', type=Path, default=live.fp.client_root(required=False))
    parser.add_argument('--data', type=Path, help='Use MPQs from this Data directory instead of the installed client Data')
    parser.add_argument('--runtime', type=Path, help='Unpacked proxy or source-linked runtime directory')
    parser.add_argument('--backend', type=Path, help='DXVK d3d9.dll with the same bitness as the proxy')
    parser.add_argument('--dxvk-adapter', help='Optional DXVK device name filter')
    parser.add_argument('--vulkan-shaders', type=Path, help='Native Vulkan shader set containing manifest.txt')
    parser.add_argument('--graphics', type=Path, default=FIXTURES / 'gameplay-4k.wtf',
                        help='Override the versioned benchmark graphics preset')
    parser.add_argument('--output', type=Path, required=True, help='New output directory')
    parser.add_argument('--resolution', choices=('1280x720', '1920x1080', '3840x2160'), default='3840x2160')
    parser.add_argument('--desktop', choices=('private', 'interactive', 'foreground'), default='private',
                        help='Inactive desktop, interactive background, or interactive foreground timing')
    parser.add_argument('--timeout', type=int, default=180, help='Maximum client time per scene in seconds')
    parser.add_argument('--keep-client', action='store_true', help='Retain private clients for investigation')
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('Live gameplay timing requires a Windows GPU and desktop session')
    if not args.client or not live.fp.is_client(args.client):
        parser.error('Set NORTHLIGHT_CLIENT or pass --client with a 3.3.5a client directory')
    if not args.case.startswith('stock-') and (not args.runtime or not args.runtime.is_dir()):
        parser.error('Northlight cases require --runtime')
    if args.case.endswith('-dxvk') and (not args.backend or not args.backend.is_file()):
        parser.error('DXVK cases require --backend')
    if args.case == 'native-vulkan':
        args.vulkan_shaders = (args.vulkan_shaders or args.runtime / 'VulkanShaders').resolve()
        if not (args.vulkan_shaders / 'manifest.txt').is_file():
            parser.error('Pass --vulkan-shaders with a client-compatible shader set containing manifest.txt')
    if args.timeout < 45 or len(set(args.scene)) != len(args.scene):
        parser.error('Use distinct scenes and a timeout of at least 45 seconds')
    args.output = args.output.resolve()
    args.client = args.client.resolve()
    args.data = (args.data or args.client / 'Data').resolve()
    for source in (args.client, args.data, args.runtime, args.vulkan_shaders):
        if source and (args.output.is_relative_to(source.resolve()) or source.resolve().is_relative_to(args.output)):
            parser.error('Benchmark output must be disjoint from client, runtime and shader inputs')
    args.output.mkdir(parents=True, exist_ok=False)
    reports = [attempt(args, scene) for scene in args.scene]
    passed = all(report['status'] == 'PASS' for report in reports)
    live.write_json(args.output / 'summary.json', {'status': 'PASS' if passed else 'FAIL', 'runs': reports})
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
