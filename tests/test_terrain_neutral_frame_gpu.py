# northlight-test: requires=zig,windows-gpu
"""Production material composite must preserve neutral detail across facets."""
from pathlib import Path
import argparse, os, subprocess, sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

parser = argparse.ArgumentParser(__doc__)
parser.add_argument('--counterexample', action='store_true')
parser.add_argument('--backend', type=Path)
parser.add_argument('--split', type=int, default=128)
parser.add_argument('--silhouette', type=int, default=256)
parser.add_argument('--source', type=Path, help='Existing actual-MPQ source.fgr from validate_auto_bump.py')
parser.add_argument('--distance', type=float, default=1)
args = parser.parse_args()
out = fp.output_dir() / 'terrain-neutral-frame-gpu'
out.mkdir(parents=True, exist_ok=True)
binary = out / 'terrain-neutral-frame.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O2',
                '-UNDEBUG', '-Wno-microsoft-exception-spec', '-std=c++17',
                *fp.test_include_flags(), str(Path(__file__).with_name('terrain_neutral_frame_gpu.cpp')),
                '-luser32', '-o', str(binary)], env=fp.zig_env(), check=True)
backend = args.backend or Path(os.environ['SystemRoot']) / 'SysWOW64/d3d9.dll'
command = [str(binary), str(backend.resolve()),
           '--counterexample' if args.counterexample else '--fixed',
           str(args.split), str(args.silhouette)]
if args.source:
    command.append(str(args.source.resolve()))
env = fp.zig_env()
env['NORTHLIGHT_TEST_TERRAIN_DISTANCE'] = str(args.distance)
run = subprocess.run(command,
                     cwd=out, env=env, capture_output=True, text=True, timeout=90)
(out / ('counterexample.log' if args.counterexample else 'fixed.log')).write_text(run.stdout + run.stderr, encoding='utf-8')
assert run.returncode == 0, run.stdout + run.stderr
assert 'TERRAIN_NEUTRAL_FRAME ' in run.stdout, run.stdout
print(run.stdout.strip())
