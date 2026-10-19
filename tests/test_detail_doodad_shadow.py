#!/usr/bin/env python3
# northlight-test: requires=zig
"""Bounded native grass shadow replay; optionally audits the tester's stock BLS."""
import ast
import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
import client_fixtures

HERE = Path(__file__).resolve().parent
stub = next(ast.literal_eval(n.value) for n in ast.parse((HERE / 'test_terrain_snapshot.py').read_text()).body
            if isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'stub' for t in n.targets))
with tempfile.TemporaryDirectory(prefix='northlight-detail-shadow-') as tmp:
    out = Path(tmp)
    (out / 'd3d9.h').write_text(stub)
    fixtures = []
    if client_fixtures.shaders_available():
        fp.use_source_modules()
        from world_scene_builder import Assets
        from extract_signatures import variants
        assets = Assets(archives='stock')
        try:
            for major in (2, 3):
                name = f'shaders\\vertex\\vs_{major}_0\\DetailDoodad.bls'
                codes = list(variants(assets.read(name)))
                assert len(codes) == 6
                for variant, _, code in codes:
                    fixture = out / f'detail-{major}-{variant}.bin'
                    fixture.write_bytes(code)
                    fixtures.append(str(fixture))
        finally:
            assets.close()
    else:
        print('SKIP native BLS subcase: set NORTHLIGHT_CLIENT and NORTHLIGHT_STORMLIB')
    for flags in (['-O2'], ['-O0', '-g']):
        target = out / ('grass-shadow-test.exe' if os.name == 'nt' else 'grass-shadow-test')
        subprocess.run([str(fp.zig()), 'c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', *flags, '-UNDEBUG',
                        '-I', str(out), *fp.test_include_flags(), str(HERE / 'test_detail_doodad_shadow.cpp'),
                        '-o', str(target)], env=fp.zig_env(), check=True)
        subprocess.run([str(target), *fixtures], check=True)
