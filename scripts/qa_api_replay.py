"""Windows API QA captures with a PresentMon clock and apitrace playback."""
from bisect import bisect_right
import csv
import ctypes
import os
from pathlib import Path
import re
import subprocess
import time
import uuid
from zipfile import ZipFile

from prepare_native_scene import digest


RENDERERS = {'d3d9ex': ('d3d9', '-d3d9ex'), 'd3d11': ('dxgi', '-d3d11'), 'opengl': ('gl', '-opengl')}
BOUNDARIES = {
    'd3d9ex': r'IDirect3D(?:Device|SwapChain)9(?:Ex)?::Present(?:Ex)?',
    'd3d11': r'IDXGISwapChain\d*::Present(?:1)?',
    'opengl': r'(?:SwapBuffers|wglSwap(?:Layer)?Buffers)',
}


def present_window(ticks, calls, mark, frequency):
    if len(calls) != len(ticks) or len(ticks) < 2:
        raise ValueError(f'Frame counts differ or are too small: PresentMon={len(ticks)}, apitrace={len(calls)}')
    if any(a >= b for a, b in zip(ticks, ticks[1:])):
        raise ValueError('PresentMon frame timestamps are not strictly ordered')
    first = max(0, bisect_right(ticks, mark - 15 * frequency) - 1)
    last = bisect_right(ticks, mark) - 1
    if last <= first:
        raise ValueError('The QA mark does not contain a playable frame sequence')
    return {'first_present': first + 1, 'last_present': last + 1,
            'duration_ms': (ticks[last] - ticks[first]) * 1000 // frequency,
            'preview_calls': [calls[i] for i in sorted({first, (first + last) // 2, last})]}


def certify(renderer, timeline, trace, cli, mark, frequency, output, timeout):
    boundary = BOUNDARIES[renderer]
    with timeline.open(encoding='utf-8-sig', newline='') as input_file:
        rows = list(csv.DictReader(input_file))
    if len({row['ProcessID'] for row in rows}) != 1:
        raise ValueError('Expected PresentMon events from exactly one captured client')
    ticks = sorted(int(row['QPCTime']) for row in rows)
    calls_file = output / 'api-presents.txt'
    with calls_file.open('xb') as log, (output / 'api-dump.stderr').open('xb') as errors:
        subprocess.run([cli, 'dump', '--color=never', '--thread-ids=true', '--multiline=false', '--verbose',
                        '--grep=' + boundary + '|CreateDeviceEx', trace], stdout=log, stderr=errors,
                       check=True, timeout=timeout, creationflags=subprocess.CREATE_NO_WINDOW)
    pattern = re.compile(r'^(\d+) @\d+ ' + boundary + r'\(.*\) = (?:S_OK|D3D_OK|TRUE|GL_TRUE|1|0)$')
    calls = []
    ex = False
    with calls_file.open(encoding='utf-8', errors='replace') as log:
        for line in log:
            if match := pattern.fullmatch(line.rstrip()):
                calls.append(int(match[1]))
            elif '::CreateDeviceEx(' in line and line.rstrip().endswith(('S_OK', 'D3D_OK')):
                ex = True
    if renderer == 'd3d9ex' and not ex:
        raise ValueError('No successful D3D9Ex device creation in the API trace')
    return present_window(ticks, calls, mark, frequency)


def record(args):
    import tkinter as tk
    from run_northrend_baseline import child_environment, stage_pe, write_json

    exe, tools, output, monitor = (path.resolve() for path in (args.client, args.tool_root, args.output, args.presentmon))
    cli = tools / 'bin/apitrace.exe'
    for binary in (exe, cli, monitor):
        stage_pe(binary, 'WowClient64.exe')
    if output.is_relative_to(exe.parent):
        raise ValueError('Put the QA capture outside the game directory')
    output.mkdir(parents=True, exist_ok=False)
    env = child_environment(output)
    trace, timeline = output / 'scene.trace', output / 'presents.csv'
    session = 'NorthlightQA-' + uuid.uuid4().hex
    api, flag = RENDERERS[args.renderer]
    result = {'status': 'INCOMPLETE', 'qa_transport': 'windows-apitrace', 'renderer': args.renderer,
              'client': exe.name, 'client_sha256': digest(exe), 'fps_eligible': False}
    counter = ctypes.c_int64()
    ctypes.windll.kernel32.QueryPerformanceFrequency(ctypes.byref(counter))
    frequency = counter.value
    tick = None
    window = tk.Tk()
    window.title('Northlight graphics QA')
    message = tk.StringVar(value='Recording. Mark the scene after reproducing the issue.')
    tk.Label(window, textvariable=message, padx=18, pady=8).pack()

    def save():
        nonlocal tick
        ctypes.windll.kernel32.QueryPerformanceCounter(ctypes.byref(counter))
        tick = counter.value
        result.update(mark_qpc=tick, qpc_frequency=frequency)
        button.configure(state='disabled')
        message.set('Scene marked. Exit the test client to finish the package.')

    button = tk.Button(window, text='Save last 15 seconds', command=save)
    button.pack(padx=18, pady=(0, 12))
    window.protocol('WM_DELETE_WINDOW', lambda: message.set('Exit the test client to close this session.'))

    def poll():
        if game.poll() is None:
            window.after(100, poll)
        else:
            window.quit()

    try:
        with (output / 'presentmon.log').open('xb') as mon_log, (output / 'client.stdout').open('xb') as app_log:
            recorder = subprocess.Popen([monitor, '--process_name', exe.name, '--session_name', session,
                                         '--output_file', timeline, '--qpc_time', '--v1_metrics', '--no_console_stats',
                                         '--no_track_input'], env=env, stdout=mon_log, stderr=subprocess.STDOUT,
                                        creationflags=subprocess.CREATE_NO_WINDOW)
            try:
                time.sleep(1)
                if recorder.poll() is not None:
                    raise ValueError('PresentMon did not start; see presentmon.log')
                game = subprocess.Popen([cli, 'trace', '--api=' + api, '--output=' + str(trace), exe, flag],
                                        cwd=exe.parent, env=env, stdout=app_log, stderr=subprocess.STDOUT)
                window.after(0, poll)
                window.mainloop()
                game.wait(timeout=10)
                # Let the real-time ETW session deliver the client's final display events before stopping it.
                time.sleep(3)
            finally:
                if recorder.poll() is None:
                    subprocess.run([monitor, '--session_name', session, '--terminate_existing_session'],
                                   env=env, stdout=mon_log, stderr=subprocess.STDOUT,
                                   creationflags=subprocess.CREATE_NO_WINDOW, timeout=10)
                    recorder.wait(timeout=10)
                window.destroy()
        if game.returncode or recorder.returncode or tick is None:
            raise ValueError(f'Incomplete session (marked={tick is not None}, client={game.returncode}, PresentMon={recorder.returncode})')
        selection = certify(args.renderer, timeline, trace, cli, tick, frequency, output, args.timeout)
        result.update(status='PASS', qa_scene=selection,
                      trace_sha256=digest(trace), timeline_sha256=digest(timeline))
        write_json(output / 'validation.json', result)
        pending = output.with_name(output.name + '.zip.pending')
        with ZipFile(pending, 'x') as bundle:
            for path in (trace, timeline, output / 'validation.json'):
                bundle.write(path, path.name)
        pending.rename(output.with_name(output.name + '.zip'))
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        result.update(status='INCOMPLETE', error=str(error))
        write_json(output / 'validation.json', result)
    print(f'{result["status"]}: {output / "validation.json"}')
    return 0 if result['status'] == 'PASS' else 1


def replay(args, capture, output, source):
    from run_northrend_baseline import child_environment, run_logged, stage_pe, write_json

    trace, timeline = capture / 'scene.trace', capture / 'presents.csv'
    if (source.get('status') != 'PASS' or digest(trace) != source['trace_sha256'] or
            digest(timeline) != source['timeline_sha256'] or output.is_relative_to(capture)):
        raise ValueError('Use a complete graphics QA package and a separate replay directory')
    renderer = source['renderer']
    tools = args.tool_root.resolve() / 'bin'
    host = tools / ('glretrace.exe' if renderer == 'opengl' else 'd3dretrace.exe')
    cli = tools / 'apitrace.exe'
    for binary in (host, cli):
        stage_pe(binary, 'WowClient64.exe')
    output.mkdir(parents=True, exist_ok=False)
    (output / 'preview').mkdir()
    env = child_environment(output)
    result = {'status': 'INCOMPLETE', 'qa_transport': source['qa_transport'], 'renderer': renderer,
              'qa_scene': source['qa_scene'], 'fps_eligible': False}
    try:
        selection = certify(renderer, timeline, trace, cli, source['mark_qpc'], source['qpc_frequency'], output, args.timeout)
        if selection != source['qa_scene']:
            raise ValueError('Graphics QA interval differs from its trace and timestamps')
        calls = selection['preview_calls']
        options = ['--snapshot', ','.join(map(str, [*calls, (1 << 32) - 2])), '--call-nos=true',
                   '--snapshot-prefix', 'preview/present-']
        if renderer != 'opengl':
            options.append('--query-handling=run')
        if not args.qa_visible:
            options.append('--headless')
        result['host_exit_code'] = run_logged([host, *options, trace], output, env, 'retrace', args.timeout)
        previews = [f'preview/present-{call:010d}.png' for call in calls]
        if result['host_exit_code'] or any(not (output / path).is_file() or not (output / path).stat().st_size for path in previews):
            raise ValueError('Graphics replay did not write all expected previews')
        result.update(status='PASS', previews=previews)
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        result['error'] = str(error)
    write_json(output / 'validation.json', result)
    print(f'{result["status"]}: {output / "validation.json"}')
    return 0 if result['status'] == 'PASS' else 1
