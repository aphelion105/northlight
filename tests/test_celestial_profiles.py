#!/usr/bin/env python3
# northlight-test: requires=cxx,client
"""Runner for test_celestial_profiles.cpp (native clang++, plain and ASan/UBSan; no game, Wine or GPU).

The test reads <root>/celestial-profiles.ini and the real zone IDs in <root>/world-cache/fog.
The ini is the tracked client-config copy; world-cache comes from the client root
(--client, default: northlight_paths.client_root()). Both are only read. Writes nothing outside a temporary folder.
"""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import argparse, os, shutil, subprocess, tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('--client', type=Path, default=fp.client_root())
parser.add_argument('--ini', type=Path, default=fp.REPO/'client-config' / 'celestial-profiles.ini')
args = parser.parse_args()
fog = args.client / 'world-cache' / 'fog'
assert fog.is_dir(), f'{fog} missing: pass --client <WoW client root>'
with tempfile.TemporaryDirectory(prefix='northlight-celestial-profiles-') as tmp:
    root = Path(tmp) / 'root'
    root.mkdir()
    shutil.copyfile(args.ini, root / 'celestial-profiles.ini')
    compiler = [str(fp.zig()), 'c++', '-target', 'x86-windows-gnu'] if os.name == 'nt' else ['clang++']
    modes = [('O2', ['-O2'])]
    if os.name != 'nt': modes.append(('asan', ['-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all']))
    for label, flags in modes:
        exe = Path(tmp) / ('test-' + label + ('.exe' if os.name == 'nt' else ''))
        subprocess.run([*compiler, '-std=c++17', '-UNDEBUG', '-Wall', '-Wextra', *flags, *fp.test_include_flags(),
                        str(HERE / 'test_celestial_profiles.cpp'), '-o', str(exe)], env=fp.zig_env() if os.name == 'nt' else None, check=True)
        print(label, subprocess.check_output([str(exe), str(root), str(fog)], text=True), end='', flush=True)
