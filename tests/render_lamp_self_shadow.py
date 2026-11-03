#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client,stormlib,world-cache manual
"""QA capture reconstruction using production point cubes and paired composites.

No game is launched. Missing animated casters/stencil are not reconstructed.
The shader delta is applied to the captured composite input to retain its base.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import numpy as np
from PIL import Image
sys.path[:0]=[str(Path(__file__).resolve().parents[1]),str(Path(__file__).resolve().parents[1]/'renderer')]
import northlight_paths as fp
from world_scene_builder import Assets,InstancedBuilder
from test_m2_lamp_self_shadow_gpu import compile_gpu


def load(path):
    b=path.read_bytes();magic,w,h,fmt,stride=struct.unpack_from('<5I',b);assert magic==0x31524746
    if fmt==113:return np.frombuffer(b,'<f2',offset=20).reshape(h,w,4).astype('f4')
    assert fmt in (21,22)
    return np.frombuffer(b,'u1',offset=20).reshape(h,w,4)[...,[2,1,0,3]].astype('f4')/255


def save(path,a,fmt=113):
    h,w,_=a.shape
    b=a.astype('<f2').tobytes() if fmt==113 else np.rint(np.clip(a,0,1)*255).astype('u1')[...,[2,1,0,3]].tobytes()
    path.write_bytes(struct.pack('<5I',0x31524746,w,h,fmt,8 if fmt==113 else 4)+b)


def main():
    p=argparse.ArgumentParser(__doc__);p.add_argument('--capture',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--reuse-exe',type=Path)
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);prefix=str(a.capture.resolve()/'capture-1-');cache=out/'cache';source=fp.world_cache()
    records=[json.loads(l) for l in Path(prefix+'lighting-inputs.jsonl').read_text().splitlines()]
    c=np.fromfile(prefix+'lighting-pass-1-constants.f32','<f4').reshape(-1,4);center=c[6,:3]
    selected=set(map(int,re.findall(r'^POINT source=(\d+)',(a.capture.parent/'northlight-renderer.log').read_text(),re.M)[-16:]))
    captured={int(r['source_id']) for r in records if r['event']=='source' and r['kind']==2};selected&=captured
    for folder in ('Azeroth','models'):(cache/folder).mkdir(parents=True,exist_ok=True)
    tx=math.floor((17066.66666666667-float(center[1]))/533.33333333333);ty=math.floor((17066.66666666667-float(center[0]))/533.33333333333)
    keys=set();owners={}
    for y in range(ty-1,ty+2):
        for x in range(tx-1,tx+2):
            tile=source/'Azeroth'/f'{x}_{y}.fg3'
            if not tile.exists():continue
            shutil.copy2(tile,cache/'Azeroth'/tile.name);b=tile.read_bytes()
            for r in struct.iter_unpack('<32sQI18f',b[12:]):
                keys.add(r[0].hex())
                if r[2] not in (1,3):continue
                sid=int.from_bytes(hashlib.sha256(r[0]+struct.pack('<QII',r[1],r[2],0)).digest()[:8],'little')
                if sid in selected:owners[sid]=r[0].hex()
    assert len(owners)==len(selected)>0
    assets=Assets();builder=InstancedBuilder(assets,cache);names={hashlib.sha256(('FGS3-v1:'+n).encode()).hexdigest():n for n in assets.providers if n.endswith('.m2')}
    for key in set(owners.values()):
        name=names[key];mesh=builder.m2(name);assert any(len(m)==4 for m in mesh[2]);builder.add_mesh(mesh,((1,0,0),(0,1,0),(0,0,1)),(0,0,0),'m2',0)
    assets.close()
    for key in keys-set(owners.values()):
        path=cache/'models'/(key+'.fgs')
        if not path.exists():shutil.copy2(source/'models'/path.name,path)
    lights=[r for r in struct.iter_unpack('<8fQII',(source/'lights/Azeroth.fgl').read_bytes()[16:]) if r[8] in selected]
    assert len(lights)==len(selected)
    (cache/'sources.fgl').write_bytes(struct.pack('<4sIII',b'FGL1',1,48,len(lights))+b''.join(struct.pack('<8fQII',*r) for r in lights))
    exe=a.reuse_exe.resolve() if a.reuse_exe else compile_gpu(out);backend=fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll'
    settings=out/'settings';settings.mkdir(exist_ok=True);env={**fp.zig_env(),'DXVK_LOG_PATH':str(out),'DISABLE_VULKAN_OBS_CAPTURE':'1'}
    cmd=[exe,backend,str(settings)+os.sep,cache,prefix,out]
    with (out/'gpu.log').open('w') as log:subprocess.run(list(map(str,cmd)),check=True,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=300,cwd=out)
    before,after=(load(out/('correction-'+s+'.fgr')) for s in ('before','after'))
    delta=after-before;original=load(Path(prefix+'current-local-light.fgr'));fixed=original.copy();fixed[...,:3]=np.maximum(0,original[...,:3]+delta[...,:3]);save(out/'current-local-fixed.fgr',fixed)
    composite=out/'composite.exe';cmd=[fp.zig(),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG',*fp.test_include_flags(),fp.TESTS/'lamp_capture_composite_gpu.cpp','-luser32','-o',composite]
    subprocess.run(list(map(str,cmd)),check=True,env=env,timeout=180)
    subprocess.run(list(map(str,[composite,backend,prefix,out,'world'])),check=True,env=env,timeout=120,cwd=out)
    worldBefore,worldAfter=(load(out/('world-'+s+'.fgr')) for s in ('before','after'))
    materialInput=load(Path(prefix+'lighting-input-g3-s0.fgr'));save(out/'material-input-fixed.fgr',materialInput+worldAfter-worldBefore,22)
    subprocess.run(list(map(str,[composite,backend,prefix,out,'material'])),check=True,env=env,timeout=120,cwd=out)
    materialBefore,materialAfter=(load(out/('material-'+s+'.fgr')) for s in ('before','after'))
    final=load(Path(prefix+'lighting-final-scene.fgr'));result=np.clip(final+materialAfter-materialBefore,0,1)
    for name,pixels in [('before',final),('after',result)]:Image.fromarray(np.rint(pixels[...,:3]*255).astype('u1')).save(out/(name+'.png'))
    changed=np.max(np.abs(result[...,:3]-final[...,:3]),axis=2)
    report={'sources':len(lights),'source_ids':sorted(selected),'game_launched':False,'whole_scene_replay':False,'animated_casters_replayed':False,'stencil_replayed':False,'hard_pcf':True,
            'correction_delta_mean':delta[...,:3].mean(axis=(0,1)).tolist(),'changed_pixels_over_1_255':int((changed>1/255).sum()),'composite_baseline_mean_absolute_error':float(np.abs(materialBefore[...,:3]-final[...,:3]).mean()),
            'method':'Production cache/cubes/LocalLighting; paired WorldComposite delta on recorded MaterialComposite input; captured MaterialComposite shader; paired final delta preserves captured base.'}
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))

if __name__=='__main__':main()
