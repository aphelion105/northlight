"""Build the runtime geometry/shadow upload plan without launching or changing the game."""
from pathlib import Path
import argparse
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cache', type=Path, required=True)
parser.add_argument('--map', required=True)
parser.add_argument('--center', type=float, nargs=3, required=True)
args = parser.parse_args()
out = fp.output_dir()
out.mkdir(parents=True, exist_ok=True)
exe = out/'probe.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17',
                '-O2', *fp.test_include_flags(), str(fp.REPO/'scripts/probe_world_geometry.cpp'),
                str(fp.src('world_gi.cpp')), str(fp.src('world_mesh_plan.cpp')),
                '-o', str(exe)], env=fp.zig_env(), check=True)
subprocess.run([str(exe), str(args.cache.resolve()), args.map, *map(str, args.center)], check=True)
