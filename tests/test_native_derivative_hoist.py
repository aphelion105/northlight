#!/usr/bin/env python3
# northlight-test:
"""Native derivative hoist preserves inputs and fails closed on unsupported SM3."""
from pathlib import Path
import sys,unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts/shaders'))
from compile_local_light_shaders import hoist_native_derivatives
VERSION=0xffff0300;END=0xffff
DCL=[0x0200001f,0x80000005,0x900f0002]
DSX=[0x0200005b,0x80070000,0x90c90002]
DSY=[0x0200005c,0x80070001,0x90d20002]
BASE=[VERSION,*DCL,0x01000028,0xe0000000,*DSX,*DSY,0x0000002b,END]
class Hoist(unittest.TestCase):
 def test_fresh_temps_and_original_sites(self):
  got=hoist_native_derivatives(BASE)
  self.assertEqual(got[:4],[VERSION,*DCL]);self.assertEqual(got[4:10],[DSX[0],0x80070002,DSX[2],DSY[0],0x80070003,DSY[2]])
  self.assertEqual(got[10:],[0x01000028,0xe0000000,0x02000001,DSX[1],0x80e40002,0x02000001,DSY[1],0x80e40003,0x2b,END])
 def test_rejected_shapes(self):
  variants=[]
  for index,value in [(0,0xffff0200),(-1,0), (6,0x0300005b),(6,0x1200005b),(8,0x90c92002),(8,0x91c90002),(8,0x80c90002),(8,0x90c90003),(7,0x80170000),(7,0x81070000),(9,0x0200005b)]:
   bad=BASE.copy();bad[index]=value;variants.append(bad)
  variants += [BASE[:-2]+DCL+BASE[-2:],BASE[:6]+BASE[9:]]
  variants += [[VERSION,*DCL]+sum(([0x02000001,0x800f0000+i,0x90e40002] for i in range(31)),[])+DSX+DSY+[END]]
  for bad in variants:
   with self.subTest(words=bad):
    with self.assertRaises(ValueError):hoist_native_derivatives(bad)
if __name__=='__main__':unittest.main()
