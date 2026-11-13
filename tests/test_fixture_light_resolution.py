#!/usr/bin/env python3
"""Geometry policy: no asset names, camera, native slot order or MPQ writes."""
from pathlib import Path
import math
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'renderer'))
from world_fixture_emission import open_flame_position


def bowl(rim=1.,lid=False,scale=1.):
    vertices=[(x,y,z) for z in (0.,rim) for x,y in ((-.5,-.5),(.5,-.5),(.5,.5),(-.5,.5))]
    faces=[(0,1,2,3),(0,4,5,1),(1,5,6,2),(2,6,7,3),(3,7,4,0)]
    if lid:faces.append((4,5,6,7))
    # A supporting leg supplies the fixture's ground level.
    vertices.extend([(-1.5,0.,-2.),(1.5,0.,-2.),(0.,0.,0.)])
    triangles=[tri for a,b,c,d in faces for tri in ((a,b,c),(a,c,d))]+[(8,9,10)]
    return [tuple(k*scale for k in v) for v in vertices],triangles


class Resolution(unittest.TestCase):
    def resolve(self,rim=1.,lid=False,scale=1.):
        vertices,triangles=bowl(rim,lid,scale)
        return open_flame_position(vertices,triangles,(0.,0.,.8*scale),(0.,0.,.7*scale),2*scale,12*scale)

    def test_open_bowl_keeps_central_shadow_and_uses_smallest_step(self):
        result=self.resolve()
        self.assertIsNotNone(result)
        self.assertEqual(result['baseline_visibility'],0)
        self.assertGreaterEqual(result['resolved_visibility'],.45)
        self.assertLessEqual(result['offset'][2],result['max_lift'])
        self.assertEqual(result['offset'][:2],(0.,0.))

    def test_closed_lid_refuses_and_same_asset_changed_geometry_recomputes(self):
        self.assertIsNone(self.resolve(lid=True))
        self.assertIsNone(self.resolve(rim=1.2)) # Higher walls need travel beyond this policy's budget.

    def test_scale_covariance(self):
        a,b=self.resolve(),self.resolve(scale=3.)
        self.assertAlmostEqual(b['offset'][2],a['offset'][2]*3)
        self.assertEqual(a['resolved_visibility'],b['resolved_visibility'])

    def test_unsupported_or_unrelated_sources_keep_original(self):
        v,t=bowl()
        for point,nozzle,radius in [((2,0,.8),(0,0,.7),2),((0,0,.8),(8,0,.7),2),
                                    ((0,0,math.nan),(0,0,.7),2),((0,0,.8),(0,0,.7),.01)]:
            self.assertIsNone(open_flame_position(v,t,point,nozzle,radius,12))
        self.assertIsNone(open_flame_position(v,t*700,(0,0,.8),(0,0,.7),2,12))


if __name__=='__main__':unittest.main()
