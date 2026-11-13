#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig
"""GPU regression for rain/snow around foreground mushroom and roof silhouettes."""
from pathlib import Path
import argparse
import os
import subprocess
import sys
import struct
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

parser = argparse.ArgumentParser(__doc__)
parser.add_argument('--backend', type=Path, help='System D3D9 by default; optional plain x86 DXVK DLL')
parser.add_argument('--external-assets', action='store_true', help='Exercise production compressed resource loading')
parser.add_argument('--allow-running-game', action='store_true',
                    default=os.environ.get('NORTHLIGHT_TEST_ALLOW_RUNNING_GAME') == '1',
                    help='Independent image correctness only; no timing or game FPS claim')
args = parser.parse_args()
if os.name != 'nt':
    raise SystemExit('This GPU regression requires Windows')
backend = args.backend or Path(os.environ['WINDIR']) / 'SysWOW64' / 'd3d9.dll'
out = fp.output_dir()
exe = out / 'weather-occlusion.exe'
asset_flags, asset_sources = [], []
if args.external_assets:
    sys.path.insert(0, str(fp.SCRIPTS))
    import build_renderer_assets
    asset_dir = out/'assets'
    manifest = build_renderer_assets.prepare(asset_dir)
    raw = zlib.decompress((asset_dir/build_renderer_assets.FILENAME).read_bytes()[8:])
    entries = [e for e in manifest['entries'] if e['type'] != 'unsigned char']
    checks = ['#pragma once'] + ['#include "'+name+'"' for name in sorted({e['header'] for e in entries})]
    checks.append('static void checkResourceShaders(IDirect3DDevice9* device){')
    for entry in entries:
        version = struct.unpack_from('<I', raw, entry['offset'])[0] >> 16
        assert version in (0xfffe, 0xffff)
        kind = 'Vertex' if version == 0xfffe else 'Pixel'
        checks.append('{IDirect3D'+kind+'Shader9* shader=nullptr;'
                      'assert(SUCCEEDED(device->Create'+kind+'Shader(reinterpret_cast<const DWORD*>('
                      +entry['name']+'),&shader)));assert(shader);shader->Release();}')
    checks.append('std::puts("PASS all '+str(len(entries))+' external shader arrays accepted by D3D9");}')
    (asset_dir/'resource_shader_checks.h').write_text('\n'.join(checks)+'\n')
    asset_flags = ['-I', str(asset_dir), '-DNORTHLIGHT_EXTERNAL_ASSETS=1']
    asset_sources = [str(fp.src('renderer_assets.cpp'))]
world = fp.src('world_renderer.h').read_text(encoding='utf-8')
start = world.index('struct NameMaskCompositeBind {')
end = world.index('\n};', start) + 3
(out / 'lens_name_mask.h').write_text('#include "name_stencil.h"\n' + world[start:end] + '\n', encoding='utf-8')
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                *asset_flags, '-I', str(out), *fp.test_include_flags(), str(fp.TESTS / 'weather_occlusion_gpu.cpp'),
                *asset_sources, '-luser32', '-o', str(exe)],
               env=fp.zig_env(), check=True)
command = [str(exe), str(backend.resolve())]
if args.allow_running_game:
    command.append('--allow-running-game')
    print('Concurrent game allowed explicitly: image/state correctness only, no performance claim.', flush=True)
subprocess.run(command, cwd=out, check=True, timeout=180)
