#!/usr/bin/env python3
# northlight-test: requires=cxx
"""World mesh buffer store (mesh_buffer_store.h, 0.3.216 issue #66): content-key lookup over active and idle buffers,
claims that never touch the active table, atomic commit, cancel with invalidated keys, shared duplicate pages, idle-only
trim order, and a randomised invariant run. Native clang++ with ASan/UBSan. No game or GPU."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='mesh-buffer-store-') as temp:
    binary = Path(temp) / 'test'
    subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-UNDEBUG', '-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    *fp.test_include_flags(), str(HERE / 'test_mesh_buffer_store.cpp'), '-o', str(binary)], check=True)
    result = subprocess.run([str(binary)], text=True, capture_output=True)
    print(result.stdout + result.stderr, end='')
    result.check_returncode()
