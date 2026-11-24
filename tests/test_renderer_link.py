#!/usr/bin/env python3
# northlight-test: requires=zig
"""The production link path removes dead code, preserves exports/PDBs and handles spaces."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
import os
import struct
from unittest import mock
import build_renderer

out = fp.output_dir()/'path with spaces'
out.mkdir(parents=True, exist_ok=True)
source, exports, dll = out/'input.cpp', out/'exports.def', out/'result.dll'
source.write_text('extern "C" const char* kept(){return "NL_KEPT_CONTROL";}\n'
                  'extern "C" const char* unused(){return "NL_DEAD_CONTROL";}\n')
exports.write_text('EXPORTS\n kept @7\n')
command = [str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-shared',
           '-ffunction-sections', '-fdata-sections', str(source), str(exports), '-o', str(dll)]
with mock.patch.object(build_renderer.northlight_paths, 'out', return_value=out):
    build_renderer.link_renderer(command, fp.zig_env(), dll)
data = dll.read_bytes()
assert b'NL_KEPT_CONTROL' in data and b'NL_DEAD_CONTROL' not in data
assert dll.with_suffix('.pdb').is_file()
pe = struct.unpack_from('<I', data, 0x3c)[0]
assert struct.unpack_from('<H', data, pe+4)[0] == 0x14c
print('PASS COFF removal: unused function/data gone; referenced export, x86 DLL and PDB retained; spaced paths')
# From the cache's parent (the repository root by default) Zig prints its cache inputs relative to the working directory.
os.chdir(Path(fp.zig_env()['ZIG_GLOBAL_CACHE_DIR']).parent)
relative = out/'relative.dll'
with mock.patch.object(build_renderer.northlight_paths, 'out', return_value=out):
    build_renderer.link_renderer(command[:-1] + [str(relative)], fp.zig_env(), relative)
assert b'NL_KEPT_CONTROL' in relative.read_bytes() and b'NL_DEAD_CONTROL' not in relative.read_bytes()
print('PASS relink from the Zig cache parent: relative cache inputs')
