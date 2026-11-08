#!/usr/bin/env python3
# northlight-test: requires=cxx,client,stormlib timing
"""Cold skin-source decoder: generic oracle, live-size waves and build timing."""
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
sys.path.insert(0, str(fp.TESTS))
import client_fixtures

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, default=fp.REPO/'out/replay-skin-source/validation.json')
p.add_argument('--baseline', type=Path, help='Prior replay bounds/job/envelopes/skin headers')
a = p.parse_args()
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
paths = [path for path in fp.SRC.rglob('*') if path.is_file()] + [Path(__file__), fp.TESTS/'replay_skin_source.cpp', fp.TESTS/'test_replay_skin_safety.py', fp.TESTS/'test_terrain_snapshot.py']
inputs = {str(path): sha(path) for path in paths}
if a.baseline:
    inputs.update({str(a.baseline/header): sha(a.baseline/header) for header in ('replay_bounds_job.h', 'replay_bounds.h', 'replay_envelopes.inl', 'replay_skin_envelope.inl')})

def literal(path, name):
    return next(ast.literal_eval(n.value) for n in ast.parse(path.read_text()).body
                if isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and t.id == name for t in n.targets))

report = []
with tempfile.TemporaryDirectory(prefix='skin-source-', dir=fp.REPO/'out') as temporary:
    root = Path(temporary)
    (root/'d3d9.h').write_text(literal(fp.TESTS/'test_terrain_snapshot.py', 'stub'), encoding='utf-8')
    fixture = literal(fp.TESTS/'test_replay_skin_safety.py', 'harness').split('int main(')[0].replace('static ', '[[maybe_unused]] static ')
    (root/'skin_fixture.inc').write_text(fixture, encoding='utf-8')
    programs = [client_fixtures.four_bone_vs3(), client_fixtures.four_bone_body_vs3()]
    modes = [('O2', ['-O2']), ('sanitized', ['-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all']), ('timing', ['-O2'])]
    if a.baseline:
        modes.extend([('baseline-O2', ['-O2']), ('baseline-timing', ['-O2'])])
    for name, flags in modes:
        folder = root/name
        folder.mkdir()
        if name.startswith('baseline-'):
            for header in ('replay_bounds_job.h', 'replay_bounds.h', 'replay_envelopes.inl', 'replay_skin_envelope.inl'):
                (folder/header).write_bytes((a.baseline/header).read_bytes())
        binary = folder/'test'
        compiler = [fp.zig(), 'c++', '-target', 'x86_64-linux-musl'] if os.name == 'nt' else ['clang++']
        counters = [] if name.startswith('baseline-') else ['-DNORTHLIGHT_TEST_SKIN_SOURCE_COUNTERS']
        cmd = [*compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', *flags, *counters, '-I', folder, '-I', root, *fp.test_include_flags(), fp.TESTS/'replay_skin_source.cpp', '-o', binary]
        subprocess.run(list(map(str, cmd)), check=True, env=fp.zig_env())
        for program in programs[:1] if 'timing' in name else programs:
            run = ['wsl', '--exec', '/mnt/host/'+binary.drive[0].lower()+'/'+binary.relative_to(binary.anchor).as_posix()] if os.name == 'nt' else [str(binary)]
            program_arg = '/mnt/host/'+program.drive[0].lower()+'/'+program.relative_to(program.anchor).as_posix() if os.name == 'nt' else str(program)
            result = subprocess.run([*run, program_arg, *(['timing'] if 'timing' in name else [])], capture_output=True, text=True)
            print(name, result.stdout+result.stderr, flush=True)
            result.check_returncode()
            report.append({'name': name, 'program': str(program), 'program_sha256': sha(program), 'stdout': result.stdout})
assert all(sha(Path(path)) == digest for path, digest in inputs.items()), 'Source changed during validation'
a.output.parent.mkdir(exist_ok=True, parents=True)
a.output.write_text(json.dumps({'pass': True, 'inputs': inputs, 'runs': report, 'scope': 'Deterministic bounds-clock correctness and isolated source-decoder microbenchmark; no gameplay FPS claim.'}, indent=2)+'\n', encoding='utf-8', newline='\n')
