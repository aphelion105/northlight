#!/usr/bin/env python3
# northlight-test: requires=cxx,world-cache
"""World mesh pages on real world-cache plans (Stormwind, Elwynn): exact geometry, conservative sub-batch bounds, sealing,
and (0.3.216, issue #66) the page-key reuse between consecutive builds for sequential against content-defined paging.
The MEASURE lines report page counts, 64 KiB-rounded capacity, build+hash time and the reused fraction. Native clang++."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='mesh-pages-real-') as temp:
    binary = Path(temp) / 'test'
    subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-UNDEBUG', '-O2', *fp.test_include_flags(), str(HERE / 'test_world_mesh_pages_real.cpp'),
                    str(fp.src('world_gi.cpp')), str(fp.src('world_mesh_plan.cpp')), '-o', str(binary)], check=True)
    result = subprocess.run([str(binary), str(fp.world_cache())], text=True, capture_output=True)
    print(result.stdout + result.stderr, end='')
    result.check_returncode()
