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
import build_renderer_assets as assets

out = fp.output_dir()
exe = out/'host.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                str(fp.TESTS/'renderer_assets_dll.cpp'), '-o', str(exe)], env=fp.zig_env(), check=True)
bundle = assets.built_path(fp.dll()).read_bytes()
assets.verify_pair(fp.dll().read_bytes(), bundle)
for name, content in [('valid', bundle), ('missing', None), ('damaged', bundle[:-1]+bytes([bundle[-1]^1]))]:
    folder = out/name
    folder.mkdir(exist_ok=True)
    shutil.copy2(exe, folder/'host.exe')
    shutil.copy2(fp.dll(), folder/'d3d9.dll')
    (folder/'northlight-renderer.ini').write_text('[Renderer]\nBackend=native\n')
    if content is not None:
        (folder/assets.FILENAME).write_bytes(content)
    subprocess.run([str(folder/'host.exe')], cwd=folder, check=True, timeout=60)
    log = (folder/'northlight-renderer.log').read_text(errors='replace')
    line = next(line for line in log.splitlines() if 'RESOURCES NORTHLIGHT_ASSETS_V1:' in line)
    assert ('loaded=1' in line) == (name == 'valid'), line
    if name != 'valid':
        assert 'native rendering retained' in log
    print('PASS DLL resource startup:', name)
