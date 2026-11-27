#!/usr/bin/env python3
# northlight-test:
"""Check the QA scene mark and relocatable archive inputs."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from zipfile import ZipFile
import northlight_paths as fp

sys.path.insert(0, str(fp.SCRIPTS))
from run_northrend_baseline import LOG, qa_archive, qa_selection
from qa_d3d9_replay import dxvk_runtime, trace_window
from qa_api_replay import present_window


mark = 'QA_SCENE saved first=47 last=468 durationMs=15018'
assert qa_selection(['WORLD camera']) is None
assert qa_selection([mark]) == {'first_present': 47, 'last_present': 468, 'duration_ms': 15018}
for rows in ([mark, mark], [mark.replace('last=468', 'last=47')], [mark.split(' durationMs=')[0]]):
    try:
        qa_selection(rows)
    except ValueError:
        pass
    else:
        raise AssertionError(f'Accepted malformed QA scene mark: {rows}')

root = fp.output_dir() / 'stage'
client = root / 'client'
run = root / 'scene.1'
sidecar = run / 'snapshots/capture-123'
client.mkdir(parents=True)
sidecar.mkdir(parents=True)
for parent, names in ((root, ('baseline.json', 'northlight-renderer.ini')),
                      (client, ('northlight-quality.ini', 'Wow.exe')),
                      (run, ('validation.json', 'wow.trace', LOG, 'capture-wrapper.dll')),
                      (sidecar, ('capture.journal.jsonl',))):
    for name in names:
        (parent / name).write_text(name, encoding='ascii')
archive = qa_archive(run, root, client, sidecar)
assert archive.name == 'scene.1.zip'
with ZipFile(archive) as package:
    assert set(package.namelist()) == {'baseline.json', 'northlight-renderer.ini', 'northlight-quality.ini',
                                     'validation.json', 'wow.trace', LOG, 'capture-wrapper.dll',
                                     'snapshots/capture-123/capture.journal.jsonl'}
    assert package.testzip() is None

lines = [line for frame in range(3) for line in (
    f'{2*frame+10} @1 D3DPERF_SetMarker(col = 0, wszName = L"NLQA1;present={frame}")',
    f'{2*frame+11} @1 IDirect3DDevice9::Present(this = 0x14815be0000) = D3D_OK')]
lines.append('16 @1 IDirect3DDevice9::Present(this = 0x14815be0000) = D3D_OK')
assert trace_window(lines, 0, 2) == {'preview_calls': {'0': 11, '1': 13, '2': 15}, 'last_boundary_call': 16}
try:
    trace_window(lines[:2] + lines[4:], 0, 2)
except ValueError:
    pass
else:
    raise AssertionError('Accepted a missing x64 QA Present')

profile = ('info:  Effective configuration:\ninfo:    d3d9.customVendorId = 1002\n'
           'info:  Vulkan: 1.4\ninfo:  Creating device:\ninfo:  NVIDIA GeForce RTX 4090:\n')
assert dxvk_runtime(profile) == {'device': 'NVIDIA GeForce RTX 4090', 'options': {'d3d9.customVendorId': '1002'}}

assert present_window(list(range(1, 21)), list(range(101, 121)), 20, 1) == {
    'first_present': 5, 'last_present': 20, 'duration_ms': 15_000, 'preview_calls': [105, 112, 120]}
try:
    present_window([1, 2, 3], [101, 102], 3, 1)
except ValueError:
    pass
else:
    raise AssertionError('Paired unequal ETW and API frame sequences')
print('PASS QA scene mark and archive')
