"""Sample individual pixels after selected D3D9 trace calls through stock DXVK."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import threading

from prepare_native_scene import digest, tool_runtime
from run_northrend_baseline import DXVK_ADAPTER_ENV, child_environment, dxvk_adapter, write_json
from northrend_frame_parity import draw_witness


END_OF_TRACE = (1 << 32) - 1
DXVK_LOG = 'Wow_d3d9.log'


def snapshots(stream, calls, size):
    width, height = size
    for call in calls:
        if (stream.readline(64) != b'P6\n' or stream.readline(64) != f'#{call}\n'.encode('ascii') or
                stream.readline(64) != f'{width} {height}\n'.encode('ascii') or stream.readline(64) != b'255\n'):
            raise ValueError(f'Unexpected snapshot header at D3D9 call {call}')
        length = width * height * 3
        pixels = stream.read(length)
        if len(pixels) != length:
            raise ValueError(f'Truncated snapshot at D3D9 call {call}')
        yield str(call), pixels
    if stream.read(1):
        raise ValueError('Unexpected extra snapshot output')


def read_samples(stream, calls, size, points):
    offsets = [(y * size[0] + x) * 3 for x, y in points]
    return {call: [image[offset:offset + 3].hex() for offset in offsets]
            for call, image in snapshots(stream, calls, size)}


def compare_draw_images(live, replay, size, rect):
    left, top, right, bottom = rect
    stride = size[0] * 3
    stock = [b''.join(image[y * stride + left * 3:y * stride + right * 3] for y in range(top, bottom))
             for image in replay]
    native = []
    for image in live:
        rgb = bytearray(len(image) // 4 * 3)
        rgb[0::3], rgb[1::3], rgb[2::3] = image[2::4], image[1::4], image[0::4]
        native.append(rgb)
    before_differences = after_differences = introduced = removed = live_changes = stock_changes = 0
    pixel_count = (right - left) * (bottom - top)
    for offset in range(0, pixel_count * 3, 3):
        span = slice(offset, offset + 3)
        before, after = native[0][span], native[1][span]
        stock_before, stock_after = stock[0][span], stock[1][span]
        differed_before, differs_after = before != stock_before, after != stock_after
        before_differences += differed_before
        after_differences += differs_after
        introduced += not differed_before and differs_after
        removed += differed_before and not differs_after
        live_changes += before != after
        stock_changes += stock_before != stock_after
    return dict(pixels=pixel_count, rgb_different_before=before_differences, rgb_different_after=after_differences,
                became_different=introduced, became_equal=removed, live_changed=live_changes, stock_changed=stock_changes)


def verify_draw_call(tool, trace, call, selector, timeout):
    dumped = subprocess.run([str(tool), 'dump', '--color=never', f'--calls={call}', str(trace)],
                            check=True, capture_output=True, text=True, timeout=timeout).stdout.splitlines()
    min_vertex, vertices, start, count = map(int, selector.split(','))
    signature = (rf'^\s*{call} IDirect3DDevice9::DrawIndexedPrimitive\(this = 0x[0-9a-fA-F]+, '
                 rf'PrimitiveType = D3DPT_TRIANGLELIST, BaseVertexIndex = 0, MinVertexIndex = {min_vertex}, '
                 rf'NumVertices = {vertices}, startIndex = {start}, primCount = {count}\) = D3D_OK\s*$')
    matches = [line.strip() for line in dumped if re.fullmatch(signature, line)]
    if len(matches) != 1:
        raise ValueError('The live witness does not identify the exact selected draw in the captured trace')
    return matches[0]


def coordinates(value):
    try:
        x, y = (int(part) for part in value.split(','))
    except ValueError as error:
        raise argparse.ArgumentTypeError('Use X,Y for a pixel coordinate') from error
    return x, y


def effective_dxvk_config(log):
    blocks = []
    current = None
    for line in log.splitlines():
        if line == 'info:  Effective configuration:':
            current = {}
            blocks.append(current)
        elif current is not None:
            option = re.fullmatch(r'info:    ([A-Za-z0-9_.]+) = (.+)', line)
            if option is None:
                current = None
            else:
                key, value = option.groups()
                if key in current:
                    raise ValueError(f'Duplicate DXVK option in the effective configuration: {key}')
                current[key] = value
    if not blocks or not blocks[0] or any(block != blocks[0] for block in blocks):
        raise ValueError('Missing or inconsistent effective configuration in the DXVK log')
    return blocks[0]


def probe(args):
    root = args.capture.resolve()
    capture = json.loads((root / 'validation.json').read_bytes())
    if capture.get('status') != 'PASS':
        raise ValueError('A probe needs a completed live capture')
    trace = root / 'wow.trace'
    if trace.stat().st_size != capture['trace']['bytes'] or digest(trace) != capture['trace']['sha256']:
        raise ValueError('The trace differs from the live capture receipt')
    backend = args.backend.resolve()
    backend_hash = digest(backend)
    if backend_hash != capture['runtime_inputs']['backend_d3d9.dll']:
        raise ValueError('The DXVK binary differs from the one used for capture')
    live_dxvk_log = (root / DXVK_LOG).read_bytes()
    configuration = effective_dxvk_config(live_dxvk_log.decode('utf-8'))
    adapter_receipt = capture.get('dxvk_adapter')
    live_adapter = dxvk_adapter(root / DXVK_LOG, adapter_receipt['filter'] if adapter_receipt else None)
    if adapter_receipt and live_adapter != adapter_receipt:
        raise ValueError('The DXVK adapter log differs from the capture receipt')

    size = tuple(map(int, capture['observation']['output']['resolution'].split('x')))
    width, height = size
    if not (0 < width <= 16384 and 0 < height <= 16384) or any(
            not (0 <= x < width and 0 <= y < height) for x, y in args.point):
        raise ValueError('A probe point falls outside the captured render size')
    live = witness = draw_call = None
    if args.draw_witness:
        expected = capture.get('draw_witness')
        if not expected:
            raise ValueError('Live capture has no certified indexed draw witness')
        witness, live = draw_witness(root / 'frame-parity', size, capture['observation']['output']['msaa'])
        if expected != {'selector': expected['selector'], **witness}:
            raise ValueError('Live draw witness differs from the capture receipt')
        calls = [witness['call'] - 1, witness['call']]
    else:
        if not args.point:
            raise ValueError('At least one --point is required for selected calls')
        calls = sorted(args.calls)
    if not (0 <= calls[0] and calls[-1] < END_OF_TRACE) or len(set(calls)) != len(calls):
        raise ValueError('Use unique trace call numbers below the end-of-trace sentinel')

    runtime = tool_runtime(args.tool_root)
    source = Path(runtime['root']) / 'bin'
    if witness:
        draw_call = verify_draw_call(source / 'apitrace.exe', trace, calls[1], expected['selector'], args.timeout)
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    host = out / 'd3dretrace.exe'
    shutil.copyfile(source / host.name, host)
    for dependency in source.glob('*.dll'):
        shutil.copyfile(dependency, out / dependency.name)
    shutil.copyfile(backend, out / 'd3d9.dll')
    # The sentinel keeps retrace running through teardown after the last sampled call.
    selection = ','.join(map(str, calls + [END_OF_TRACE]))
    command = [str(host), '--headless', '--query-handling=run', '--snapshot-force-backbuffer',
               '-s', '-', '-S', selection, str(trace)]
    environment = child_environment(out)
    environment[DXVK_ADAPTER_ENV] = live_adapter['device']
    (out / 'dxvk.conf').write_text(''.join(f'{key} = {value}\n' for key, value in configuration.items()), encoding='utf-8')
    with (out / 'retrace.stderr').open('xb') as log, subprocess.Popen(
            command, cwd=out, env=environment, stdout=subprocess.PIPE, stderr=log) as process:
        timer = threading.Timer(args.timeout, process.kill)
        timer.start()
        try:
            if witness:
                images = [image for _, image in snapshots(process.stdout, calls, size)]
                comparison = compare_draw_images(live, images, size, witness['rect'])
            else:
                samples = read_samples(process.stdout, calls, size, args.point)
            if process.wait():
                raise ValueError('DXVK replay failed. See retrace.stderr.')
        finally:
            timer.cancel()
            if process.poll() is None:
                process.kill()
    if effective_dxvk_config((out / DXVK_LOG).read_text(encoding='utf-8')) != configuration:
        raise ValueError('The probe ran with different effective DXVK settings')
    replay_adapter = dxvk_adapter(out / DXVK_LOG, live_adapter['device'])
    if replay_adapter['device'] != live_adapter['device']:
        raise ValueError('The probe used a different DXVK device from the capture')
    report = dict(trace_sha256=capture['trace']['sha256'], backend_sha256=backend_hash,
                  retracer_sha256=digest(host), command=command, size=size,
                  capture_dxvk_log_sha256=live_adapter['log_sha256'],
                  capture_dxvk_adapter=live_adapter, replay_dxvk_adapter=replay_adapter,
                  dxvk=configuration, fps_eligible=False)
    if witness:
        report.update(draw_witness=expected, trace_draw=draw_call, calls=calls, comparison=comparison)
    else:
        report.update(points=args.point, samples=samples)
    write_json(out / 'samples.json', report)
    print(out / 'samples.json')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('capture', 'backend', 'tool-root', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    selection = parser.add_mutually_exclusive_group(required=True)
    selection.add_argument('--calls', nargs='+', type=int)
    selection.add_argument('--draw-witness', action='store_true')
    parser.add_argument('--point', action='append', type=coordinates, default=[])
    parser.add_argument('--timeout', type=int, default=180)
    try:
        probe(parser.parse_args())
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, f'{error}\n')
