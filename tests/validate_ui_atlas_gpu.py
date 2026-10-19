#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# northlight-test: requires=zig,client
"""Manual Windows DXVK atlas upload and diagnostic render validation; no game launch.

Pass --proxy /path/to/copied/d3d9.dll to exercise that DLL's own stream instead.
The copied proxy's backend/config must already be next to the helper executable.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--proxy', type=Path)
parser.add_argument('--out', type=Path, default=fp.REPO/'out/ui-atlas-gpu')
parser.add_argument('--opt', choices=('O0', 'O2'), default='O2')
args = parser.parse_args()
if sys.platform != 'win32':
    raise SystemExit('This manual backend probe needs Windows/Vulkan; no game is launched.')
folder = args.out.resolve()
folder.mkdir(parents=True, exist_ok=True)
exe = folder/'ui-atlas-gpu.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-'+args.opt,
                '-Wno-microsoft-exception-spec', '-UNDEBUG', *fp.test_include_flags(),
                str(fp.TESTS/'validate_ui_atlas_gpu.cpp'), '-luser32', '-o', str(exe)],
               check=True, env=fp.zig_env())
backend = args.proxy.resolve() if args.proxy else fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll'
config = folder/'dxvk-test.conf'
config.write_text('dxvk.enableAsync = False\n')
env = dict(os.environ, DXVK_LOG_PATH=str(folder), DXVK_LOG_LEVEL='info', DXVK_CONFIG_FILE=str(config))
run = subprocess.run([str(exe), str(backend), str(folder/'atlas'), *(['single'] if args.proxy else [])],
                     cwd=folder, env=env, capture_output=True, text=True, timeout=120)
report = {'returncode': run.returncode, 'stdout': run.stdout, 'stderr': run.stderr,
          'backend': str(backend), 'backend_sha256': hashlib.sha256(backend.read_bytes()).hexdigest()}
(folder/('result-'+args.opt+'.json')).write_text(json.dumps(report, indent=2), encoding='utf-8')
print(run.stdout + run.stderr)
run.check_returncode()
