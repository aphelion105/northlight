#!/usr/bin/env python3
# northlight-test: requires=zig,client,stormlib
"""Real SDK capture integration with stock native shaders and Cold Blood CPU-upload bytes; no device."""
from pathlib import Path
import os
import struct
import subprocess
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

if os.name != 'nt':
    print('SKIP: native Windows SDK integration runtime required')
    raise SystemExit(0)

fp.use_source_modules()
from world_scene_builder import Assets, array, string, unpack
from extract_signatures import variants
from build_equipment_emitters import lifetime_track


def native_fixture():
    assets = Assets(archives='stock')
    try:
        data = bytearray()
        for path, index in ((r'shaders\vertex\vs_3_0\CDiffuse_T1.bls', 0),
                            (r'shaders\vertex\vs_3_0\Color_T1.bls', 1),
                            (r'shaders\pixel\ps_3_0\Combiners_Mod.bls', 0)):
            code = list(variants(assets.read(path)))[index][2]
            data.extend(struct.pack('<I', len(code) // 4))
            data.extend(code)
        blob = assets.read(r'particles\snowflake.blp')
        assert blob[:4] == b'BLP2'
        encoding, depth, alpha, _ = unpack('<4B', blob, 8)
        assert encoding == 2
        width, height = unpack('<II', blob, 12)
        offsets, sizes = unpack('<16I', blob, 20), unpack('<16I', blob, 84)
        level = next(i for i in range(16) if offsets[i] and sizes[i] and max(width >> i, height >> i) <= 32)  # the cache keeps mips up to 32 texels
        width, height = max(1, width >> level), max(1, height >> level)
        block = 8 if depth in (0, 1) else 16
        d3d = 0x31545844 if block == 8 else 0x33545844 if alpha == 1 else 0x35545844
        raw = blob[offsets[level]:offsets[level] + sizes[level]]
        data.extend(struct.pack('<5I', width, height, d3d, ((width + 3) // 4) * block, len(raw)))
        data.extend(raw)
        model = assets.read(r'spells\ice_precast_med_hand.m2')
        assert model[:4] == b'MD20' and unpack('<I', model, 4)[0] == 264
        count, offset = unpack('<II', model, 80)
        textures = [string(model, name) for _, _, _, name in array(model, offset, count, '<4I')]
        count, offset = unpack('<II', model, 296)
        stride = 492 if unpack('<I', model, 16)[0] & 0x200 else 476
        source = next(offset + i * stride for i in range(count)
                      if textures[unpack('<H', model, offset + i * stride + 22)[0]].lower() == r'particles\snowflake.blp')
        assert model[source + 40] == 4  # Native SrcAlpha / One.
        colors = lifetime_track(model, source + 260, '<3f')
        alpha = lifetime_track(model, source + 276, '<h')
        assert colors[0][0] == alpha[0][0] == 0
        data.extend(struct.pack('<4f', *(v / 255 for v in colors[0][1]), alpha[0][1][0] / 32767))
        return data
    finally:
        assets.close()


with tempfile.TemporaryDirectory(prefix='northlight-particle-capture-') as temporary:
    root = Path(temporary)
    fixture = root / 'native.bin'
    fixture.write_bytes(native_fixture())
    for architecture in ('x86', 'x86_64'):
        for optimization in ('-O2', '-O0'):
            binary = root / (architecture + optimization + '.exe')
            subprocess.run([str(fp.zig()), 'c++', '-target', architecture + '-windows-gnu', optimization,
                            '-UNDEBUG', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-microsoft-exception-spec', *fp.test_include_flags(),
                            str(Path(__file__).with_suffix('.cpp')), '-o', str(binary)], env=fp.zig_env(), check=True)
            print(architecture, optimization, flush=True)
            subprocess.run([str(binary), str(fixture)], check=True)
