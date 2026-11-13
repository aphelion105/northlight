#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Compile production resource wrappers/decoder and compare against canonical bytes."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
import subprocess
import zlib
import build_renderer_assets as assets

out = fp.output_dir()
manifest = assets.prepare(out)
checks = ['#pragma once', '#include <windows.h>']
checks += ['#include "'+name+'"' for name in sorted({e['header'] for e in manifest['entries']})]
checks.append('static void checkArrays(const unsigned char* raw){')
for entry in manifest['entries']:
    namespace = {'weather_texture_data.h': 'NorthlightWeatherArt::',
                 'overlay_font_roboto.h': 'NorthlightOverlayFont::'}.get(entry['header'], '')
    name = namespace+entry['name']
    checks += [f'static_assert(sizeof({name})=={entry["bytes"]});',
               f'assert(std::memcmp({name},raw+{entry["offset"]},{entry["bytes"]})==0);']
checks.append('}')
(out/'resource_array_checks.h').write_text('\n'.join(checks)+'\n')
exe = out/'assets.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                '-I', str(out), *fp.include_flags(), str(fp.TESTS/'renderer_assets_decode.cpp'),
                str(fp.src('renderer_assets.cpp')), '-o', str(exe)], env=fp.zig_env(), check=True)
subprocess.run([str(exe)], cwd=out, check=True)
assert (out/'decoded.bin').read_bytes() == zlib.decompress((out/assets.FILENAME).read_bytes()[8:])
print('PASS decoded resource bytes match Python zlib and original generated headers')
