#!/usr/bin/env python3
# northlight-test: requires=client,stormlib
"""Check source tile coverage, FGS3 bounds/keys, and every referenced FGS2 header.

This performs file/data checks only. Runtime shader/camera alignment remains a
user game test. The full FGS2 payload parser is separately tested in world_gi.
"""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp; fp.use_source_modules()
import argparse, json, math, re, struct, time
from pathlib import Path
from world_scene_builder import Assets, CLIENT, decode_blp, placement, quaternion_matrix, matvec
import client_archives

MAPS=['Azeroth','Kalimdor','Expansion01','Northrend']


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cache',type=Path,help='default: <client>/world-cache')
    p.add_argument('--report',type=Path,help='default: <cache>/validation.json');p.add_argument('--maps',nargs='+',choices=MAPS,default=MAPS)
    p.add_argument('--tiles',nargs=4,type=int,metavar=('X0','Y0','X1','Y1'),help='expect only these source tiles (partial test builds)')
    client_archives.add_arguments(p);args=p.parse_args()
    assets=Assets.from_args(args);root=args.cache or assets.client/'world-cache';models={};counts={};missing=[]
    try:
        for mapname in args.maps:
            pattern=re.compile(r'world\\maps\\'+mapname+r'\\'+mapname+r'_(\d+)_(\d+)\.adt$',re.I)
            expected={tuple(map(int,m.groups())) for path in assets.providers if (m:=pattern.fullmatch(path))}
            if args.tiles:expected={(x,y) for x,y in expected if args.tiles[0]<=x<=args.tiles[2] and args.tiles[1]<=y<=args.tiles[3]}
            checked=0;instance_count=0;deleted=[]
            for x,y in sorted(expected):
                path=root/mapname/f'{x}_{y}.fg3'
                if not path.exists():
                    source=f'world\\maps\\{mapname}\\{mapname}_{x}_{y}.adt'
                    if not assets.read(source):deleted.append([x,y]);continue
                    missing.append(str(path.relative_to(root)));continue
                data=path.read_bytes();magic,version,count=struct.unpack_from('<4sII',data)
                assert magic==b'FGS3' and version==3 and len(data)==12+116*count,path
                for record in struct.iter_unpack('<32sQI18f',data[12:]):
                    key,uid,category,*values=record
                    assert category in (0,1,2,3),path
                    assert all(math.isfinite(v) for v in values),path
                    assert all(values[i]<=values[i+3] for i in range(3)),path
                    matrix=values[6:15]
                    det=matrix[0]*(matrix[4]*matrix[8]-matrix[5]*matrix[7])-matrix[1]*(matrix[3]*matrix[8]-matrix[5]*matrix[6])+matrix[2]*(matrix[3]*matrix[7]-matrix[4]*matrix[6])
                    assert abs(det)>1e-12,(path,'singular instance transform')
                    if key not in models:
                        model=root/'models'/(key.hex()+'.fgs')
                        with model.open('rb') as stream:
                            magic,version,nv,nt,nm=struct.unpack('<4s4I',stream.read(20))
                        assert magic==b'FGS2' and version==2 and nv and nt and nm,model
                        assert model.stat().st_size>=20+32*nv+16*nt+28*nm,model
                        models[key]=model.stat().st_size
                checked+=1;instance_count+=count
            counts[mapname]={'source_tiles':len(expected),'source_deletion_markers':deleted,'active_source_tiles':len(expected)-len(deleted),'cached_tiles':checked,'instances':instance_count}
        header=bytearray(148);header[:4]=b'BLP2';struct.pack_into('<I',header,4,1)
        struct.pack_into('<4BII',header,8,2,1,0,1,4,4);struct.pack_into('<I',header,20,148);struct.pack_into('<I',header,84,8)
        bits=sum((3 if i%2 else 1)<<(i*2) for i in range(16))
        w,h,rgba=decode_blp(bytes(header)+struct.pack('<HHI',0,0xf800,bits))
        assert (w,h)==(4,4) and rgba[:8]==bytes([255,0,0,255,0,0,0,0])
        matrix,translation=placement((17066.6666667,123.,17066.6666667),(0,0,0),1.)
        assert abs(translation[0])<1e-6 and abs(translation[1])<1e-6 and translation[2]==123.
        rotated=matvec(quaternion_matrix((0.,0.,math.sqrt(.5),math.sqrt(.5)),1.),(1.,0.,0.))
        assert abs(rotated[0])<1e-6 and abs(rotated[1]-1.)<1e-6 and abs(rotated[2])<1e-6
        report={'checked_at_unix':time.time(),'archive_fingerprint':assets.fingerprint(),'coverage':counts,'referenced_models':len(models),
                'referenced_model_bytes':sum(models.values()),'missing_tiles':missing,
                'descriptor_bounds_finite':True,'transforms_invertible':True,'referenced_FGS2_headers_valid':True,
                'BC1_cutout_known_pattern':True,'ADT_origin_height_test':True,
                'MODD_quaternion_positive_90deg_test':True,
                'game_launched':False,'runtime_alignment_verified':False}
        (args.report or root/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps(report,indent=2))
        return 0 if not missing else 1
    finally:assets.close()


if __name__=='__main__':raise SystemExit(main())
