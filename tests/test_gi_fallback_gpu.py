# northlight-test: requires=windows-gpu,zig
"""GPU regression for GI outside resident probes; optional alternating benchmark."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

p = argparse.ArgumentParser()
p.add_argument('--baseline', type=Path)
args = p.parse_args()
out = fp.output_dir()
out.mkdir(parents=True, exist_ok=True)
env = fp.zig_env()
for key, name in [('TEMP', 'tmp'), ('TMP', 'tmp'), ('ZIG_LOCAL_CACHE_DIR', 'zig-local'), ('ZIG_GLOBAL_CACHE_DIR', 'zig-global')]:
    (out/name).mkdir(exist_ok=True)
    env[key] = str(out/name)
env.update(DISABLE_VULKAN_OBS_CAPTURE='1', DXVK_LOG_PATH=str(out))
backend = os.environ.get('NORTHLIGHT_TEST_D3D9', str(Path(os.environ['WINDIR']) / 'SysWOW64/d3d9.dll'))
exe = out / 'gi-fallback.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                '-Wno-return-type', *fp.include_flags(), str(fp.TESTS / 'gi_fallback_gpu.cpp'),
                '-luser32', '-o', str(exe)], env=env, check=True)
subprocess.run([str(exe), str(fp.COMPILED), backend,
                *([str(args.baseline.resolve())] if args.baseline else [])], cwd=out, env=env, check=True, timeout=120)
