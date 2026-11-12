#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client
"""Two-pass bloom prototype: GPU energy, source coverage and boundary checks."""
import argparse
import hashlib
import json
import os
import shutil
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
from windows_gpu_guard import processes

parser = argparse.ArgumentParser(__doc__)
parser.add_argument('--output', type=Path, default=fp.output_dir()/'hdr-bloom-wide')
parser.add_argument('--compact-reference', type=Path, help='Optional archived pre-wide Frame header for exact default-off comparison.')
parser.add_argument('--allow-game-process', action='store_true', help='Explicit correctness-only authorization; never records timings.')
args = parser.parse_args()
if not args.allow_game_process:
    processes()
output = args.output.resolve()
assert not fp.is_client(output), 'Use a standalone output directory.'
output.mkdir(parents=True, exist_ok=True)
if args.compact_reference:
    shutil.copyfile(args.compact_reference, output/'hdr_frame_reference.h')
binaries = {}
for source in ('hdr_bloom_wide_gpu.cpp', 'hdr_wide_toggle_gpu.cpp', 'hdr_frame_gpu.cpp'):
    binary = output/(Path(source).stem+'.exe')
    flags = ['-DNORTHLIGHT_COMPACT_REFERENCE'] if args.compact_reference and source=='hdr_wide_toggle_gpu.cpp' else []
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG', *flags,
                    '-I'+str(output), *fp.include_flags(), str(fp.TESTS/source), '-luser32', '-o', str(binary)], env=fp.zig_env(), check=True)
    binaries[source] = binary
report = {'game_control': False, 'timings_measured': False, 'allow_game_process': args.allow_game_process,
          'source_sha256': hashlib.sha256((fp.SHADERS/'hdr_effects.hlsl').read_bytes()).hexdigest(), 'runs': [], 'compact_reference_sha256': hashlib.sha256(args.compact_reference.read_bytes()).hexdigest() if args.compact_reference else None,
          'header_sha256': hashlib.sha256(fp.src('hdr_frame.h').read_bytes()).hexdigest()}
for backend, name in [('dxvk', 'dxvk_d3d9.dll'), ('dxvk2', 'dxvk2_d3d9.dll')]:
    runtime = fp.client_root()/'renderer-backends'/backend/name
    for source, binary in binaries.items():
        result = subprocess.run([str(binary), str(runtime)], cwd=output, env=dict(os.environ, DXVK_LOG_PATH=str(output)), capture_output=True, text=True, timeout=120)
        report['runs'].append({'backend': backend, 'test': source, 'runtime_sha256': hashlib.sha256(runtime.read_bytes()).hexdigest(),
                               'exe_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
                               'exit_code': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr})
        (output/'validation.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
        print(backend, source, result.stdout.strip(), flush=True)
        assert result.returncode == 0, result.stderr
