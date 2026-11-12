"""Read the non-sparse WDC5 layouts used by the local Forever atmosphere import.

Format reference: wowdev/DBCD WDC5Reader.cs (schema + compression metadata).
Encrypted or truncated sections are excluded in full; no zero-filled rows.
"""
from pathlib import Path
import struct, re, json

def decode(name, root):
    ROOT=Path(root)
    path=ROOT/'tables'/(name+'.db2')
    if not path.exists():path=ROOT/'tables'/(name+'.partial.db2')
    data=path.read_bytes()
    assert data[:4]==b'WDC5'
    h=struct.unpack_from('<9I2H7I',data,136)
    layouts={'Light':0x5F16BC84,'LightData':0x360DA016,'LightDataGlobalVolumeFog':0x24290E20,
             'ZoneLight':0x94CE95E0,'ZoneLightPoint':0xDE2377FB,'Weather':0x1628B83C}
    if name not in layouts or h[5]!=layouts[name]:raise ValueError(f'Unsupported {name} layout {h[5]:08X}')
    count,fields,size=h[:3]
    assert not h[9]&1, 'sparse unsupported'
    schema=(ROOT/(name+'.dbd')).read_text(encoding='utf-8-sig')
    blocks=schema.split('\n\n')
    types={re.sub(r'<.*?>','',line.split()[1]).rstrip('?'):line.split()[0] for line in blocks[0].splitlines()[1:] if line.strip()}
    block=next(b for b in blocks if f'{h[5]:08X}' in b)
    specs=[]
    for line in block.splitlines():
        if not line or line.startswith(('BUILD','LAYOUT','COMMENT')) or 'noninline' in line:continue
        key=re.sub(r'\$.*?\$|<.*?>|\[.*?\]','',line)
        match=re.search(r'\[(\d+)\]',line)
        bitspec=re.search(r'<(u?)(\d+)>',line)
        specs.append((key,types[key],int(match[1]) if match else 1, (int(bitspec[2]),bool(bitspec[1])) if bitspec else None))
    assert len(specs)==fields,(name,len(specs),fields)
    sections=[struct.unpack_from('<Q8I',data,204+40*i) for i in range(h[-1])]
    off=204+40*h[-1]
    meta=[struct.unpack_from('<hh',data,off+4*i) for i in range(fields)];off+=4*fields
    storage=[struct.unpack_from('<HHIIIII',data,off+24*i) for i in range(fields)];off+=24*fields
    palettes={};commons={}
    for kind in (3,4):
        for i,s in enumerate(storage):
            if s[3]==kind:
                palettes[i]=struct.unpack_from('<'+'I'*(s[2]//4),data,off);off+=s[2]
    for i,s in enumerate(storage):
        if s[3]==2:
            commons[i]=dict(struct.unpack_from('<II',data,off+j) for j in range(0,s[2],8));off+=s[2]
    out={};skipped=[]
    for section in sections:
        key,start,n,strings,end,idsz,relsz,sparse,copies=section
        section_end=start+n*size+strings+idsz+copies*8+relsz
        if section_end>len(data):skipped.append({'key':hex(key),'records':n});continue
        assert sparse==0
        idpos=start+n*size+strings
        relpos=idpos+idsz+copies*8
        relationships={}
        if relsz:
            rn=struct.unpack_from('<I',data,relpos)[0]
            relationships={idx:val for val,idx in (struct.unpack_from('<II',data,relpos+12+8*j) for j in range(rn))}
        ids=struct.unpack_from('<'+'I'*(idsz//4),data,idpos)
        for row in range(n):
            rowpos=start+row*size;bits=int.from_bytes(data[rowpos:rowpos+size],'little');values={}
            rid=ids[row] if ids else None
            for i,(field,typ,arr,cast) in enumerate(specs):
                s=storage[i];pos,width,extra,kind,a,b,card=s
                raw=(bits>>pos)&((1<<width)-1)
                if kind==2:vals=[commons[i].get(rid,a)]
                elif kind==3:vals=[palettes[i][raw]]
                elif kind==4:vals=palettes[i][raw*card:(raw+1)*card];assert len(vals)==arr
                elif kind==0:
                    w=32-meta[i][0];assert w>0
                    vals=[(bits>>(pos+j*w))&((1<<w)-1) for j in range(arr)]
                elif kind in (1,5):
                    assert arr==1
                    vals=[raw-(1<<width) if kind==5 and raw&(1<<(width-1)) else raw]
                else:raise ValueError(kind)
                if cast:
                    mask=(1<<cast[0])-1
                    vals=[v&mask for v in vals]
                    if not cast[1]:vals=[v-(mask+1) if v&(1<<(cast[0]-1)) else v for v in vals]
                if typ=='float':vals=[struct.unpack('<f',struct.pack('<I',v&0xffffffff))[0] for v in vals]
                elif typ in ('string','locstring'):
                    vals=[data[rowpos+pos//8+v-(count-n)*size:].split(b'\0',1)[0].decode('utf-8') if v else '' for v in vals]
                values[field]=vals if arr>1 else vals[0]
                if field=='ID':rid=values[field]
            values['ID']=rid
            if name=='ZoneLightPoint':values['ZoneLightID']=relationships[row]
            assert rid not in out
            out[rid]=values
        for j in range(copies):
            dest,source=struct.unpack_from('<II',data,idpos+idsz+j*8)
            out[dest]=dict(out[source],ID=dest)
    # Keep partial/encrypted section counts visible to the importer.
    print(name,len(out),'skipped',skipped)
    return out
