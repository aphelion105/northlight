"""Cutout cache preserves fine source alpha while opaque GI textures stay small."""
from pathlib import Path
import struct
import sys

sys.path[:0] = [str(Path(__file__).resolve().parents[1]), str(Path(__file__).resolve().parents[1]/'renderer')]
from world_scene_builder import Builder, decode_blp

full = bytes(v for y in range(256) for x in range(256) for v in (32,64,96,255*((x+y)%2)))
mip = bytes((32,64,96,128))*128*128
header = bytearray(148)
header[:4] = b'BLP2'
struct.pack_into('<I4BII', header, 4, 1,3,8,0,1,256,256)
struct.pack_into('<2I', header, 20, 148,148+len(full))
struct.pack_into('<2I', header, 84, len(full),len(mip))
blob = bytes(header)+full+mip

class Assets:
    unsupported = set()
    def read(self, path):
        assert path == 'leaves.blp'
        return blob

b = Builder(Assets())
opaque = b.material('leaves.blp',0)
cutout = b.material('leaves.blp',1)
assert b.materials[opaque][1:3] == (128,128)
assert b.materials[cutout][1:3] == (256,256)
assert b.materials[cutout][4][3::4] == full[3::4]
assert len(set(b.materials[opaque][4][3::4])) == 1
assert set(b.materials[cutout][4][3::4]) == {0,255}
assert b.materials[cutout][3] == .5
assert not b.assets.unsupported
print('PASS cutout cache: source alpha exact, opaque GI mip 128, cutoff unchanged')
