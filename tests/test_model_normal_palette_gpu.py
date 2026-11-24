# northlight-test: requires=windows-gpu,zig,client
"""Check the proved palette shader, ordered replay, allocation failure and Reset."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
from windows_gpu_guard import processes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--backend', type=Path)
    args = parser.parse_args()
    backend = args.backend or fp.client_root() / 'renderer-backends/dxvk/dxvk_d3d9.dll'
    output = fp.output_dir() / 'model-normal-palette'
    output.mkdir(parents=True, exist_ok=True)
    exe = output / 'model-normal-palette.exe'
    env = {**fp.zig_env(), 'DXVK_LOG_PATH': str(output), 'DISABLE_VULKAN_OBS_CAPTURE': '1'}
    command = [str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O2', '-std=c++17',
               '-UNDEBUG', '-Wno-microsoft-exception-spec', *fp.test_include_flags(),
               str(fp.TESTS / 'model_normal_palette_gpu.cpp'), str(fp.src('world_gi.cpp')),
               '-luser32', '-luuid', '-o', str(exe)]
    with (output / 'compile.log').open('w', encoding='utf-8') as log:
        subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=240)
    processes()
    result = subprocess.run([str(exe), str(backend.resolve())], env=env, cwd=output,
                            capture_output=True, timeout=120)
    (output / 'gpu.log').write_bytes(result.stdout + result.stderr)
    print((result.stdout + result.stderr).decode(errors='replace'))
    result.check_returncode()
    assert b'MODEL_NORMAL_PALETTE_PASS' in result.stdout
    assert b'MODEL_NORMAL_INSTANCE_ROWS_PASS' in result.stdout
    assert b'NATIVE_INDEXED_PAIR_PASS' in result.stdout
    assert b'GAME_INDEXED_RANGES_PASS' in result.stdout
    assert b'ORDERED_PARTICLE_PAIR_PASS' in result.stdout
    assert b'PARTICLE_TEXTURE_PAIR_PASS' in result.stdout
    assert b'PARTICLE_PAIR_CONSTANTS_PASS' in result.stdout


if __name__ == '__main__':
    main()
