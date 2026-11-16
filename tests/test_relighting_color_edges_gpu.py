# northlight-test: requires=windows-gpu,zig
"""Production composition preserves neutral-light hue and material silhouettes."""
from pathlib import Path
import argparse
import os
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
import windows_gpu_guard

p = argparse.ArgumentParser(__doc__)
p.add_argument('--shaders', type=Path, default=fp.COMPILED)
p.add_argument('--backend', type=Path, action='append')
a = p.parse_args()
windows_gpu_guard.processes(allow_game=True)  # Image correctness only.
out = fp.output_dir() / 'relighting-color-edges'
out.mkdir(parents=True, exist_ok=True)
exe = out / 'relighting.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17',
                '-O2', '-UNDEBUG', *fp.test_include_flags(),
                str(fp.TESTS / 'relighting_color_edges_gpu.cpp'), '-luser32', '-o', str(exe)],
               env=fp.zig_env(), check=True)
backends = a.backend or [Path(os.environ['WINDIR']) / 'SysWOW64/d3d9.dll']
for index, backend in enumerate(backends):
    run = subprocess.run([str(exe), str(backend.resolve()), str(a.shaders.resolve())],
                         cwd=out, env=fp.zig_env(), capture_output=True, text=True, timeout=90)
    (out / f'backend-{index}.log').write_text(run.stdout + run.stderr)
    print(run.stdout, flush=True)
    run.check_returncode()
