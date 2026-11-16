# northlight-test: requires=windows-gpu,zig,client,stormlib
"""Real WMO POM/displacement with the receiver stencil tags used in captures."""
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
from extract_signatures import variants
from world_scene_builder import Assets, decode_blp
from windows_gpu_guard import processes
sys.path.insert(0, str(fp.SCRIPTS / 'shaders'))
from shader_compiler import Compiler

out = fp.output_dir() / 'receiver-stencil'
out.mkdir(parents=True, exist_ok=True)
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
watched = [p for folder in (fp.SRC, fp.SHADERS) for p in folder.rglob('*') if p.is_file()]
watched += [Path(__file__), fp.TESTS / 'auto_normal_oblique_gpu.cpp']
inputs = {str(p): sha(p) for p in watched}
assets = Assets()
try:
    names = sorted(n for n in assets.providers if n.endswith('.blp') and 'dalaran' in n and 'floor' in n and not n.endswith('_s.blp'))
    assert names
    material = names[0]
    data = assets.read(material)
    programs = []
    for family, index in [('mapobjdiffuse_t1', 0), ('mapobjudiffuse_t1', 39), ('mapobjuspecular_t1', 69)]:
        programs.append((family, index, dict((i, c) for i, _, c in variants(assets.read(f'shaders\\vertex\\vs_3_0\\{family}.bls')))[index]))
    pixel = dict((i, c) for i, _, c in variants(assets.read('shaders\\pixel\\ps_3_0\\mapobjdiffuse.bls')))[0]
finally:
    assets.close()
source = out / 'source'
source.mkdir(exist_ok=True)
for level in range(10):
    w, h, rgba = decode_blp(data, max(1, 256 >> level))
    bgra = bytearray(rgba)
    bgra[0::4], bgra[2::4] = rgba[2::4], rgba[0::4]
    payload = struct.pack('<5I', 0x31524746, w, h, 21, 4) + bgra
    (source / f'source.fgr.{level}').write_bytes(payload)
    if not level:
        (source / 'source.fgr').write_bytes(payload)
    if w == h == 1:
        break
Compiler(allow_compatible_binary=True).compile(fp.SHADERS / 'auto_normal_preview.hlsl', 'PreviewBase', 'ps_3_0', source / 'PreviewBase.bin')
exe = out / 'receiver.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                '-Wno-microsoft-exception-spec', '-DNORTHLIGHT_OBLIQUE_WIDTH=320', '-DNORTHLIGHT_OBLIQUE_HEIGHT=200',
                *fp.test_include_flags(), str(fp.TESTS / 'auto_normal_oblique_gpu.cpp'), '-luser32', '-o', str(exe)], env=fp.zig_env(), check=True)
backend = Path(os.environ.get('NORTHLIGHT_TEST_D3D9', fp.client_root() / 'renderer-backends/dxvk2/dxvk2_d3d9.dll'))
backend_hash = sha(backend)
records = []
products = ['normal-on', 'pom-on', 'displacement-on', 'displacement-indexed', 'displacement-zero', 'geometry-off-depth', 'geometry-on-depth']
def image(p):
    b = p.read_bytes()
    magic, w, h, fmt, stride = struct.unpack_from('<5I', b)
    assert magic == 0x31524746 and stride in (8, 16)
    return np.frombuffer(b, dtype='<f2' if stride == 8 else '<f4', offset=20).astype(np.float32).reshape(h, w, 4)
for family, index, vertex in programs:
    for samples in (0, 8):
        reference = None
        for marked in (False, True):
            folder = out / f'{family}-{index}-{samples}-{int(marked)}'
            folder.mkdir(exist_ok=True)
            for file in source.iterdir():
                (folder / file.name).write_bytes(file.read_bytes())
            (folder / 'terrain-vs.bin').write_bytes(vertex)
            (folder / 'terrain-ps.bin').write_bytes(pixel)
            env = fp.zig_env()
            env.update(NORTHLIGHT_TEST_MSAA=str(samples), NORTHLIGHT_TEST_WMO_DECLARATION="1", DXVK_LOG_PATH=str(folder),
                       DXVK_CONFIG="d3d9.customVendorId = 1002; d3d9.cachedDynamicBuffers = True; d3d9.cachedWriteOnlyBuffers = True")
            if marked:
                env['NORTHLIGHT_TEST_RECEIVER_STENCIL'] = '1'
            processes()
            run = subprocess.run([str(exe), str(backend), str(folder) + '/', '12', '.7', 'wall'], env=env, cwd=folder, capture_output=True, text=True, timeout=90)
            (folder / 'gpu.log').write_text(run.stdout + run.stderr)
            assert run.returncode == 0, run.stdout[-3000:] + run.stderr
            assert 'MATERIAL_MSAA_PASS' in run.stdout and 'DISPLACEMENT_PREPARED' in run.stdout
            images = {n: image(folder / (n + '.fgr')) for n in products}
            assert all(np.isfinite(v).all() for v in images.values())
            delta = lambda a, b: float(np.max(np.abs(images[a] - images[b])))
            assert delta('normal-on', 'pom-on') > .001, 'POM must change actual pixels'
            assert delta('geometry-off-depth', 'geometry-on-depth') > .001, 'Displacement must change actual depth'
            assert delta('displacement-on', 'displacement-indexed') < .0002
            # Mode 3 retains its POM fallback when geometric strength is zero.
            assert delta('pom-on', 'displacement-zero') < .0002
            if reference is not None:
                assert all(np.array_equal(v, reference[n]) for n, v in images.items()), 'Receiver marking changed relief'
            else:
                reference = images
            records.append(dict(program=family, variant=index, msaa=samples, stencil=marked,
                                pom_delta=delta('normal-on', 'pom-on'), depth_delta=delta('geometry-off-depth', 'geometry-on-depth')))
            print('RECEIVER_RELIEF_PASS', records[-1], flush=True)
assert all(sha(Path(p)) == digest for p, digest in inputs.items()) and sha(backend) == backend_hash
(out / 'validation.json').write_text(json.dumps(dict(passed=True, inputs=inputs, material=material,
    material_sha256=hashlib.sha256(data).hexdigest(), backend=str(backend), backend_sha256=backend_hash,
    cases=records, scope='Offscreen image/depth correctness; no gameplay FPS measurement'), indent=2) + '\n')
