#!/usr/bin/env python3
# northlight-test: requires=stormlib
"""Placement coverage tests: emitters need neither a render mesh nor a skin."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp; fp.use_source_modules()
import math,struct,unittest
import world_light_placements as p
from world_scene_builder import doodad_indices

def chunk(tag,data):return struct.pack('<4sI',tag[::-1].encode(),len(data))+data
def adt(direct=b'',buildings=b''):
    return b''.join(chunk(k,v) for k,v in [('MVER',struct.pack('<I',18)),('MMDX',b'InvisibleLamp.mdx\0'),('MMID',struct.pack('<I',0)),('MWMO',b'room.wmo\0'),('MWID',struct.pack('<I',0)),('MDDF',direct),('MODF',buildings)])
def root(uid=7,selected=1):return struct.pack('<2I12f4H',0,uid,*([0.]*12),0,selected,0,0)
def doodad(name,pos=(0,0,0),quat=(0,0,0,1),scale=1):return struct.pack('<I3f4ffI',name,*pos,*quat,scale,0xffffffff)
def modset(first,count):return b'\0'*20+struct.pack('<III',first,count,0)

class PlacementTests(unittest.TestCase):
    def test_no_mesh_no_skin_direct_and_duplicate(self):
        d=struct.pack('<2I6f2H',0,42,*([0.]*6),1024,0)
        items=list(p.discover(adt(d+d),lambda name:self.fail('No WMO should be read')))
        self.assertEqual(len(items),1);self.assertEqual(items[0][0],'invisiblelamp.m2')
        self.assertEqual(items[0][1][1:3],(42,1))
    def test_default_selected_only_hidden_and_composition(self):
        names=b'default.m2\0selected.m2\0inactive.m2\0hidden.m2\0'
        offsets=[0,11,23,35]
        q=(0,0,math.sqrt(.5),math.sqrt(.5))
        blob=chunk('MODN',names)+chunk('MODD',b''.join([doodad(offsets[0]),doodad(offsets[1],(2,3,4),q),doodad(offsets[2]),doodad(offsets[3],scale=0)]))+chunk('MODS',modset(0,1)+modset(1,1)+modset(2,2))
        metadata=p.wmo_doodads(blob)
        items=list(p.discover(adt(buildings=root()),lambda _:metadata))
        self.assertEqual([x[0] for x in items],['room.wmo','default.m2','selected.m2'])
        self.assertEqual(items[2][1][1],(7<<32)|1)
        # The transformed child unit X is root(local rotation X + local pos).
        r=items[0][1];c=items[2][1];m=[r[9+i*3:12+i*3] for i in range(3)]
        expected=[sum(m[i][j]*v for j,v in enumerate((2,4,4)))+r[18+i] for i in range(3)]
        actual=[c[9+i*3]+c[18+i] for i in range(3)]
        for a,b in zip(actual,expected):self.assertAlmostEqual(a,b,places=2)
        items=list(p.discover(adt(buildings=root(selected=2)),lambda _:metadata))
        self.assertEqual([x[0] for x in items],['room.wmo','default.m2','inactive.m2'])
    def test_game_doodad_selection(self):
        # CMapObj::CreateRefs: only MODR-listed doodads; each belongs to the FIRST set holding it; set 0 or the placed set.
        sets=[(0,1),(1,2),(2,2),(9,0)]   # doodad 2 is in sets 1 and 2 (set 1 wins); 4.. are in no set; set 3 is empty
        self.assertEqual(doodad_indices(sets,5,None,0),[0])
        self.assertEqual(doodad_indices(sets,5,None,1),[0,1,2])
        self.assertEqual(doodad_indices(sets,5,None,2),[0,3])   # 2 is set 1's, not 2's
        self.assertEqual(doodad_indices(sets,5,{1,3,4,9},1),[1])
        self.assertEqual(doodad_indices(sets,5,{0,3},2),[0,3])
        self.assertEqual(doodad_indices(sets,5,set(),1),[])
        self.assertEqual(doodad_indices([],5,None,0),[])   # no MODS: nothing exists
        names=b'a.m2\0b.m2\0';blob=chunk('MODN',names)+chunk('MODD',doodad(0)+doodad(5))+chunk('MODS',modset(0,2))
        items=list(p.discover(adt(buildings=root()),lambda _:p.wmo_doodads(blob,{1})))
        self.assertEqual([x[0] for x in items],['room.wmo','b.m2'])
    def test_invalid_records_fail(self):
        for blob in [adt(b'x'),adt(buildings=b'x'),adt(struct.pack('<2I6f2H',1,42,*([0.]*6),1024,0))]:
            with self.assertRaises(ValueError):list(p.discover(blob,lambda _:None))
        for blob in [chunk('MODD',b'x'),chunk('MODS',b'x'),chunk('MODS',modset(0,1))]:
            with self.assertRaises(ValueError):p.wmo_doodads(blob)
    def test_float32_identity_stability_and_zero_scale(self):
        m=((1,0,0),(0,1,0),(0,0,1));a=p.record('lamp.m2',7,1,m,(1/3,0,0))
        self.assertEqual(a,p.INSTANCE.unpack(p.INSTANCE.pack(*a)))
        self.assertIsNone(p.record('lamp.m2',7,1,((0,0,0),)*3,(0,0,0)))
        with self.assertRaises(ValueError):p.record('lamp.m2',7,1,m,(math.nan,0,0))

if __name__=='__main__':unittest.main()
