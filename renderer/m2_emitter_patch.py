"""Light-only v264 append, with one valid identity root for all new emitters.

Original MIT Northlight single-emitter implementation extended locally. No
game assets or GPL ComfyAtmosphere source are embedded in this helper.
"""
import math,struct


def append_lights(blob,emitters):
    if len(blob)<304 or blob[:4]!=b'MD20' or struct.unpack_from('<I',blob,4)[0]!=264:raise ValueError('Requires MD20/v264')
    if struct.unpack_from('<I',blob,264)[0]:raise ValueError('Preserve native lights')
    if not 1<=len(emitters)<=16:raise ValueError('One to sixteen compact source regions required')
    for p in emitters:
        if len(p['position'])!=3 or len(p['rgb'])!=3:raise ValueError('Vector size')
        if not all(math.isfinite(v) for key in ('position','rgb') for v in p[key]):raise ValueError('Nonfinite vector')
        if any(abs(v)>100 for v in p['position']) or not all(0<=v<=1 for v in p['rgb']) or max(p['rgb'])<=0:raise ValueError('Emitter position/colour')
        if not 0<p['intensity']<=4 or not 0<=p['start']<p['end']<=32:raise ValueError('Energy/range')
    globals,go=struct.unpack_from('<II',blob,20);bones,bo=struct.unpack_from('<II',blob,44)
    if globals>=32767 or bones>=32767 or globals and (go<304 or go+globals*4>len(blob)) or bones and (bo<304 or bo+bones*88>len(blob)):raise ValueError('Array bounds')
    out=bytearray(blob)
    def add(data):
        out.extend(b'\0'*(-len(out)%4));offset=len(out);out.extend(data);return offset
    empty=struct.pack('<Hh4I',0,-1,0,0,0,0)
    identity=struct.pack('<iIhHI',-1,0,-1,0,0)+empty*3+struct.pack('<3f',0,0,0)
    struct.pack_into('<II',out,44,bones+1,add(blob[bo:bo+bones*88]+identity))
    struct.pack_into('<II',out,20,globals+1,add(blob[go:go+globals*4]+struct.pack('<I',1)))
    def track(fmt,values):
        timestamp=add(struct.pack('<I',0));key=add(struct.pack(fmt,*values));times=add(struct.pack('<II',1,timestamp));keys=add(struct.pack('<II',1,key))
        return struct.pack('<hh4I',0,globals,1,times,1,keys)
    records=[]
    for p in emitters:
        light=struct.pack('<HH3f',1,bones,*p['position'])
        for fmt,value in [('<3f',(0,0,0)),('<f',(0,)),('<3f',p['rgb']),('<f',(p['intensity'],)),('<f',(p['start'],)),('<f',(p['end'],)),('<B',(1,))]:light+=track(fmt,value)
        assert len(light)==156;records.append(light)
    struct.pack_into('<II',out,264,len(records),add(b''.join(records)))
    return bytes(out)


def verify_preserved(original,patched,count=1):
    restored=bytearray(patched[:len(original)])
    for a,b in ((20,28),(44,52),(264,272)):restored[a:b]=original[a:b]
    if restored!=original:raise ValueError('Original geometry/track/model bytes changed')
    bones,bo=struct.unpack_from('<II',original,44);nb,nbo=struct.unpack_from('<II',patched,44);n,off=struct.unpack_from('<II',patched,264)
    if nb!=bones+1 or n!=count or nbo<len(original) or nbo+nb*88>len(patched) or off+n*156>len(patched):raise ValueError('Appended array bounds')
    if patched[nbo:nbo+bones*88]!=original[bo:bo+bones*88]:raise ValueError('Original bone data changed')
    empty=struct.pack('<Hh4I',0,-1,0,0,0,0)
    root=struct.pack('<iIhHI',-1,0,-1,0,0)+empty*3+struct.pack('<3f',0,0,0)
    if patched[nbo+bones*88:nbo+nb*88]!=root:raise ValueError('Not a static identity root')
    for i in range(n):
        if struct.unpack_from('<H',patched,off+i*156+2)[0]!=bones:raise ValueError('Invalid light bone index')


def append_light(blob,position,rgb,intensity,start,end):
    return append_lights(blob,[dict(position=position,rgb=rgb,intensity=intensity,start=start,end=end)])
