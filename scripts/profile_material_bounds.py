"""Compare the complete warm CPU envelope path with a frozen Git revision."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import statistics
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tests')]
import northlight_paths as fp
import client_fixtures

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--reference', required=True)
p.add_argument('--output', type=Path, default=fp.output_dir() / 'material-bounds-cpu')
p.add_argument('--control', action='store_true', help='Compare the reference against itself')
a = p.parse_args()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=True)
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
files = [q for q in fp.SRC.rglob('*') if q.is_file()]
files += [Path(__file__), fp.TESTS / 'material_bounds_cpu.cpp', fp.TESTS / 'material_envelope_fast.cpp']
inputs = {str(q): sha(q) for q in files}
revision = subprocess.check_output(['git', 'rev-parse', a.reference]).decode('utf-8').strip()
old_bounds = subprocess.check_output(['git', 'show', revision + ':src/replay/replay_bounds.h']).decode('utf-8')
old_envelopes = subprocess.check_output(['git', 'show', revision + ':src/replay/replay_envelopes.inl'])
(out / 'Before.inl').write_bytes(old_envelopes)
start = old_bounds.index('inline bool singleSkinIntervals(')
end = old_bounds.index('} // namespace detail', start)
(out / 'BeforeKernel.inl').write_text(old_bounds[start:end].replace('singleSkinIntervals', 'beforeSingleSkinIntervals'), encoding='utf-8')
overlay = []
if a.control:
    folder = out / 'control'
    folder.mkdir(exist_ok=True)
    for name in ('replay_bounds.h', 'replay_envelopes.inl', 'replay_skin_envelope.inl'):
        (folder / name).write_bytes(subprocess.check_output(['git', 'show', revision + ':src/replay/' + name]))
    overlay = ['-I' + str(folder)]
header = client_fixtures.actor_client_programs(out)
data = client_fixtures.four_bone_vs3().read_bytes()
words = struct.unpack(f'<{len(data)//4}I', data)
with header.open('a', encoding='utf-8') as f:
    f.write('\nnamespace ClientShaders { static const uint32_t FourBoneVs3[]={' + ','.join(map(str, words)) + '}; }\n')
rows = []
for opt in ('-O0', '-O2'):
    exe = out / (opt[1:] + '.exe')
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', opt, '-UNDEBUG',
                    '-I' + str(out), *overlay, '-I' + str(fp.TESTS), *fp.test_include_flags(),
                    str(fp.TESTS / 'material_bounds_cpu.cpp'), '-o', str(exe)], env=fp.zig_env(), check=True)
    result = subprocess.run([str(exe), *(['timing'] if opt == '-O2' else [])], capture_output=True, text=True, timeout=180)
    (out / (opt[1:] + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
    if result.returncode:
        print(result.stdout + result.stderr)
    result.check_returncode()
    assert 'BOUNDS_CPU_PASS' in result.stdout
    if opt == '-O2':
        rows = [dict(re.findall(r'(\w+)=([^ ]+)', line)) for line in result.stdout.splitlines()
                if line.startswith('BOUNDS_CPU ') and 'warm=0 ' in line]
timing = []
for mode in range(5):
    row = {'mode': mode}
    for key, old in [('before_ms', '1'), ('after_ms', '0')]:
        selected = [r for r in rows if r['mode'] == str(mode) and r['old'] == old]
        complete = [r for r in selected if r['valid'] == '1536' and r['operations'] == r['expectedOperations']]
        assert len(complete) >= 6, (mode, key, 'too few complete batches', len(complete))
        row[key] = statistics.median(float(r['ms']) for r in complete)
        row[key + '_incomplete_batches'] = len(selected) - len(complete)
    timing.append(row)
assert all(sha(Path(q)) == value for q, value in inputs.items()), 'Source changed during benchmark'
report = {'pass': True, 'reference': revision, 'control': a.control, 'inputs': inputs,
          'frozen_inputs': {name: sha(out / name) for name in ('Before.inl', 'BeforeKernel.inl', 'actor_client_programs.h')},
          'timing': timing, 'samples': rows,
          'scope': 'CPU component: 16 draws per frame, 96 poses, real clock overhead, ABBA, complete batches only. Not game FPS.'}
(out / 'validation.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print(json.dumps({'reference': revision, 'control': a.control, 'timing': timing}, indent=2))
