# northlight-test: requires=windows-exe,zig timing
"""Exact deferred state traces and warm CPU submission versus the shipped build."""
import json
import re
import subprocess
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

out = fp.output_dir() / 'deferred-blocks'
out.mkdir(parents=True, exist_ok=True)
fixture = (fp.TESTS / 'displacement_state_reuse.cpp').read_text().split('int main(')[0]
fixture = fixture.replace('struct MaterialCaptureStateTestAccess {',
                          'struct MaterialCaptureStateTestAccess {\n static void deferredBlocks();\n static void replayBindings();\n static void replayConstantRanges();')
(out / 'material_bindings_reference.h').write_bytes(fixture.encode())
(out / 'material_packet_bindings.h').write_bytes((fp.TESTS / 'test_material_batch_bindings.cpp').read_text().rsplit('int main(', 1)[0].encode())
control = out / 'control'
control.mkdir(exist_ok=True)
(control / 'auto_bump_renderer.h').write_bytes(subprocess.check_output(
    ['git', 'show', 'ed4f773f:src/water/auto_bump_renderer.h'], cwd=fp.REPO))
exes = {}
for name, headers in [('control', control), ('current', fp.src('auto_bump_renderer.h').parent)]:
    exe = out / (name + '.exe')
    command = [fp.zig(), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG', '-std=c++17',
               '-Wno-microsoft-exception-spec', '-I' + str(headers), '-I' + str(out),
               *fp.test_include_flags(), fp.TESTS / 'material_deferred_blocks.cpp', '-luser32', '-luuid', '-o', exe]
    with (out / (name + '-compile.log')).open('w') as log:
        subprocess.run(list(map(str, command)), env=fp.zig_env(), stdout=log, stderr=subprocess.STDOUT, check=True)
    exes[name] = exe
records, reference = [], None
for turn, name in enumerate(['control', 'current', 'current', 'control']):
    run = subprocess.run([str(exes[name])], capture_output=True, text=True, check=True)
    (out / f'{turn}-{name}.log').write_text(run.stdout + run.stderr, encoding='utf-8')
    assert 'DEFERRED_BLOCKS_PASS' in run.stdout
    cases = [line for line in run.stdout.splitlines() if line.startswith('DEFERRED_BLOCK_CASE')]
    assert len(cases) == 3
    if reference is None:
        reference = cases
    assert cases == reference, (name, cases, reference)
    for line in run.stdout.splitlines():
        if line.startswith('DEFERRED_BLOCK_TIMING'):
            row = dict(re.findall(r'(\w+)=([^ ]+)', line))
            records.append(dict(name=name, turn=turn, **row))
    print(name, run.stdout.strip(), flush=True)
(out / 'validation.json').write_text(json.dumps(dict(passed=True, records=records, exact_cases=reference,
    scope='143 deferred packets, fake native device, real CPU cache and flush; no GPU or gameplay FPS claim.'), indent=2) + '\n')
