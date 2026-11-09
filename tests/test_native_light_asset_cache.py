#!/usr/bin/env python3
# northlight-test: requires=zig,windows-exe timing
"""Native-light cache budget invariants; optional exact reference and ABBA control."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

parser=argparse.ArgumentParser(__doc__)
parser.add_argument('--before',type=Path)
args=parser.parse_args()
out=fp.output_dir();out.mkdir(parents=True,exist_ok=True)
source=fp.src('native_scene_lights.h');digest=hashlib.sha256(source.read_bytes()).hexdigest()
variants={'current':[]}
if args.before:
    reference=out/'reference';reference.mkdir()
    (reference/source.name).write_bytes(args.before.read_bytes())
    variants['reference']=['-I'+str(reference),'-DNORTHLIGHT_NATIVE_CACHE_REFERENCE']
for label,flags in variants.items():
    subprocess.run(list(map(str,[fp.zig(),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG',
        *flags,*fp.test_include_flags(),fp.TESTS/'native_light_asset_cache.cpp','-o',out/(label+'.exe')])),env=fp.zig_env(),check=True)
rows=[]
for mode in (0,1):
    order=['current','reference','reference','current'] if args.before else ['current']
    images={}
    for index,label in enumerate(order):
        image=out/f'{mode}-{index}-{label}.bin'
        result=subprocess.run([str(out/(label+'.exe')),str(image),str(mode)],capture_output=True,text=True,check=True)
        (out/f'{mode}-{index}-{label}.log').write_text(result.stdout+result.stderr,encoding='utf-8')
        print(result.stdout,end='',flush=True)
        import re
        ms=float(re.search(r'warmMs=([0-9.]+)',result.stdout)[1])
        images[label]=hashlib.sha256(image.read_bytes()).hexdigest();rows.append(dict(mode=mode,label=label,ms=ms,sha256=images[label]))
    assert len(set(images.values()))==1,'Native light values changed against reference'
assert hashlib.sha256(source.read_bytes()).hexdigest()==digest
(out/'validation.json').write_text(json.dumps(dict(pass_=True,source_sha256=digest,reference=bool(args.before),rows=rows,
    scope='CPU only, 384 synthetic models/96 assets, exact native-light values, current pose/header/reset/budget ledger. Timing includes fixture reads and invariant checks; no game FPS claim.'),indent=2)+'\n',encoding='utf-8')
