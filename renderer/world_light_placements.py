#!/usr/bin/env python3
"""Authored light placement discovery independent of renderable mesh coverage.

Only ADT MDDF/MODF and the WMO doodads the game instantiates (MODR refs, MODS sets, MODD) establish instances.
No skin/triangle requirement, no fabricated lamps, no runtime/game clocks.
Matrices use the same audited world-scene placement functions as FGS3.
"""
import hashlib,math,re,struct
from world_scene_builder import chunks,unpack,array,string,placement,quaternion_matrix,matmul,matvec,wmo_group,doodad_indices

INSTANCE=struct.Struct('<32sQI18f')
def model_path(path):
    return re.sub(r'\.(mdx|mdl)$','.m2',path.replace('/','\\').lower())

def record(path,uid,kind,matrix,translation):
    if not 0<=uid<2**64:raise ValueError('Placement UID outside uint64')
    values=tuple(x for row in matrix for x in row)+tuple(translation)
    if not all(math.isfinite(x) and abs(x)<100000 for x in values):raise ValueError('Invalid light placement transform')
    if not any(x for row in matrix for x in row):return None # explicitly hidden zero-scale instance
    key=hashlib.sha256(('FGS3-v1:'+path).encode()).digest()
    # Bounds are unused by the light extractor; influence comes from authored
    # light position and attenuation. Match FGS3's float32 transform precision.
    return INSTANCE.unpack(INSTANCE.pack(key,uid,kind,*(0.,)*6,*values))

def names(root,index_chunk,string_chunk):
    data=root.get(index_chunk,b'')
    if len(data)%4:raise ValueError('Invalid ADT name-index chunk')
    return [string(root.get(string_chunk,b''),offset) for offset, in array(data,0,len(data)//4,'<I')]

def group_blobs(read,path,blob):
    """The group files of a root WMO (read(name) -> bytes) that exist."""
    count=unpack('<I',dict(chunks(blob)).get('MOHD',b''),4)[0]
    if count>4096:raise ValueError('WMO group count')
    for i in range(count):
        try:yield read(path[:-4]+f'_{i:03d}.wmo')
        except FileNotFoundError:continue

def group_refs(group_blobs):
    """MODR doodad refs of the group files the game loads (CMapObj::CreateRefs); a group that is not there has none."""
    refs=set()
    for blob in group_blobs:
        sub=dict(chunks(wmo_group(blob)[0]['MOGP'],68));modr=sub.get('MODR',b'')
        refs.update(x[0] for x in array(modr,0,len(modr)//2,'<H'))
    return refs

def wmo_doodads(blob,refs=None):
    """Parse placement-only WMO chunks once; do not open group render meshes. `refs` are the MODR doodad refs
    of its groups (group_refs); None does not filter by them."""
    root={tag:data for tag,data in chunks(blob) if tag in ('MODS','MODD','MODN')}
    sets=root.get('MODS',b'');data=root.get('MODD',b'');text=root.get('MODN',b'')
    if len(sets)%32 or len(data)%40:raise ValueError('Invalid WMO doodad placement record size')
    result=[]
    for offset in range(0,len(data),40):
        packed=unpack('<I',data,offset)[0]
        path=model_path(string(text,packed&0xffffff))
        pos=unpack('<3f',data,offset+4);quat=unpack('<4f',data,offset+16);scale=unpack('<f',data,offset+32)[0]
        if not all(math.isfinite(v) for v in (*pos,*quat,scale)):raise ValueError('Nonfinite WMO doodad transform')
        result.append((path,pos,quat,scale))
    selection=[]
    for offset in range(0,len(sets),32):
        first,count=unpack('<II',sets,offset+20)
        if first+count>len(result):raise ValueError('WMO doodad set exceeds MODD')
        selection.append((first,count))
    return result,selection,refs

def discover(blob,read_wmo,source_label=''):
    """Yield (canonical source asset, FGS3-shaped instance) from one ADT.

    read_wmo(name) returns cached wmo_doodads metadata; source errors should
    propagate to the builder's report, never turn into guessed placements.
    """
    root={tag:data for tag,data in chunks(blob) if tag in ('MVER','MMDX','MMID','MWMO','MWID','MDDF','MODF')}
    if unpack('<I',root.get('MVER',b''))[0]!=18:raise ValueError('Unsupported ADT version')
    models=names(root,'MMID','MMDX');wmos=names(root,'MWID','MWMO');seen=set()
    direct=root.get('MDDF',b'');buildings=root.get('MODF',b'')
    if len(direct)%36 or len(buildings)%64:raise ValueError('Invalid ADT placement record size')
    for entry in array(direct,0,len(direct)//36,'<2I6f2H'):
        name_id,uid=entry[:2]
        if name_id>=len(models):raise ValueError('MDDF name outside MMDX')
        if (1,uid) in seen:continue
        seen.add((1,uid));path=model_path(models[name_id])
        matrix,translation=placement(entry[2:5],entry[5:8],entry[8]/1024)
        item=record(path,uid,1,matrix,translation)
        if item is not None:yield path,item
    for entry in array(buildings,0,len(buildings)//64,'<2I12f4H'):
        name_id,uid=entry[:2]
        if name_id>=len(wmos):raise ValueError('MODF name outside MWMO')
        if (2,uid) in seen:continue
        seen.add((2,uid));path=wmos[name_id]
        matrix,translation=placement(entry[2:5],entry[5:8],1.)
        yield path,record(path,uid,2,matrix,translation)
        doodads,sets,refs=read_wmo(path)
        for index in doodad_indices(sets,len(doodads),refs,entry[15]):   # same selection as the client cache
            name,pos,quat,scale=doodads[index]
            if scale==0:continue
            local=quaternion_matrix(quat,scale);rotation=matmul(matrix,local);offset=matvec(matrix,pos)
            item=record(name,(uid<<32)|index,3,rotation,tuple(offset[i]+translation[i] for i in range(3)))
            if item is not None:yield name,item
