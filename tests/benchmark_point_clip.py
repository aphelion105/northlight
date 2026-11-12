# northlight-test: requires=windows-exe,zig manual timing
"""Compare frozen and current prepared point-shadow culling in one x86 process."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import northlight_paths as fp

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline', type=Path, required=True, help='Frozen point_light_shadow.h')
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
out = args.output.resolve();out.mkdir(parents=True, exist_ok=False)
inputs = [args.baseline.resolve(), fp.src('point_light_shadow.h'), fp.TESTS / 'point_clip_benchmark.cpp', Path(__file__)]
digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
hashes = {str(p): digest(p) for p in inputs}
(out / 'baseline_point_light_shadow.h').write_text(args.baseline.read_text().replace('NorthlightPointShadow', 'OldPointShadow'), newline='\n')
exe = out / 'point-clip.exe'
command = [fp.zig(), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
           *fp.test_include_flags(), '-I', out, fp.TESTS / 'point_clip_benchmark.cpp', '-o', exe]
subprocess.run(list(map(str, command)), env=fp.zig_env(), check=True)
result = subprocess.run([str(exe)], capture_output=True, text=True, check=True)
(out / 'run.log').write_text(result.stdout + result.stderr)
assert 'CLIP_BENCH_PASS' in result.stdout
rows = [dict(re.findall(r'(\w+)=([^ ]+)', line)) for line in result.stdout.splitlines()
        if line.startswith('CLIP_BENCH ') and 'warm=0 ' in line]
summary = []
for spread in (32, 96):
    for mask in (63, 3, 1):
        entry = {'spread': spread, 'requested': mask, 'candidates': 163840}
        for mode in (0, 1):
            values = [float(r['cpuMs']) for r in rows if int(r['mode']) == mode and int(r['spread']) == spread and int(r['requested']) == mask]
            assert len(values) == 6
            entry['after_ms' if mode else 'before_ms'] = statistics.median(values)
        summary.append(entry)
setup_rows = [dict(re.findall(r'(\w+)=([^ ]+)', line)) for line in result.stdout.splitlines()
              if line.startswith('CLIP_SETUP ') and 'warm=0 ' in line]
preparation = []
for count in (0, 1, 8, 64):
    entry = {'candidates_per_preparation': count, 'preparations': 4096}
    assert len({r['checksum'] for r in setup_rows if int(r['count']) == count}) == 1
    for mode in (0, 1):
        values = [float(r['cpuMs']) for r in setup_rows if int(r['count']) == count and int(r['mode']) == mode]
        assert len(values) == 6
        entry['after_ms' if mode else 'before_ms'] = statistics.median(values)
    preparation.append(entry)
assert all(digest(Path(path)) == sha for path, sha in hashes.items())
report = {'pass': True, 'scope': __doc__, 'gpu_timing_measured': False, 'total_northlight_cost_measured': False,
          'inputs': hashes, 'exe_sha256': digest(exe), 'order': 'ABBA', 'warmup_blocks': 4,
          'summary': summary, 'preparation': preparation}
(out / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(summary, indent=2))
