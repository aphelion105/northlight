#!/usr/bin/env python3
# northlight-test: requires=zig
"""Production writer: exact bytes, cross-kind sharing, row padding and IO failures."""
import hashlib
import os
import struct
import subprocess
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
from capture_files import read_bytes, BLOBS

out = fp.output_dir()
binary = out / ('diagnostic-files.exe' if os.name == 'nt' else 'diagnostic-files')
command = [str(fp.zig()), 'c++', *(['-target', 'x86-windows-gnu'] if os.name == 'nt' else []),
           '-std=c++17', '-O2', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
           *fp.test_include_flags(), str(fp.TESTS / 'diagnostic_files.cpp'), '-o', str(binary)]
subprocess.run(command, env=fp.zig_env(), check=True)
folder = out / 'unicode-\u00dcber-\u4e16\u754c'
subprocess.run([str(binary), str(out)], check=True)
raw, compact = folder / 'raw', folder / 'compact'
for p in raw.iterdir():
    if p.is_file():
        assert read_bytes(compact / p.name) == p.read_bytes(), p.name
assert read_bytes(compact / 'padded.fgr') == struct.pack('<5I', 0x31524746, 1, 2, 21, 4) + bytes(range(1, 9))
assert (compact / 'padded.fgr').read_bytes() == (compact / 'packed.fgr').read_bytes()
for p in (compact / BLOBS).iterdir():
    if p.is_file():
        assert hashlib.sha256(p.read_bytes()).hexdigest() == p.name
assert len(list((compact / BLOBS).iterdir())) == 5
print('DIAGNOSTIC_FILES_ROUNDTRIP_PASS independent_python_hashes unicode_paths')
