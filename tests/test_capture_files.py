#!/usr/bin/env python3
# northlight-test:
"""Compact archives preserve every byte and reject broken references/unsafe paths."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import zipfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
from capture_files import pack, unpack, read_bytes, MAGIC, BLOBS, CaptureInput


def rejects(fn):
    try:
        fn()
    except (ValueError, OSError, zipfile.BadZipFile):
        return
    raise AssertionError('Invalid capture accepted')


with tempfile.TemporaryDirectory(dir=fp.output_dir()) as folder:
    root = Path(folder)
    source = root / 'old'
    source.mkdir()
    image = struct.pack('<5I', 0x31524746, 32, 32, 21, 4) + bytes(range(256)) * 16
    expected = {f'capture-1-{i}.fgr': image for i in range(24)}
    expected.update({'capture-1-vs.bin': b'bytecode', 'capture-1-ps.bin': b'bytecode',
                     'capture-1-state.f32': b'\0' * 32, 'capture-1-inputs.jsonl': b'{"event":"end"}\n'})
    for name, data in expected.items():
        (source / name).write_bytes(data)
    before = {p.name: p.read_bytes() for p in source.iterdir()}
    report = pack(source, root / 'compact.zip')
    assert report['duplicate_binary_files'] == 24
    with zipfile.ZipFile(root / 'compact.zip') as archive:
        blobs = [n for n in archive.namelist() if n.startswith(BLOBS + '/')]
        assert len(blobs) == 3
        assert sum(archive.getinfo(n).file_size for n in blobs) == len(image) + 8 + 32
        archive.extractall(root / 'compact')  # Only this test's own generated safe archive.
    for name, data in expected.items():
        assert read_bytes(root / 'compact' / name) == data
    unpack(root / 'compact.zip', root / 'restored')
    unpack(root / 'compact', root / 'restored-folder')
    unpack(source, root / 'restored-raw')
    pack(root / 'compact.zip', root / 'repacked.zip')
    unpack(root / 'repacked.zip', root / 'restored-repacked')
    for target in ('restored', 'restored-folder', 'restored-raw', 'restored-repacked'):
        assert {p.relative_to(root / target).as_posix(): p.read_bytes() for p in (root / target).rglob('*') if p.is_file()} == expected
    assert {p.name: p.read_bytes() for p in source.iterdir()} == before
    rejects(lambda: pack(source, source / 'inside.zip'))
    rejects(lambda: unpack(source, root / 'restored'))
    for index, name in enumerate(('../escape', '/absolute', 'C:/escape', 'a/../b')):
        bad = root / f'bad-{index}.zip'
        with zipfile.ZipFile(bad, 'w') as archive:
            archive.writestr(name, b'bad')
        rejects(lambda: unpack(bad, root / f'bad-out-{index}'))
    for index, payload in enumerate((MAGIC + b'../escape 1\n', MAGIC + b'a' * 64 + b' 999999999999\n',
                                     MAGIC + b'a' * 64 + b' 1\n')):
        bad = root / f'ref-{index}.zip'
        with zipfile.ZipFile(bad, 'w') as archive:
            archive.writestr('image.fgr', payload)
            if index == 2:
                archive.writestr(BLOBS + '/' + 'a' * 64, b'x')
        rejects(lambda: unpack(bad, root / f'ref-out-{index}'))
        assert not (root / f'ref-out-{index}').exists()
    duplicate = root / 'duplicate.zip'
    with zipfile.ZipFile(duplicate, 'w') as archive:
        archive.writestr('file.bin', b'one')
        archive.writestr('FILE.bin', b'two')
    rejects(lambda: CaptureInput(duplicate))
    (fp.output_dir() / 'capture-files.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print('CAPTURE_FILES_PASS legacy compact zip folder roundtrip corruption traversal immutable_input')
