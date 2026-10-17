#!/usr/bin/env python3
# northlight-test: requires=cxx
"""World mesh pages (world_mesh_pages.h, world_mesh_page_gpu.h, mesh_page_key.h): exact paging, content anchors
(0.3.216, issue #66), page content keys and the page upload cursor. Native clang++ with ASan/UBSan. No game or GPU."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='mesh-pages-') as temp:
    for name, extra in (('test_world_mesh_pages', [fp.src('world_gi.cpp'), fp.src('world_mesh_plan.cpp')]), ('test_world_mesh_page_gpu', [])):
        binary = Path(temp) / name
        subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-UNDEBUG', '-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                        *fp.test_include_flags(), str(HERE / (name + '.cpp')), *map(str, extra), '-o', str(binary)], check=True)
        result = subprocess.run([str(binary)], text=True, capture_output=True)
        print(result.stdout + result.stderr, end='')
        result.check_returncode()
