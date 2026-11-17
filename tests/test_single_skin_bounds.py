# northlight-test: requires=windows-exe,zig,client,stormlib
"""Exact single-bone capture bounds, including changing palettes and failure output."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
import client_fixtures

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--benchmark', action='store_true')
p.add_argument('--baseline-ref', default='fe00d60a')
a = p.parse_args()
out = fp.output_dir() / 'single-skin-bounds'
out.mkdir(parents=True, exist_ok=True)
baseline = subprocess.check_output(['git', 'show', a.baseline_ref + ':src/replay/actor_deformation.h'])
(out / 'legacy_actor_deformation.h').write_text(baseline.decode().replace('NorthlightActorDeformation', 'LegacyActorDeformation'))
client_fixtures.actor_client_programs(out)
inputs = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in
          [Path(__file__), fp.TESTS / 'single_skin_bounds.cpp', *fp.SRC.rglob('*.h'), *fp.SRC.rglob('*.inl'), out / 'actor_client_programs.h']}
rows = []
for opt in ['O2', 'O0']:
    exe = out / ('bounds-' + opt + '.exe')
    command = [str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-' + opt, '-UNDEBUG', '-std=c++17',
               '-I', str(out), *fp.test_include_flags(), str(fp.TESTS / 'single_skin_bounds.cpp'), '-o', str(exe)]
    with (out / (opt + '-compile.log')).open('w') as log:
        subprocess.run(command, env=fp.zig_env(), stdout=log, stderr=subprocess.STDOUT, check=True)
    run = subprocess.run([str(exe), *(['timing'] if a.benchmark and opt == 'O2' else [])], capture_output=True, text=True, check=True)
    (out / (opt + '.log')).write_text(run.stdout + run.stderr)
    print(run.stdout, flush=True)
    assert run.stdout.count('SINGLE_BOUNDS_PASS') == 4
    rows.extend(dict(re.findall(r'(\w+)=([^ ]+)', line)) for line in run.stdout.splitlines() if line.startswith('SINGLE_BOUNDS_TIMING '))
assert all(hashlib.sha256(Path(path).read_bytes()).hexdigest() == digest for path, digest in inputs.items())
(out / 'validation.json').write_text(json.dumps({'pass': True, 'inputs': inputs, 'baseline_ref': a.baseline_ref,
    'baseline_sha256': hashlib.sha256(baseline).hexdigest(), 'timing': rows, 'scope': 'CPU vertex bounds only; no complete capture or game FPS claim'}, indent=2) + '\n')
