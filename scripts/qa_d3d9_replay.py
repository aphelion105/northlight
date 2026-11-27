"""Bind the marked QA interval to a closed backend D3D9 trace."""
import re
import subprocess


MARKER = re.compile(r'^\d+ @(\d+) D3DPERF_SetMarker\(.*wszName = L"NLQA1;present=(\d+)"\)$')
PRESENT = re.compile(r'^(\d+) @(\d+) IDirect3D(?:Device|SwapChain)9(?:Ex)?::Present(?:Ex)?\(.*\) = (\S+)$')
RELEASE = re.compile(r'^(\d+) @\d+ IDirect3DDevice9(?:Ex)?::Release\(.*\) = 0$')


def dxvk_runtime(log):
    devices = set()
    configurations = []
    lines = iter(log.splitlines())
    for line in lines:
        if line == 'info:  Effective configuration:':
            options = {}
            for option in lines:
                match = re.fullmatch(r'info:    ((?:dxvk|d3d9)\.[A-Za-z0-9_]+) = ([^;#\s]+)', option)
                if not match:
                    break
                options[match[1]] = match[2]
            configurations.append(options)
        elif line == 'info:  Creating device:':
            device = next(lines, '')
            if re.fullmatch(r'info:  .+:', device):
                devices.add(device[7:-1])
    if len(devices) != 1 or not configurations or any(config != configurations[0] for config in configurations):
        raise ValueError('DXVK must report one device and one consistent effective QA profile')
    return {'device': devices.pop(), 'options': configurations[0]}


def trace_window(lines, first, last):
    marked = {}
    pending = None
    last_call = 0
    for line in lines:
        line = line.rstrip()
        if marker := MARKER.fullmatch(line):
            tid, frame = map(int, marker.groups())
            if pending is not None or frame in marked:
                raise ValueError('Backend QA trace has overlapping or duplicate markers')
            pending = tid, frame
        elif present := PRESENT.fullmatch(line):
            call, tid, result = present.groups()
            last_call = int(call)
            if pending is None:
                continue
            if int(tid) != pending[0]:
                continue
            if result not in ('D3D_OK', 'S_OK', '0'):
                raise ValueError('A marked backend Present failed')
            marked[pending[1]] = last_call
            pending = None
        elif release := RELEASE.fullmatch(line):
            last_call = int(release[1])
    if pending or first >= last or len(marked) < last - first + 1 or any(frame not in marked for frame in range(first, last + 1)):
        raise ValueError('Backend QA trace is missing a marked Present')
    return {'preview_calls': {str(frame): marked[frame] for frame in (first, (first + last) // 2, last)},
            'last_boundary_call': last_call}


def certify(trace, tools, selection, output, timeout):
    calls = output / 'qa-d3d9-calls.txt'
    command = [tools / 'bin/apitrace.exe', 'dump', '--color=never', '--thread-ids=true', '--multiline=false',
               '--verbose', '--grep=D3DPERF_SetMarker|::Present|IDirect3DDevice9.*::Release', trace]
    with calls.open('xb') as log, (output / 'qa-d3d9-dump.stderr').open('xb') as errors:
        subprocess.run(command, stdout=log, stderr=errors, check=True, timeout=timeout,
                       creationflags=subprocess.CREATE_NO_WINDOW)
    with calls.open(encoding='utf-8', errors='replace') as log:
        return {**selection, **trace_window(log, selection['first_present'], selection['last_present'])}
