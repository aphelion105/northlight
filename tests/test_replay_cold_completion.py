#!/usr/bin/env python3
# northlight-test: requires=cxx timing
"""Cold source completion, exact rigid source enclosure and unchanged fail-open budgets."""
import argparse
import ast
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

HERE = Path(__file__).resolve().parent
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,default=fp.REPO/'out/replay-cold-completion/validation.json')
args=parser.parse_args()
paths=[fp.src(name) for name in ('replay_bounds.h','replay_bounds_budget.h','replay_envelopes.inl','replay_bounds_job.h','replay_bounds_schedule.h')]+[Path(__file__),HERE/'test_replay_cold_completion.cpp']
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
inputs={str(path):sha(path) for path in paths}

def literal(path, name):
    return next(ast.literal_eval(n.value) for n in ast.parse(path.read_text()).body
                if isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and t.id == name for t in n.targets))

with tempfile.TemporaryDirectory(prefix='northlight-cold-completion-', dir=fp.REPO/'out') as temporary:
    root = Path(temporary)
    (root/'d3d9.h').write_text(literal(HERE/'test_terrain_snapshot.py', 'stub'))
    fixture = literal(HERE/'test_actor_deformation.py', 'harness').split('int main(')[0]
    (root/'actor_fixture.inc').write_text(fixture)
    source = fp.src('replay_bounds_job.h').read_text()
    reference = source[source.index('inline Totals schedule('):source.index('// Render thread, before start():')]
    reference = reference.replace('inline Totals schedule(', 'inline Totals referenceSchedule(')
    reference = reference.replace('return visit(index,3);', 'return visit(index,0);')
    reference = '\n'.join(line for line in reference.splitlines() if 'NorthlightReplaySchedule::finishCold(' not in line and 'const auto cold=' not in line)
    (root/'reference_schedule.inc').write_text('namespace NorthlightReplayBoundsJob {\n'+reference+'}\n')
    terminal = source[source.index('inline Totals schedule('):source.index('// Render thread, before start():')]
    terminal = terminal.replace('inline Totals schedule(', 'inline Totals beforeTerminalSchedule(').replace('if(live())cache.continuePendingAfterReady(buildBudget);', '')
    terminal = '\n'.join(line for line in terminal.splitlines() if 'NorthlightReplaySchedule::finishCold(' not in line and 'const auto cold=' not in line)
    (root/'terminal_reference.inc').write_text('namespace NorthlightReplayBoundsJob {\n'+terminal+'}\n')
    previous = source[source.index('inline Totals schedule('):source.index('// Render thread, before start():')]
    previous = previous.replace('inline Totals schedule(', 'inline Totals previousTerminalSchedule(')
    previous = '\n'.join('    if(live())cache.continuePendingAfterReady(buildBudget);' if 'NorthlightReplaySchedule::finishCold(' in line else line for line in previous.splitlines() if 'const auto cold=' not in line)
    (root/'previous_terminal.inc').write_text('namespace NorthlightReplayBoundsJob {\n'+previous+'}\n')
    report = []
    for name, flags in [('O2', ['-O2', '-DNORTHLIGHT_TEST_CLOCK']),
                        ('scalar-indices', ['-O2', '-DNORTHLIGHT_TEST_CLOCK', '-DNORTHLIGHT_REPLAY_BOUNDS_SCALAR_INDICES']),
                        ('UBSan', ['-O1', '-g', '-fsanitize=undefined', '-fno-sanitize-recover=all', '-DNORTHLIGHT_TEST_CLOCK']),
                        ('timing', ['-O2'])]:
        binary = root/name
        compiler = [fp.zig(), 'c++', '-target', 'x86_64-linux-musl'] if os.name == 'nt' else ['clang++']
        command = list(map(str, [*compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', *flags, '-I', root, *fp.test_include_flags(), HERE/'test_replay_cold_completion.cpp', '-o', binary]))
        subprocess.run(command, check=True, env=fp.zig_env())
        run = ['wsl', '--exec', '/mnt/host/'+binary.drive[0].lower()+'/'+binary.relative_to(binary.anchor).as_posix()] if os.name == 'nt' else [str(binary)]
        result = subprocess.run(run, capture_output=True, text=True)
        if result.returncode:
            print(result.stdout+result.stderr, flush=True)
        result.check_returncode()
        print(name, result.stdout, flush=True)
        report.append({'name': name, 'stdout': result.stdout})
    output = args.output
    output.parent.mkdir(exist_ok=True, parents=True)
    assert all(sha(Path(path))==digest for path,digest in inputs.items())
    output.write_text(json.dumps({'pass': True, 'inputs':inputs, 'runs': report, 'scope': 'Authored cold-wave workload; no gameplay FPS claim. Same source and current pose, unchanged time/operation/byte limits, certified interval evaluator.'}, indent=2)+'\n')
