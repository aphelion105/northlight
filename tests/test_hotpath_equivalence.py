#!/usr/bin/env python3
# northlight-test: requires=zig
"""Compare optimized production algorithms to the frozen 9537631 implementations."""
import subprocess
import json
import re
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

manifest = json.loads((fp.SHADERS / 'world-shader-build.json').read_text())['shaders']
weights = {constant: manifest[entry]['static_instruction_slots'] for constant, entry in
           (('DirectSlots', 'LocalDirect'), ('Direct16Slots', 'LocalDirect16'), ('FogSlots', 'LocalFog'))}
production = fp.src('local_light_batches.h').read_text()
for constant, slots in weights.items():
    assert int(re.search(r'\b' + constant + r'=(\d+)', production)[1]) == slots, constant

windows = sys.platform == 'win32'
compiler = [str(fp.zig()), 'c++', '-target', 'x86-windows-gnu'] if windows else ['clang++']
variants = [('O2', ['-O2']), ('O0', ['-O0'])] if windows else [
    ('O2', ['-O2']), ('asan', ['-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all'])]
with tempfile.TemporaryDirectory(prefix='hotpaths-', dir=fp.output_dir()) as tmp:
    folder = Path(tmp)
    sources = folder / 'fixtures' / 'hotpaths-9537631'
    shutil.copytree(fp.FIXTURES / 'hotpaths-9537631', sources)
    source = folder / 'test_hotpath_equivalence.cpp'
    shutil.copyfile(fp.TESTS / source.name, source)
    # Compare the algorithms using the same shader cost and leave the archive intact.
    batches = sources / 'local_light_batches.h'
    text = batches.read_text()
    for constant, slots in weights.items():
        text, matches = re.subn(r'\b' + constant + r'=\d+', f'{constant}={slots}', text)
        assert matches == 1, constant
    batches.write_text(text)
    # Keep the current small-caster policy while testing the frozen ranking.
    eligible = re.compile(r'^inline bool eligible\([^\n]+\n.*?^\}', re.M | re.S)
    current_policy = eligible.search(fp.src('point_light_shadow.h').read_text())[0]
    point = sources / 'point_light_shadow.h'
    text, matches = eligible.subn(lambda _: current_policy, point.read_text())
    assert matches == 1
    point.write_text(text)
    for name, flags in variants:
        exe = folder / (name + ('.exe' if windows else ''))
        subprocess.run([*compiler, '-std=c++17', '-UNDEBUG', '-Wall', '-Wextra', '-Werror', *flags,
                        *fp.test_include_flags(), str(source), '-o', str(exe)], env=fp.zig_env() if windows else None, check=True)
        print(name, subprocess.check_output([str(exe)], text=True), end='', flush=True)
