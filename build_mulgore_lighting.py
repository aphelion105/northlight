"""Mulgore-only warm prairie atmosphere in the existing lighting MPQ.

Use an explicit pre-change patch-z.mpq as input; never stack transformations.
Two parts: (a) the warm day bands and fog of Mulgore's profile, found through its
Light rows 201, 202 and 234 (the same ids on stock and HD); (b) the clone of the
HD skybox without its sun. (b) is skipped with the reason when the profile has
no HD sky (stock: sky 0), and (a) still applies.
Band layout: https://github.com/wowdev/noggit3/blob/default/src/noggit/Sky.h
Coordinates/radii and sky rings: same repository's Sky.cpp (scale 36).
No game execution. The renderer, draw distance and night palette are untouched.
Its warm day ends at 20:00 (strength 0 from then on); the input is already retimed
(build_lighting.retime), so the zone reaches night by 21:15 like its neighbours.
"""
import argparse
import hashlib
import json
import math
import struct
import sys
from pathlib import Path
from build_lighting import DBC, u, f, putu, putf, rgb, pack, mix, sample_color, require, Skip
from mpq import Archive

LIGHT_IDS = {201, 202, 234}
TABLES = ('Light', 'LightParams', 'LightIntBand', 'LightFloatBand')
# The HD skybox's own sun and flare batches, omitted from the clone.
OMIT = {'mulgor_sun.blp','mulgor_lensflare01.blp','mulgor_sunflare01.blp','mulgor_sunflare02.blp','mulgor_sunray01.blp'}
# Zenith -> 30/15/5/0-degree sky rings -> distance fog. Warm horizon,
# restrained lavender overhead; no cyan veil. Ambient fill stays cool so the
# existing geometry shadows retain contrast with peach-gold direct light.
DAY = {1:(82,96,111), 2:(174,172,188), 3:(231,182,165),
       4:(255,208,162), 5:(255,218,164), 6:(238,192,153),
       7:(191,153,129), 12:(245,206,176)}
EVENING = {1:(72,87,104), 2:(167,151,176), 3:(239,169,146),
           4:(255,198,143), 5:(255,212,146), 6:(241,177,132),
           7:(194,143,116), 12:(250,194,155)}

def strength(t):
    if t <= 600 or t >= 2400: return 0.
    if t < 720: return (t-600)/120
    if t <= 2360: return 1.
    return (2400-t)/40

def sample_float(row,t):
    pairs=sorted((u(row,2+i),f(row,18+i)) for i in range(u(row,1)))
    assert pairs
    if len(pairs)==1:return pairs[0][1]
    pairs=[(pairs[-1][0]-2880,pairs[-1][1])]+pairs+[(pairs[0][0]+2880,pairs[0][1])]
    for (a,x),(b,y) in zip(pairs,pairs[1:]):
        if a<=t<=b:return x+(y-x)*(t-a)/(b-a)
    raise ValueError(t)

def rewrite(row, floating, value):
    # Retain all authored key times. Explicit daytime endpoints keep the
    # original night interpolation exactly intact, including midnight wrap.
    times=sorted({u(row,2+i) for i in range(u(row,1))}|{600,720,2040,2360,2400})
    assert len(times)<=16
    old=bytearray(row);putu(row,1,len(times))
    for i in range(16):putu(row,2+i,0);putu(row,18+i,0)
    for i,t in enumerate(times):
        putu(row,2+i,t)
        if floating:putf(row,18+i,value(t,sample_float(old,t)))
        else:putu(row,18+i,pack(value(t,sample_color(old,t)),u(old,18)))

def build(source,output,client,assets=None,skybox=None,fog=None):
    """assets: a world_scene_builder.Assets of the client (default: the configured client);
    skybox: LightSkybox.dbc bytes (default: the client's Data/patch-x.mpq); fog: the world
    cache's fog folder (default: <client>/world-cache/fog). Raises build_lighting.Skip when the
    input is not the isolated Mulgore profile; without the HD skybox only the bands change."""
    with Archive(source) as a:
        payload={name:a.read(name) for name in a.names() if not name.startswith('(')}
    keys={n:'DBFilesClient\\'+n+'.dbc' for n in TABLES}
    tables={n:DBC(payload[keys[n]]) for n in TABLES}
    baseline={n:DBC(payload[keys[n]]) for n in TABLES}
    light,params,ints,floats=(tables[n] for n in TABLES)
    require(LIGHT_IDS<=set(light.index),'Mulgore Light rows absent')
    profiles={u(light.index[i],7) for i in LIGHT_IDS}
    # Fail closed if this input is not the expected isolated Mulgore profile.
    require(len(profiles)==1,'Mulgore Light rows do not share one profile')
    profile=next(iter(profiles))
    users={(u(r,0),col) for r in light.rows for col in range(7,15) if u(r,col)==profile}
    require(users=={(i,7) for i in LIGHT_IDS},f'Mulgore profile is shared: {sorted(users)}')
    fog=fog or client/'world-cache/fog'
    for i in LIGHT_IDS:
        r=light.index[i];require(u(r,1)==1,'Mulgore Light row not on Kalimdor')
        gx=f(r,2)/36/(533.3333333333334/16);gy=f(r,4)/36/(533.3333333333334/16)
        tx,ty=math.floor(gx/16),math.floor(gy/16)
        ix,iy=math.floor(gx)-16*tx,math.floor(gy)-16*ty
        b=(fog/f'Kalimdor/{tx}_{ty}.frf').read_bytes()
        require(struct.unpack_from('<I',b,24+4*(iy*16+ix))[0]==215,'Mulgore Light row outside Mulgore')
    owned=assets is None
    if owned:
        sys.path.insert(0,str(Path(__file__).resolve().parent/'renderer'))   # this repository's renderer/
        from world_scene_builder import Assets
        assets=Assets()
    sky_skipped=None
    try:
        if skybox is None:
            with Archive(client/'Data/patch-x.mpq') as a:skybox=a.read('DBFilesClient\\LightSkybox.dbc')
        sky=DBC(skybox);old_id=u(params.index[profile],2)
        require(old_id in sky.index,f'Mulgore sky {old_id} absent')
        original_name=sky.strings[u(sky.index[old_id],1):].split(b'\0',1)[0].decode()
        require(original_name[:-4].lower()+'.m2' in assets.providers,f'Mulgore sky model {original_name} absent')
        model=assets.read(original_name[:-4]+'.m2');count,offset=struct.unpack_from('<II',model,80)
        names={model[pos:pos+size].rstrip(b'\0').decode().lower().split('\\')[-1]
               for kind,_,size,pos in (struct.unpack_from('<4I',model,offset+16*i) for i in range(count)) if kind==0}
        require(OMIT<=names,f'Mulgore sky {old_id} has no HD sun batches')
    except Skip as skip:
        sky_skipped=str(skip)
    except BaseException:
        if owned:assets.close()
        raise
    # (a) The warm day bands and fog of the profile.
    output.mkdir(parents=True,exist_ok=True)
    changed_ints=set();changed_floats=set()
    for ch,target in DAY.items():
        row=ints.index[(profile-1)*18+ch+1];changed_ints.add(u(row,0))
        def color(t,old):
            evening=max(0.,min((t-1920)/360,1.))
            return mix(old,mix(target,EVENING[ch],evening),strength(t))
        rewrite(row,False,color)
    for ch in (0,1):
        row=floats.index[(profile-1)*6+ch+1];changed_floats.add(u(row,0))
        # Fog end 18% closer; start/end ratio smoothly approaches .28. These
        # are authored fog distances, not additional world geometry loading.
        rewrite(row,True,lambda t,old: old*(1-.18*strength(t)) if ch==0 else old+(.28-old)*strength(t))
    for n,t in tables.items():
        old=baseline[n]
        allowed=changed_ints if n=='LightIntBand' else changed_floats if n=='LightFloatBand' else set()
        assert t.fields==old.fields and t.strings==old.strings and len(t.rows)==len(old.rows)
        for row in t.rows:
            before=old.index[u(row,0)]
            if u(row,0) not in allowed:assert row==before
            else:
                assert 0<u(row,1)<=16
                times=[u(row,2+i) for i in range(u(row,1))]
                assert times==sorted(set(times)) and all(0<=v<2880 for v in times)
                # Night roundoff is at most half a byte after integer color
                # interpolation; floating bands retain float32 precision.
                for time in list(range(0,601,3))+list(range(2400,2880,3)):
                    if n=='LightIntBand':assert max(abs(a-b) for a,b in zip(sample_color(row,time),sample_color(before,time)))<=.51
                    else:assert abs(sample_float(row,time)-sample_float(before,time))<.005
        data=t.bytes();DBC(data);payload[keys[n]]=data
        (output/(n+'.dbc')).write_bytes(data)
    # (b) This HD client has textured clouds on skybox 172. Clone the model and
    # skybox row for this profile only, retaining geometry, UV motion, alpha,
    # and all night keys. Tint the existing day RGB tracks rather
    # than modifying any shared cloud textures or adding rendering work.
    # Skipped (sky_skipped) when the profile has no HD sky.
    cloned=None
    try:
        if sky_skipped is None:
            sky_before=sky.bytes()
            skyrow=bytearray(sky.index[old_id]);offset=u(skyrow,1)
            original_name=sky.strings[offset:].split(b'\0',1)[0].decode()
            original_model=assets.read(original_name[:-4]+'.m2')
            model=bytearray(original_model)
            nc,oc=struct.unpack_from('<II',model,72);assert nc==4
            permitted=set()
            for color in (0,2):
                track=oc+40*color
                interp,glob,nt,ot,nv,ov=struct.unpack_from('<Hh4I',model,track)
                assert interp==1 and glob==-1 and nt==nv==1
                count,times=struct.unpack_from('<2I',model,ot)
                values,value_offset=struct.unpack_from('<2I',model,ov);assert values==count
                for i in range(count):
                    t=struct.unpack_from('<I',model,times+4*i)[0]
                    # 800000-ms sky animation spans one day. Preserve the night
                    # endpoints (including 19:00 for the daytime cloud track).
                    if 200000<=t<=(633333 if color==0 else 600000):
                        at=value_offset+12*i
                        struct.pack_into('<3f',model,at,1.,.77,.57)
                        permitted.update(range(at,at+12))
            assert len(model)==len(original_model)
            assert all(a==b or i in permitted for i,(a,b) in enumerate(zip(model,original_model)))
            custom='Environments\\Stars\\fr_mulgore_warm'
            payload[custom+'.m2']=bytes(model)
            skins=struct.unpack_from('<I',model,68)[0];assert 0<skins<=4
            # The HD model has an independently animated sun + flare, while the
            # renderer owns the clock-based sun. Omit only those named batches in
            # this cloned skybox, otherwise the player sees two different suns.
            texture_count,texture_offset=struct.unpack_from('<II',model,80)
            texture_names=[]
            for i in range(texture_count):
                kind,flags,size,pos=struct.unpack_from('<4I',model,texture_offset+16*i)
                texture_names.append(bytes(model[pos:pos+size]).rstrip(b'\0').decode().lower() if kind==0 else '')
            lookup_count,lookup_offset=struct.unpack_from('<II',model,128)
            lookup=struct.unpack_from('<'+'H'*lookup_count,model,lookup_offset)
            omit=OMIT
            removed=[]
            for i in range(skins):
                skin=bytearray(assets.read(original_name[:-4]+f'{i:02d}.skin'));before_skin=bytes(skin)
                batch_count,batch_offset=struct.unpack_from('<II',skin,36);kept=[]
                for j in range(batch_count):
                    raw=bytes(skin[batch_offset+j*24:batch_offset+(j+1)*24]);assert len(raw)==24
                    batch=struct.unpack('<2B11H',raw);count,start=batch[8],batch[9]
                    assert start+count<=lookup_count
                    names=[texture_names[lookup[k]].split('\\')[-1] for k in range(start,start+count)]
                    if any(name in omit for name in names):removed.append({'skin':i,'batch':j,'textures':names})
                    else:kept.append(raw)
                assert len(kept)==batch_count-4
                struct.pack_into('<I',skin,36,len(kept))
                skin[batch_offset:batch_offset+24*batch_count]=b''.join(kept)+bytes(24*(batch_count-len(kept)))
                assert len(skin)==len(before_skin)
                assert skin[:36]==before_skin[:36] and skin[40:batch_offset]==before_skin[40:batch_offset]
                assert skin[batch_offset+24*batch_count:]==before_skin[batch_offset+24*batch_count:]
                payload[custom+f'{i:02d}.skin']=bytes(skin)
            new_id=max(sky.index)+1;putu(skyrow,0,new_id);putu(skyrow,1,len(sky.strings))
            sky.strings+=(custom+'.mdx').encode()+b'\0';sky.add(skyrow)
            putu(params.index[profile],2,new_id)
            # Only the isolated Mulgore profile can reference the cloned skybox.
            assert all(row==baseline['LightParams'].index[u(row,0)] for row in params.rows if u(row,0)!=profile)
            original_sky=DBC(sky_before)
            assert all(sky.index[id]==row for id,row in original_sky.index.items())
            payload['DBFilesClient\\LightSkybox.dbc']=sky.bytes()
            payload[keys['LightParams']]=params.bytes()
            (output/'LightParams.dbc').write_bytes(params.bytes())
            (output/'LightSkybox.dbc').write_bytes(sky.bytes())
            cloned={'old':old_id,'new':new_id,'model':custom+'.m2','original_model_sha256':hashlib.sha256(original_model).hexdigest(),'only_day_color_bytes_changed':True,'skin_count':skins,'removed_duplicate_sun_batches':removed}
    finally:
        if owned:assets.close()
    archive=output/'patch-z.mpq'
    with Archive(archive,create=True,capacity=max(16,len(payload)*2)) as a:
        for i,(name,data) in enumerate(payload.items()):
            file=output/f'payload-{i}.bin';file.write_bytes(data);a.add(file,name);file.unlink()
    with Archive(archive) as a:
        for name,data in payload.items():assert a.read(name)==data
    report={'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
            'archive_sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),
            'profile':profile,'light_ids':sorted(LIGHT_IDS),'zone':215,
            'changed_color_bands':sorted(DAY),'changed_float_bands':[0,1],
            'other_rows_byte_identical':True,'night_interpolation_preserved_within_quantization':True,
            'cloned_skybox':cloned,'sky_clone_skipped':sky_skipped,
            'renderer_changed':False,'game_launched':False,'visual_verified':False}
    (output/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args();build(args.source,args.output,Path(__file__).resolve().parent.parent)
