#!/usr/bin/env python3
"""Join a closed outer D3D9 trace to its exact native snapshot packets.

This produces a private NLJOIN1 import plan, never a scene/FPS acceptance claim.
The official parser supplies dispatch order; nested markers precede their Draw
even though their numeric call IDs are larger. No pointer substitution occurs.
"""
import argparse
from collections import defaultdict
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import threading

from prepare_native_scene import digest, inspect_snapshot, pe32, tool_runtime, unique_object

HEADER_BYTES, RECORD_BYTES, MAX_RECORDS = 256, 80, 20000
MAX_TOTAL = 256 << 20
MAX_DUMP_BYTES, MAX_TRACE_CALLS = 512 << 20, 2000000
CALL = re.compile(r'^(\d+)\s+@([0-9a-fA-F]+)\s+(\w+(?:::\w+)?)\((.*)$')
MARKER = re.compile(r'NLSNAP1;session=([A-Za-z0-9-]{1,64});record=(\d+);device=([0-9a-f]+);frame=(\d+);epoch=(\d+);thread=(\d+);trigger=([123]);draw=(\d+);bytes=(\d+);crc=([0-9a-f]{8})')
DRAW_APIS = {name: number for number, name in enumerate(('DrawPrimitive', 'DrawIndexedPrimitive', 'DrawPrimitiveUP', 'DrawIndexedPrimitiveUP'), 1)}


def load_json(path, limit=65536):
    if not 0 < path.stat().st_size <= limit:
        raise ValueError(f'Invalid metadata size: {path}')
    return json.loads(path.read_text(encoding='utf-8'), object_pairs_hook=unique_object)


def calls(path, max_calls=MAX_TRACE_CALLS):
    """Stream an official dump, keeping its parser dispatch order unchanged."""
    current, seen = None, set()
    with path.open(encoding='utf-8', errors='strict') as source:
        for line in source:
            match = CALL.match(line.rstrip('\n'))
            if match:
                if current:
                    if ' // incomplete' in current['text']:
                        raise ValueError('Incomplete call in the original trace')
                    yield current
                number = int(match[1])
                if number > 0xffffffff or number in seen:
                    raise ValueError('Out-of-range or repeated original call ID')
                if len(seen) >= max_calls:
                    raise ValueError('Trace call count exceeds the explicit parser capacity')
                seen.add(number)
                current = {'call': number, 'thread': int(match[2], 16), 'name': match[3], 'text': match[4]}
            elif line.strip():
                if current is None and re.fullmatch(r'// [A-Za-z0-9_.-]+ = "(?:[^"\\\r\n]|\\.)*"\s*', line):
                    continue # Official parser properties precede all calls.
                if not current or re.match(r'^\d+\s+@', line):
                    raise ValueError('Unexpected official dump record')
                current['text'] += '\n' + line.rstrip('\n')
        if current:
            if ' // incomplete' in current['text']:
                raise ValueError('Incomplete final call')
            yield current


def dump_trace(command, output, env):
    """Bound both official CLI streams while preserving failed/truncated evidence."""
    failure = []
    with subprocess.Popen(command, cwd=output, env=env, stdin=subprocess.DEVNULL,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE) as child:
        def copy(stream, name, limit):
            try:
                total = 0
                with (output / name).open('xb') as target:
                    for block in iter(lambda: stream.read(65536), b''):
                        if total + len(block) > limit:
                            failure.append(name + ' exceeds explicit byte capacity')
                            child.kill(); return
                        target.write(block); total += len(block)
            except Exception as error:
                failure.append(str(error)); child.kill()
        workers = [threading.Thread(target=copy, args=pair) for pair in
                   ((child.stdout, 'trace.dump', MAX_DUMP_BYTES), (child.stderr, 'dump.stderr', 1 << 20))]
        for worker in workers:
            worker.start()
        try:
            code = child.wait(timeout=300)
        except subprocess.TimeoutExpired:
            failure.append('Official parser timeout'); child.kill(); code = child.wait()
        finally:
            for worker in workers:
                worker.join()
    if failure:
        raise ValueError('; '.join(failure))
    return code


def pointer(text, field):
    match = re.search(r'\b' + re.escape(field) + r' = (?:&)?(0x[0-9a-fA-F]+|NULL)(?=[,\)])', text)
    if not match:
        raise ValueError(f'Missing exact pointer argument: {field}')
    value = 0 if match[1] == 'NULL' else int(match[1], 16)
    if not 0 < value <= 0xffffffff:
        raise ValueError(f'Invalid x86 pointer argument: {field}')
    return value


def result(text, allowed=('D3D_OK', 'S_OK')):
    match = re.search(r'\) = ([^\s]+)\s*$', text)
    if not match or match[1] not in allowed:
        raise ValueError('Unsupported or failed captured API result')
    return match[1]


def packet_reads(data):
    grouped = defaultdict(list)
    offset = 40
    for _ in range(struct.unpack_from('<I', data, 24)[0]):
        address, size, flags = struct.unpack_from('<QII', data, offset)
        offset += 16
        grouped[address, size].append(data[offset:offset + size] if flags else None)
        if flags:
            offset += size
    return grouped


def admit_no_resource_aliases(data):
    """Follow only typed native texture-identity reads, preserving per-key FIFO.

    Known failed reads retain their captured meaning. Missing reads are not
    failures we may invent. Nonzero COM identities require a future typed,
    lifetime-aware alias API and therefore cannot enter the current host.
    """
    grouped, cursor = packet_reads(data), defaultdict(int)

    def read(address, size):
        key = address, size
        at = cursor[key]
        if key not in grouped or at >= len(grouped[key]):
            raise ValueError('Typed native identity read is missing from the packet')
        cursor[key] += 1
        return grouped[key][at]

    def integer(address):
        value = read(address, 4)
        return struct.unpack('<I', value)[0] if value is not None else None

    def valid(value, tail):
        return 0x10000 <= value <= 0xffffffff - tail and value % 4 == 0

    def identity(address):
        handle = integer(address)
        if handle is None or handle == 0 or not valid(handle, 0x60):
            return handle == 0
        fields = read(handle + 0x20, 0x40)
        if fields is None:
            return False
        gx, variant = struct.unpack_from('<I', fields, 0x24)[0], struct.unpack_from('<I', fields, 0x3c)[0]
        if fields[8] & 4 and variant:
            if not valid(variant, 0x1c):
                return False
            gx = integer(variant + 0x18)
            if gx is None:
                return False
        if not gx:
            return True
        if not valid(gx, 0x3c):
            return False
        opaque = integer(gx + 0x38)
        if opaque:
            raise ValueError('Nonzero native texture identity needs a typed resource alias bridge')
        return opaque == 0

    for body in (0xd38e28, 0xd38e48, 0xd38e68):
        if identity(body + 0x10):
            identity(body + 0x10)
    for record, expected in ((0xd38ea8, 0xd38e28), (0xd38f58, 0xd38e48)):
        link = integer(record + 0xa8)
        if link == expected and identity(record + 0x1c) and identity(record + 0x1c):
            integer(record + 0xa8)


def snapshot_inputs(root):
    status = load_json(root / 'native-capture-status.json')
    if (status.get('format') != 1 or status.get('kind') != 'native-snapshot-capture-status' or
            status.get('capture_layer') != 'above-northlight' or status.get('diagnostic') is not True or
            status.get('fps_eligible') is not False or status.get('trace_join_validated') is not False or
            any(status.get(k) is not True for k in ('started', 'finished', 'complete')) or
            status.get('pending') is not False or status.get('failures') != 0 or status.get('sink_failures') != 0):
        raise ValueError('Capture status is absent, incomplete or ineligible')
    count = status.get('accepted')
    if type(count) is not int or not 0 < count <= MAX_RECORDS or status.get('marked') != count or status.get('retired') != count:
        raise ValueError('Capture/archive/marker counts differ')
    session = status.get('session')
    if not isinstance(session, str) or not re.fullmatch(r'[A-Za-z0-9-]{1,64}', session):
        raise ValueError('Invalid capture session')
    journal_path = root / 'capture.journal.jsonl'
    if not 0 < journal_path.stat().st_size <= (MAX_RECORDS + 2) * 512:
        raise ValueError('Capture journal exceeds bounded capacity')
    journal = [json.loads(line, object_pairs_hook=unique_object) for line in journal_path.read_text(encoding='utf-8').splitlines()]
    if len(journal) != count + 2 or journal[0] != {'format': 1, 'kind': 'snapshot-journal', 'session': session, 'trace_join_validated': False}:
        raise ValueError('Journal session/header/count differs')
    if journal[-1] != {'writer_drained': True, 'accepted': count, 'retired': count, 'failures': 0, 'trace_join_validated': False}:
        raise ValueError('Journal was not cleanly drained')
    packets, hashes = [], {str(root / 'native-capture-status.json'): digest(root / 'native-capture-status.json'), str(journal_path): digest(journal_path)}
    physical, total = set(), 0
    for number, row in enumerate(journal[1:-1]):
        if row.get('record') != number or row.get('path') != f'{number:06d}.nlsnap' or row.get('frame_domain') != 'stream-producer':
            raise ValueError('Journal record ordering/path/frame domain differs')
        path = (root / row['path']).resolve()
        if path.parent != root or not path.is_file():
            raise ValueError('Snapshot must be a regular file within this capture')
        stat = path.stat(); identity = stat.st_dev, stat.st_ino
        if identity in physical:
            raise ValueError('Snapshot physical file alias')
        physical.add(identity)
        metadata, data = inspect_snapshot(path), path.read_bytes()
        if (metadata['draw_ordinal'] != row.get('draw_ordinal') or metadata['trigger_kind'] != row.get('trigger') or
                len(data) != row.get('bytes') or struct.unpack_from('<I', data, len(data) - 4)[0] != row.get('archive_crc32')):
            raise ValueError('Archive/journal provenance differs')
        admit_no_resource_aliases(data)
        total += len(data)
        if total > MAX_TOTAL:
            raise ValueError('Snapshot packet budget exceeded')
        hashes[str(path)] = metadata['sha256']; packets.append(data)
    return session, journal[1:-1], packets, hashes


def join(rows, session, journal):
    factory = device = None
    factory_call = create_call = final_release = None
    trace_thread = os_thread = None
    draws = presents = resets = used = 0
    pending = None
    records, trigger_seen = [], set()
    for row in rows:
        name, text, call, thread = row['name'], row['text'], row['call'], row['thread']
        if 'Ex::' in name or name in ('Direct3DCreate9Ex', 'IDirect3DDevice9::CreateAdditionalSwapChain'):
            raise ValueError('Ex/multiple swap chains are outside the validated import protocol')
        if name == 'Direct3DCreate9':
            if factory is not None:
                raise ValueError('Multiple factories/reused capture session')
            match = re.search(r'\) = (0x[0-9a-fA-F]+)\s*$', text)
            if not match or not 0 < int(match[1], 16) <= 0xffffffff:
                raise ValueError('Invalid factory return identity')
            factory, factory_call, trace_thread = int(match[1], 16), call, thread
        elif name == 'IDirect3D9::CreateDevice':
            if device is not None or factory is None or pointer(text, 'this') != factory or thread != trace_thread:
                raise ValueError('Unmatched or multiple device creation')
            result(text); device = pointer(text, 'ppReturnedDeviceInterface'); create_call = call
        elif name == 'D3DPERF_SetMarker' and 'NLSNAP1' in text:
            if pending is not None or device is None or final_release is not None or used >= len(journal):
                raise ValueError('Marker has no unique live target Draw')
            match = re.fullmatch(r'col = (?:0x4e4c0001|' + str(0x4e4c0001) + r'), wszName = L?"(' + MARKER.pattern + r')"\)', text)
            if not match:
                raise ValueError('Malformed NLSNAP1 marker/color')
            values = MARKER.fullmatch(match[1]).groups()
            marker_session = values[0]
            numbers = [int(v, 16 if i in (1, 8) else 10) for i, v in enumerate(values[1:])]
            # record, device, frame, epoch, OS thread, trigger, draw, bytes, CRC
            number, key, frame, epoch, source_tid, trigger, draw, size, crc = numbers
            expected = journal[used]
            actual = {'record': number, 'device_key': key, 'frame': frame, 'epoch': epoch, 'thread': source_tid,
                      'trigger': trigger, 'draw_ordinal': draw, 'bytes': size, 'archive_crc32': crc}
            if marker_session != session or any(expected.get(k) != v for k, v in actual.items()) or number != used or key != device:
                raise ValueError('Marker/journal/device association differs')
            if (thread != trace_thread or frame != presents or epoch != resets or not 0 < source_tid <= 0xffffffff or
                    not 0 <= epoch <= 0xffffffff or not 0 < draw <= 0xffffffffffffffff):
                raise ValueError('Marker thread/frame/reset provenance differs')
            if os_thread is None:
                os_thread = source_tid
            if source_tid != os_thread or (frame, trigger) in trigger_seen:
                raise ValueError('Producer identity changed or trigger repeated within a frame')
            trigger_seen.add((frame, trigger))
            pending = (call, key, draw, frame, epoch, trigger, source_tid, size, crc)
        elif name.startswith('IDirect3DDevice9::') or name.startswith('IDirect3DSwapChain9::'):
            method = name.split('::')[1]
            if method not in DRAW_APIS and method not in ('Reset', 'Present', 'Release'):
                continue
            receiver = pointer(text, 'this')
            if name.startswith('IDirect3DSwapChain9::'):
                if method == 'Present':
                    raise ValueError('Swap-chain Present needs an authenticated device association')
                continue
            if device is None or receiver != device or final_release is not None or thread != trace_thread:
                raise ValueError('Call does not belong to the admitted device/thread/lifetime')
            if method == 'Release':
                match = re.search(r'\) = (\d+)\s*$', text)
                if not match or not 0 <= int(match[1]) <= 0xffffffff:
                    raise ValueError('Invalid captured device Release count')
                value = match[1]
                if value == '0':
                    if pending:
                        raise ValueError('Device retired with an unconsumed marker')
                    final_release = call
            elif method == 'Present':
                result(text)
                if pending:
                    raise ValueError('Marker crossed a Present')
                presents += 1
            elif method == 'Reset':
                result(text)
                if pending:
                    raise ValueError('Marker crossed a Reset')
                resets += 1
            else:
                result(text); draws += 1
                if pending:
                    marker, key, draw, frame, epoch, trigger, source_tid, size, crc = pending
                    if draw != draws or marker <= call or (not used and draw != 1):
                        raise ValueError('Marker is not nested in its exact original Draw')
                    records.append((call, marker, key, draw, frame, epoch, trigger, thread, source_tid, size, crc, DRAW_APIS[method]))
                    used += 1; pending = None
                elif not used:
                    raise ValueError('Capture did not begin before the first Draw')
    if pending or used != len(journal) or not used or final_release is None or not presents:
        raise ValueError('Trace/session is not complete through final device release')
    return {'factory_call': factory_call, 'create_call': create_call, 'factory': factory, 'device': device,
            'draws': draws, 'presents': presents, 'resets': resets, 'final_release': final_release,
            'trace_thread': trace_thread, 'os_thread': os_thread}, records


def compile_plan(args):
    output = args.output.resolve()
    if output.exists():
        raise ValueError('Output must be new; prior evidence is immutable')
    root, trace, client, dll = (p.resolve() for p in (args.snapshot_dir, args.trace, args.client, args.stand_dll))
    tools = tool_runtime(args.tool_root)
    session, journal, packets, inputs = snapshot_inputs(root)
    host_path = root / 'capture-host.json'; host = load_json(host_path)
    for path in (trace, client, dll):
        inputs[str(path)] = digest(path)
    inputs[str(host_path)] = digest(host_path); inputs.update(tools['files'])
    if host.get('session') != session or host.get('capture_layer') != 'above-northlight' or host.get('fps_eligible') is not False:
        raise ValueError('Capture host identity/session differs')
    if (host.get('wrapper_sha256') != digest(Path(tools['root']) / 'lib/wrappers/d3d9.dll') or
            host.get('client_sha256') != inputs[str(client)] or Path(host.get('trace_path', '')).resolve() != trace):
        raise ValueError('Capture must identify the exact official outer wrapper, client and trace')
    if not 64 <= client.stat().st_size <= 64 << 20 or not 0 < trace.stat().st_size <= 16 << 30:
        raise ValueError('Client or trace size exceeds the bounded protocol')
    pe32(client); pe32(dll)
    dll_data = dll.read_bytes()
    if b'NORTHLIGHT STAND REPLAY BUILD:' not in dll_data:
        raise ValueError('A dedicated import-only stand DLL is required')
    if any(Path(p) == output or Path(p).is_relative_to(output) for p in inputs) or output.is_relative_to(root):
        raise ValueError('Private output must not overlap capture/source inputs')
    output.mkdir(parents=True)
    command = [str(Path(tools['root']) / 'bin/apitrace.exe'), 'dump', '--color=never', '--thread-ids=true',
               '--multiline=false', str(trace)]
    report = {'format': 1, 'command': command, 'inputs': inputs, 'diagnostic': True, 'fps_eligible': False,
              'scene_ready': False, 'typed_resource_aliases_supported': False, 'pass': False}
    try:
        env = os.environ.copy(); env['TEMP'] = env['TMP'] = str(output)
        code = dump_trace(command, output, env)
        report['dump_exit_code'] = code
        if code or (output / 'dump.stderr').read_bytes().strip():
            raise ValueError('Official parser failed or emitted a warning; evidence preserved')
        summary, records = join(calls(output / 'trace.dump'), session, journal)
        paths = (str(dll).encode('utf-8'), str(trace).encode('utf-8'))
        if any(not 0 < len(p) <= 4096 for p in paths):
            raise ValueError('Import path length exceeds protocol')
        table, offset = bytearray(), 0
        for record, packet in zip(records, packets):
            target, marker, device, draw, frame, epoch, trigger, thread, os_tid, size, crc, api = record
            table += struct.pack('<5Q6IQ2I', target, marker, device, draw, frame, epoch, trigger, thread, os_tid, size, crc, offset, api, 0)
            offset += size
        payload = session.encode('ascii') + b''.join(paths) + client.read_bytes() + table + b''.join(packets)
        header = bytearray(HEADER_BYTES); header[:8] = b'NLJOIN1\0'
        struct.pack_into('<IIQ6IQ8Q2I', header, 8, 1, HEADER_BYTES, HEADER_BYTES + len(payload), len(records), 1,
                         len(session), len(paths[0]), len(paths[1]), client.stat().st_size, offset,
                         summary['factory_call'], summary['create_call'], summary['factory'], summary['device'],
                         summary['draws'], summary['presents'], summary['resets'], summary['final_release'],
                         summary['trace_thread'], summary['os_thread'])
        for at, value in ((128, inputs[str(trace)]), (160, inputs[str(client)]), (192, inputs[str(dll)]), (224, hashlib.sha256(payload).hexdigest())):
            header[at:at + 32] = bytes.fromhex(value)
        with (output / 'scene.nljoin').open('xb') as target:
            target.write(header); target.write(payload)
        report.update(pass_=True, summary=summary, records=len(records), plan_sha256=digest(output / 'scene.nljoin'),
                      trace_join_validated=True, capture_layer='above-northlight')
        report['pass'] = report.pop('pass_')
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        report['inputs_unchanged'] = all(Path(p).is_file() and digest(p) == value for p, value in inputs.items())
        (output / 'validation.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        if not report['inputs_unchanged']:
            raise ValueError('A frozen input changed while compiling the import plan')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ('trace', 'snapshot-dir', 'client', 'stand-dll', 'tool-root', 'output'):
        parser.add_argument('--' + option, type=Path, required=True)
    print(json.dumps(compile_plan(parser.parse_args()), indent=2))


if __name__ == '__main__':
    main()
