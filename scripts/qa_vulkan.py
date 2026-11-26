"""Capture a Vulkan QA scene from launch using the GFXReconstruct layer."""
from collections import deque
import ctypes
from ctypes import wintypes
import os
import struct
import subprocess
import time
from zipfile import ZipFile

from prepare_native_scene import digest


FILE_HEADER = struct.Struct('<4sIII')
BLOCK_HEADER = struct.Struct('<QI')
FRAME_MARKER = struct.Struct('<IQ')
WINDOW_NS = 15_000_000_000


def frame_markers(stream, size, offset=0, last=0):
    """Read complete GFXR 0.1 blocks and leave a partial live write for the next poll."""
    if offset == 0:
        if size < FILE_HEADER.size:
            return 0, last
        stream.seek(0)
        magic, major, minor, options = FILE_HEADER.unpack(stream.read(FILE_HEADER.size))
        if (magic, major, minor) != (b'GFXR', 0, 1) or options > 64:
            raise ValueError('Expected GFXReconstruct 0.1 with explicit frame markers')
        offset = FILE_HEADER.size + 8 * options
        if offset > size:
            return 0, last
    while offset + BLOCK_HEADER.size <= size:
        stream.seek(offset)
        length, kind = BLOCK_HEADER.unpack(stream.read(BLOCK_HEADER.size))
        following = offset + BLOCK_HEADER.size + length
        if following > size:
            break
        if kind == 1:
            if length != FRAME_MARKER.size:
                raise ValueError('Invalid GFXReconstruct frame marker length')
            marker, frame = FRAME_MARKER.unpack(stream.read(length))
            if marker == 2:
                if frame != last + 1:
                    raise ValueError('Expected contiguous Vulkan frames recorded from launch')
                last = frame
        offset = following
    return offset, last


def recent_samples(samples, tick):
    while len(samples) > 1 and samples[0][1] < tick - WINDOW_NS:
        samples.popleft()
    return (samples[0], samples[-1]) if len(samples) >= 2 else None


def live_size_reader():
    # A metadata-only handle sees live growth even when GFXReconstruct denies read sharing.
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    open_file = kernel.CreateFileW
    open_file.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID,
                          wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    open_file.restype = wintypes.HANDLE
    kernel.GetFileSizeEx.argtypes = [wintypes.HANDLE, ctypes.POINTER(ctypes.c_longlong)]
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]

    def size(path):
        handle = open_file(str(path), 0, 7, None, 3, 128, None)
        if handle == ctypes.c_void_p(-1).value:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            length = ctypes.c_longlong()
            if not kernel.GetFileSizeEx(handle, ctypes.byref(length)):
                raise ctypes.WinError(ctypes.get_last_error())
            return length.value
        finally:
            kernel.CloseHandle(handle)

    return size


def trace_end(path, offset=0, last=0):
    with path.open('rb') as stream:
        size = os.fstat(stream.fileno()).st_size
        offset, last = frame_markers(stream, size, offset, last)
    return size, offset, last


def record(args):
    import tkinter as tk
    from run_northrend_baseline import child_environment, close_client, stage_pe, write_json

    exe, tools, output = (path.resolve() for path in (args.client, args.tool_root, args.output))
    stage_pe(exe, 'WowClient64.exe')
    layer = tools / 'VkLayer_gfxreconstruct.json'
    if not layer.is_file():
        raise ValueError('Select the GFXReconstruct Windows Bin folder containing VkLayer_gfxreconstruct.json')
    if output.is_relative_to(exe.parent):
        raise ValueError('Put the QA capture outside the game directory')
    output.mkdir(parents=True, exist_ok=False)
    trace = output / 'scene.gfxr'
    env = child_environment(output)
    env = {key: value for key, value in env.items() if not key.upper().startswith('GFXRECON_')}
    env.update(VK_LAYER_PATH=str(tools), VK_INSTANCE_LAYERS='VK_LAYER_LUNARG_gfxreconstruct',
               GFXRECON_CAPTURE_PROCESS_NAME=exe.name, GFXRECON_CAPTURE_FILE=str(trace),
               GFXRECON_CAPTURE_FILE_TIMESTAMP='false', GFXRECON_CAPTURE_USE_ASSET_FILE='false',
               GFXRECON_LOG_FILE=str(output / 'capture.log'))
    if args.shaders:
        shaders = args.shaders.resolve()
        if not (shaders / 'manifest.txt').is_file():
            raise ValueError('Choose a Vulkan shader directory with manifest.txt')
        env['GX_VULKAN_SHADERS'] = str(shaders)
    window = tk.Tk()
    window.title('Northlight Vulkan QA')
    message = tk.StringVar(value='Waiting for Vulkan frames…')
    tk.Label(window, textvariable=message, padx=18, pady=8).pack()
    samples = deque()
    selection = failure = None
    live_size = live_size_reader()

    def save():
        nonlocal selection, failure
        tick = time.monotonic_ns()
        try:
            size = live_size(trace)
            if size > samples[-1][0]:
                samples.append((size, tick))
            selection = recent_samples(samples, tick)
        except OSError as error:
            failure = error
        if selection:
            button.configure(state='disabled')
            message.set('Saving. Waiting for the client to finish writing the trace…')
            window.update_idletasks()
        if selection or failure:
            window.quit()

    button = tk.Button(window, text='Save last 15 seconds', state='disabled', command=save)
    button.pack(padx=18, pady=(0, 12))
    window.protocol('WM_DELETE_WINDOW', window.quit)
    result = {'status': 'INCOMPLETE', 'qa_transport': 'vulkan-gfxr', 'fps_eligible': False,
              'client': exe.name, 'client_sha256': digest(exe), 'clock': 'capture-flush-observed'}

    def poll():
        nonlocal failure
        try:
            if game.poll() is not None:
                raise ValueError('The client exited before the scene was saved')
            if trace.is_file():
                size = live_size(trace)
                if size > 6 << 30:
                    raise ValueError('Vulkan QA capture exceeded 6 GiB')
                tick = time.monotonic_ns()
                if size and (not samples or size != samples[-1][0]):
                    samples.append((size, tick))
                ready = recent_samples(samples, tick) is not None
                button.configure(state='normal' if ready else 'disabled')
                message.set('Recording Vulkan' if ready else 'Waiting for Vulkan frames…')
        except (OSError, ValueError) as error:
            failure = error
            window.quit()
            return
        window.after(30, poll)

    try:
        with (output / 'client.stdout').open('xb') as log:
            game = subprocess.Popen([exe, '-vulkan'], cwd=exe.parent, env=env, stdout=log, stderr=subprocess.STDOUT)
        print(f'Recording Vulkan in PID {game.pid}', flush=True)
        try:
            window.after(0, poll)
            window.mainloop()
        finally:
            forced = close_client(game, timeout=90 if selection else 12)
            window.destroy()
        if failure or not selection:
            raise ValueError(str(failure or 'The Vulkan scene was not marked'))
        if forced or game.returncode:
            raise ValueError(f'The Vulkan client did not close normally (exit {game.returncode}, forced={forced})')
        (first_size, began), (last_size, ended) = selection
        with trace.open('rb') as stream:
            offset, first = frame_markers(stream, first_size)
            offset, marked_last = frame_markers(stream, last_size, offset, first)
            size = os.fstat(stream.fileno()).st_size
            offset, last = frame_markers(stream, size, offset, marked_last)
        if offset != size or marked_last <= max(first, 1):
            raise ValueError('The closed Vulkan trace has a partial frame or block')
        scene = {'first_present': max(1, first), 'last_present': marked_last, 'duration_ms': (ended - began) // 1_000_000}
        result.update(status='PASS', qa_scene=scene, trace_sha256=digest(trace), frames=last)
        write_json(output / 'validation.json', result)
        pending = output.with_name(output.name + '.zip.pending')
        with ZipFile(pending, 'x') as bundle:
            for path in (trace, output / 'capture.log', output / 'validation.json'):
                bundle.write(path, path.name)
        pending.rename(output.with_name(output.name + '.zip'))
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        result.update(status='INCOMPLETE', error=str(error))
        write_json(output / 'validation.json', result)
    print(f'{result["status"]}: {output / "validation.json"}')
    return 0 if result['status'] == 'PASS' else 1


def replay(args, capture, output, source):
    from run_northrend_baseline import child_environment, run_logged, write_json

    trace = capture / 'scene.gfxr'
    if source.get('status') != 'PASS' or digest(trace) != source['trace_sha256'] or output.is_relative_to(capture):
        raise ValueError('Use a complete Vulkan QA package and a separate replay output directory')
    size, offset, frames = trace_end(trace)
    first, last = (source['qa_scene'][key] for key in ('first_present', 'last_present'))
    if size != offset or frames != source['frames'] or not 0 < first < last <= frames:
        raise ValueError('Vulkan QA window does not belong to this trace')
    tool = args.tool_root.resolve() / 'gfxrecon-replay.exe'
    if not tool.is_file():
        raise ValueError('Select the GFXReconstruct Windows Bin folder containing gfxrecon-replay.exe')
    output.mkdir(parents=True, exist_ok=False)
    (output / 'preview').mkdir()
    env = child_environment(output)
    env = {key: value for key, value in env.items() if not key.upper().startswith(('GFXRECON_', 'VK_INSTANCE_LAYERS'))}
    preview = sorted({first, (first + last) // 2, last})
    options = ['--screenshots', ','.join(map(str, preview)), '--screenshot-format', 'png', '--screenshot-dir', 'preview']
    if not args.qa_visible:
        options.extend(['--swapchain', 'offscreen'])
    code = run_logged([tool, *options, trace], output, env, 'retrace', args.timeout)
    images = list((output / 'preview').glob('*.png'))
    ok = code == 0 and len(images) == len(preview) and all(path.stat().st_size for path in images)
    result = {'status': 'PASS' if ok else 'INCOMPLETE', 'qa_transport': source['qa_transport'],
              'host_exit_code': code, 'fps_eligible': False, 'qa_scene': source['qa_scene'],
              'previews': [path.relative_to(output).as_posix() for path in sorted(images)]}
    write_json(output / 'validation.json', result)
    print(f'{result["status"]}: {output / "validation.json"}')
    return 0 if ok else 1
