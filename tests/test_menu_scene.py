#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Menu scene capture, isolation, archive transport and environment contracts."""
from pathlib import Path
import subprocess
import sys
import importlib.util
import struct
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
spec = importlib.util.spec_from_file_location('prepare_scene', fp.SCRIPTS / 'prepare_native_scene.py')
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)

for optimization in ('-O0', '-O2'):
    exe = fp.output_dir() / f'menu-scene{optimization}.exe'
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17',
                    optimization, '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                    *fp.test_include_flags(), str(fp.TESTS / 'menu_scene.cpp'),
                    '-o', str(exe)], env=fp.zig_env(), check=True)
    archive = exe.with_suffix('.nlsnap')
    subprocess.run([str(exe), str(archive)], check=True, timeout=30)
    assert prepare.inspect_snapshot(archive)['trigger_kind'] == 4
    assert prepare.inspect_snapshot(str(archive)+'.empty')['trigger_kind'] == 4
    original = archive.read_bytes()
    for offset, value in [(len(original)-284, 3), (len(original)-284+20, 0),
                          (len(original)-284+152, 0x7fc00000)]:
        corrupt = bytearray(original)
        struct.pack_into('<I', corrupt, offset, value)
        struct.pack_into('<I', corrupt, len(corrupt)-4, zlib.crc32(corrupt[:-4]))
        bad = archive.with_suffix('.bad')
        bad.write_bytes(corrupt)
        try:
            prepare.inspect_snapshot(bad)
        except ValueError:
            pass
        else:
            raise AssertionError('Invalid menu snapshot accepted')
print('PASS menu archives imported by production tooling; malformed cameras rejected')
