#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Byte-exact subdivision regression and optional paired CPU benchmark."""
from pathlib import Path
import argparse,hashlib,json
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

p=argparse.ArgumentParser(__doc__)
p.add_argument('--reference',type=Path,help='Frozen production displacement_geometry.h for the paired oracle.')
p.add_argument('--fixture',type=Path,action='append',default=[],help='Local F2M1 mesh with native 48-byte M2 vertices.')
p.add_argument('--benchmark',action='store_true');p.add_argument('--output',type=Path)
a=p.parse_args();out = a.output or fp.output_dir() / 'displacement-subdivision'
out.mkdir(parents=True, exist_ok=True)
inputs=[fp.src('displacement_geometry.h'),fp.TESTS/'displacement_subdivision.cpp',Path(__file__),*a.fixture,*([a.reference] if a.reference else [])]
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest();hashes={str(p.resolve()):sha(p) for p in inputs};runs=[]
for mode in ('O2', 'O0'):
    exe = out / (mode + '.exe')
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-' + mode, '-UNDEBUG',
                    *(['-DNORTHLIGHT_DISPLACEMENT_REFERENCE="'+a.reference.resolve().as_posix()+'"'] if a.reference else []),
                    *fp.include_flags(), str(fp.TESTS / 'displacement_subdivision.cpp'), '-luser32', '-o', str(exe)], env=fp.zig_env(), check=True)
    result = subprocess.run([str(exe), *(['--benchmark'] if mode == 'O2' and a.benchmark else []),*map(str,a.fixture)], capture_output=True, text=True)
    (out / (mode + '.log')).write_text(result.stdout + result.stderr)
    assert result.returncode == 0, result.stdout + result.stderr
    print(mode, result.stdout, flush=True)
    runs.append(dict(mode=mode,stdout=result.stdout))
assert all(sha(p)==h for p,h in hashes.items()),'Source changed during the oracle'
(out/'validation.json').write_text(json.dumps(dict(pass_=True,inputs=hashes,runs=runs,benchmark=a.benchmark,
    scope='Byte-exact cold geometry construction; fixture population excluded from paired timings. Not a gameplay FPS measurement.'),indent=2)+'\n',encoding='utf-8')
