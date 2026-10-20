#!/usr/bin/env python3
"""Extract actual client celestial textures; does not modify native assets."""
import argparse,hashlib,json,struct,sys
from pathlib import Path
sys.path[:0]=[str(Path(__file__).resolve().parents[1]),str(Path(__file__).resolve().parent)]  # repo root, renderer/
from world_scene_builder import Assets,decode_blp
import client_archives
HERE=Path(__file__).resolve().parent
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,help='default: <client>/world-cache/celestial')
    client_archives.add_arguments(p);args=p.parse_args()
    assets=Assets.from_args(args);output=args.output or assets.client/'world-cache/celestial';output.mkdir(parents=True,exist_ok=True);manifest=[]
    try:
        for stem,path in [('sun','textures\\suncenter.blp'),('moon','textures\\moon.blp')]:
            blob=assets.read(path);w,h,rgba=decode_blp(blob,512)  # the HD moon.blp is 512: its full detail
            data=struct.pack('<4sIII',b'FCT1',1,w,h)+rgba;destination=output/(stem+'.fct');destination.write_bytes(data)
            # Matching references retain the source's encoded mip bytes. Native
            # BC uploads can be compared byte-for-byte; decompressed uploads use
            # the same C++ decoder for both sides to avoid RGB565 rounding drift.
            encoding,alpha_depth,alpha_encoding,_=struct.unpack_from('<4B',blob,8)
            source_w,source_h=struct.unpack_from('<II',blob,12);offsets=struct.unpack_from('<16I',blob,20);sizes=struct.unpack_from('<16I',blob,84)
            levels=[];mw,mh=source_w,source_h
            for level in range(16):
                if not offsets[level]:break
                if mw<=128 and mh<=128:
                    if encoding==2:
                        fmt=0x31545844 if alpha_depth in (0,1) else 0x33545844 if alpha_encoding==1 else 0x35545844
                        raw=blob[offsets[level]:offsets[level]+sizes[level]]
                    else:
                        # Palettized BLPs are uploaded as native BGRA8.
                        dw,dh,decoded=decode_blp(blob,max(mw,mh));assert (dw,dh)==(mw,mh)
                        fmt=21;raw=b''.join(bytes((decoded[i+2],decoded[i+1],decoded[i],decoded[i+3])) for i in range(0,len(decoded),4))
                    levels.append(struct.pack('<4I',mw,mh,fmt,len(raw))+raw)
                mw=max(1,mw//2);mh=max(1,mh//2)
            match_data=struct.pack('<4sII',b'FCM1',1,len(levels))+b''.join(levels)
            (output/(stem+'.fcm')).write_bytes(match_data)
            manifest.append({'body':stem,'source':path,'archive':assets.origin(path),
                'asset_sha256':hashlib.sha256(blob).hexdigest(),'width':w,'height':h,'decoded_rgba8_sha256':hashlib.sha256(rgba).hexdigest(),
                'cache_sha256':hashlib.sha256(data).hexdigest(),'match_sha256':hashlib.sha256(match_data).hexdigest(),'native_asset_modified':False})
        (output/'manifest.json').write_text(json.dumps({'format':'FCT1 header magic/version/width/height then tightly packed original sRGB RGBA8','textures':manifest,
            'archive_fingerprint':assets.fingerprint()},indent=2)+'\n')
    finally:assets.close()
if __name__=='__main__':main()
