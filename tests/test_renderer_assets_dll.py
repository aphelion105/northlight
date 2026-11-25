#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig,dll
"""Load the production DLL in isolated hosts: valid resources and native fallback on failure."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
import shutil
import subprocess
import argparse
import build_renderer_assets as assets

out = fp.output_dir()
parser = argparse.ArgumentParser()
parser.add_argument('--x64', action='store_true')
parser.add_argument('--x86-on12', action='store_true', help='Use the 32-bit D3D9On12 comparison build')
parser.add_argument('--dxvk', type=Path, help='AMD64 DXVK d3d9.dll for the x64 device test')
parser.add_argument('--hdr', action='store_true', help='Verify direct scRGB output on the current Windows HDR display')
parser.add_argument('--gpu-transfer', action='store_true', help='Require native D3D9On12 HDR presentation without CPU readback')
parser.add_argument('--dxvk-engine-workaround', action='store_true', help='Require DXVK 3 instance-name workaround')
args = parser.parse_args()
x64 = args.x64
if x64 and args.x86_on12:
    parser.error('Choose one standalone build')
if args.dxvk and not x64:
    parser.error('--dxvk requires --x64')
if args.hdr and not (x64 or args.x86_on12):
    parser.error('--hdr requires a standalone build')
if args.gpu_transfer and (not args.hdr or args.dxvk):
    parser.error('--gpu-transfer requires native --hdr')
if args.dxvk_engine_workaround and not args.dxvk:
    parser.error('--dxvk-engine-workaround requires --dxvk')
standalone = x64 or args.x86_on12
dll = fp.out()/('x64-proxy' if x64 else 'x86-on12-proxy')/'d3d9.dll' if standalone else fp.dll()
target = 'x86_64-windows-gnu' if x64 else 'x86-windows-gnu'
exe = out/'host.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', target, '-std=c++17', '-O2', '-UNDEBUG',
                *(['-DNORTHLIGHT_ASSET_TEST_DEVICE=1'] if args.x86_on12 else []),
                str(fp.TESTS/'renderer_assets_dll.cpp'), '-luser32', '-o', str(exe)], env=fp.zig_env(), check=True)
bundle = (dll.with_name(assets.FILENAME) if standalone else assets.built_path(dll)).read_bytes()
assets.verify_pair(dll.read_bytes(), bundle)
for name, content in [('valid', bundle), ('missing', None), ('damaged', bundle[:-1]+bytes([bundle[-1]^1]))]:
    folder = out/name
    folder.mkdir(exist_ok=True)
    shutil.copy2(exe, folder/'host.exe')
    shutil.copy2(dll, folder/'d3d9.dll')
    config = '[Renderer]\n' if x64 else '[Renderer]\nBackend=native\n'
    if args.dxvk:
        shutil.copy2(args.dxvk, folder/'dxvk_d3d9.dll')
        config += 'Backend=dxvk\nBackendPath=dxvk_d3d9.dll\n'
    if args.dxvk_engine_workaround:
        config += 'DxvkEngineNameWorkaround=1\n'
    if args.dxvk:
        (folder/'northlight-quality.ini').write_text('[Quality]\nCommandStream=1\nDiagnostics=1\n')
    if args.hdr:
        config += 'ExperimentalHDR=1\nHDRPipelineVersion=2\n'
        (folder/'dxvk.conf').write_text('dxvk.allowFse = False\n')
    (folder/'northlight-renderer.ini').write_text(config)
    if content is not None:
        (folder/assets.FILENAME).write_bytes(content)
    else:
        (folder/assets.FILENAME).unlink(missing_ok=True)
    subprocess.run([str(folder/'host.exe')], cwd=folder, check=True, timeout=60)
    log = (folder/'northlight-renderer.log').read_text(errors='replace')
    line = next(line for line in log.splitlines() if 'RESOURCES NORTHLIGHT_ASSETS_V1:' in line)
    assert ('loaded=1' in line) == (name == 'valid'), line
    if x64:
        assert 'NORTHLIGHT X64 D3D9: live 12340 memory and hooks disabled' in log
        assert ('rules=dxvk' if args.dxvk else 'rules=native') in log
        assert ('CreateDevice HRESULT=0x00000000' in log) == (name == 'valid')
        if args.dxvk:
            assert 'X64 D3D9 passthrough' not in log
            assert ('CSTREAM active gameTid=' in log) == (name == 'valid'), log
            if name == 'valid':
                assert 'CSTREAM RESET end' in log and 'ran=1 result=0x00000000' in log, log
    if args.hdr and name == 'valid':
        assert log.count(f'HDR experimental requested=1 active=1 stream={int(bool(args.dxvk))}') >= 2, log
        if args.dxvk:
            dxvk_log = (folder/'host_d3d9.log').read_text(errors='replace')
            assert 'VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT' in dxvk_log, dxvk_log
        else:
            assert 'lifecycle=native-dxgi' in log
            assert log.count('HDR native DXGI Present') == 2, log
            assert log.count(('X64' if x64 else 'X86') + ' native depth INTZ=1') == 2, log
            if args.gpu_transfer:
                assert log.count('color=scRGB transfer=D3D9On12 GPU') == 2, log
    if args.dxvk_engine_workaround and name == 'valid':
        assert 'active-module-pinned engine-name=1' in log, log
        assert 'DXVK3 engine-name instance original=DXVK replacement=Northlight result=0' in log, log
    if name != 'valid':
        assert 'native rendering retained' in log
    print('PASS DLL resource startup:', name)
