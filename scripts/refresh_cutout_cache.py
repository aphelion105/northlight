"""Refresh existing foliage model materials from selected ADTs; preserve geometry."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import sys

sys.path[:0] = [str(Path(__file__).resolve().parents[1]), str(Path(__file__).resolve().parents[1]/'renderer')]
from world_scene_builder import Assets, Builder, chunks, array, string

p = argparse.ArgumentParser(__doc__)
p.add_argument('--map', required=True)
p.add_argument('--tile', nargs=2, type=int, action='append', required=True)
p.add_argument('--cache', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--apply', action='store_true')
a = p.parse_args()
assert a.map in ('Azeroth','Kalimdor','Expansion01','Northrend')
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=True)
backup = out/'original-models'
backup.mkdir(exist_ok=True)
records = []
assets = Assets(maps=(a.map,))
try:
    names = set()
    for x,y in a.tile:
        root = dict(chunks(assets.read(f'world\\maps\\{a.map}\\{a.map}_{x}_{y}.adt')))
        paths = [string(root.get('MMDX',b''),o[0]) for o in array(root.get('MMID',b''),0,len(root.get('MMID',b''))//4,'<I')]
        names.update(paths[e[0]] for e in array(root.get('MDDF',b''),0,len(root.get('MDDF',b''))//36,'<2I6f2H'))
    b = Builder(assets)
    for name in sorted(names):
        if not re.search('tree|pine|bush|fern|branch|shrub|plant|redwood',name,re.I):
            continue
        name = re.sub(r'\.(mdx|mdl)$','.m2',name,flags=re.I)
        key = hashlib.sha256(('FGS3-v1:'+name).encode()).hexdigest()
        target = a.cache.resolve()/'models'/(key+'.fgs')
        if not target.is_file():
            continue
        vertices,triangles,materials = b.m2(name)
        used = sorted({v for t in triangles for v in t[:3]})
        remap = {old:new for new,old in enumerate(used)}
        mesh = ([vertices[i] for i in used],[(remap[x],remap[y],remap[z],m) for x,y,z,m in triangles],materials)
        fresh = Builder(assets)
        fresh.textures = b.textures
        fresh.cutout_textures = b.cutout_textures
        fresh.add_mesh(mesh,((1,0,0),(0,1,0),(0,0,1)),(0,0,0),'model')
        candidate = out/(key+'.fgs')
        fresh.write_scene(candidate)
        old,new = target.read_bytes(),candidate.read_bytes()
        header = struct.unpack_from('<4s4I',old)
        end = 20+header[2]*32+header[3]*16
        if old[:end] != new[:end]:
            records.append({'model':name,'status':'geometry-mismatch-skipped'})
            continue
        if old == new:
            continue
        def dimensions(data):
            at = end
            values = []
            for _ in range(header[4]):
                _,_,_,w,h,cut,size = struct.unpack_from('<3fIIfI',data,at)
                values.append([w,h,cut])
                at += 28+size
            assert at == len(data)
            return values
        record = {'model':name,'file':str(target),'geometry_equal':True,'before':dimensions(old),'after':dimensions(new),
                  'old_sha256':hashlib.sha256(old).hexdigest(),'new_sha256':hashlib.sha256(new).hexdigest(),'applied':a.apply}
        if a.apply:
            saved = backup/target.name
            if not saved.exists():shutil.copy2(target,saved)
            candidate.replace(target)
        records.append(record)
        print(name,record['before'],'->',record['after'],flush=True)
finally:
    assets.close()
(out/'validation.json').write_text(json.dumps({'applied':a.apply,'records':records},indent=2))
print('Refreshed',sum(r.get('applied',False) for r in records),'foliage models')
