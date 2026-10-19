#!/usr/bin/env python3
# northlight-test:
"""Finite analytic-world fixtures for the liquid depth/mask contract (the consumers' rule: m.x > near, m.x <= scene distance + tolerance, m.y > .003).
0.3.214: the appearance passes (reflection, foam, shoreline) are gone with their shaders; what stays is the mask the AO, GI, fog, local lights and celestial discs read.
Independent world-space ray/plane intersections are the expected answers.
Shader reconstruction is tested against them, including D24 storage and FP32 math.
No game, device, or image generation is used.
"""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import math,struct,unittest,json
from pathlib import Path
ROOT=Path(__file__).resolve().parent

def f32(x):return struct.unpack('<f',struct.pack('<f',x))[0]
def half(x):return struct.unpack('<e',struct.pack('<e',x))[0]
def dot(a,b):return sum(x*y for x,y in zip(a,b))
def add(a,b):return tuple(x+y for x,y in zip(a,b))
def scale(a,s):return tuple(x*s for x in a)
def sub(a,b):return add(a,scale(b,-1))
def unit(a):return scale(a,1/math.sqrt(dot(a,a)))
def store_depth(z,near,far,minz=0,maxz=1):
    d=(far-far*near/z)/(far-near)
    return f32(round((minz+d*(maxz-minz))*16777215)/16777215)
def reconstruct_depth(raw,near,far,minz=0,maxz=1):
    n=f32(near);f=f32(far);d=max(0,min(1,f32(f32(raw-f32(minz))*f32(1/(maxz-minz)))))
    return f32(f32(n*f)/max(f32(f-f32(d*f32(f-n))),.00001))
def visible(mask_z,alpha,opaque_z,near):return mask_z>near and mask_z<=opaque_z+max(.012,mask_z*.0007) and alpha>.003
class Geometry(unittest.TestCase):
    def test_fog_water_uses_actual_scene_depth_before_sky_cap(self):
        # Independent flat-sea intersections in the last stretch of the frustum.
        # Native transparent water leaves clear scene depth at 1. Capping that
        # depth before testing the mask used to discard these valid receivers.
        cases=0
        for near in (.1,.2,.5,1.,2.):
            for far in (727.12,1277.,2000.):
                capped=reconstruct_depth(store_depth(far,near,far),near,far)
                n,f,d=f32(near),f32(far),f32(.99999)
                dry=f32(f32(n*f)/max(f32(f-f32(d*f32(f-n))),.00001))
                self.assertLess(dry,capped)
                for fraction in (.25,.5,.75):
                    z=dry+(capped-dry)*fraction
                    eye=(0.,0.,6.)
                    ray=(0.,1.,-eye[2]/z)
                    plane_z=-eye[2]/ray[2]
                    mask=f32(plane_z)
                    self.assertTrue(visible(mask,1,capped,near))
                    if visible(mask,1,dry,near):continue
                    old=dry
                    new=mask if visible(mask,1,capped,near) else dry
                    self.assertGreater(abs(eye[2]+ray[2]*old),.001)
                    self.assertLess(abs(eye[2]+ray[2]*new),1e-5)
                    cases+=1
                # Foreground opaque surfaces and empty/transparent masks stay
                # dry, and dry sky keeps exactly the old capped march length.
                for mask,alpha,scene in ((far*.8,1,far*.4),(far*.8,0,capped),(0,1,capped)):
                    self.assertFalse(visible(mask,alpha,scene,near))
                    self.assertEqual(mask if visible(mask,alpha,scene,near) else dry,dry)
        self.assertGreater(cases,25)
        shader=(fp.SHADERS/'world_effects.hlsl').read_text()
        self.assertIn('float water=waterDistance(uv,d);return water>0?water:viewDistance(min(d,.99999));',shader)
        self.assertEqual(shader.count('fogReceiverDistance(uv,depth)'),3)
        self.assertNotIn('receiverDistance(uv,min(depth,.99999))',shader)
    def test_geometry_mask_excludes_land_and_foreground(self):
        # Camera sees an actual rectangular liquid mesh. An infinite sloped
        # terrain plane crosses it at x=0; positive-x terrain occludes water.
        eye=(0.,0.,6.);pitch=.34
        right=(1.,0.,0.);up=(0.,math.sin(pitch),math.cos(pitch));forward=(0.,math.cos(pitch),-math.sin(pitch))
        near=.5;far=1000;counts={'wet':0,'occluded_land':0,'no_geometry':0}
        for y in range(1,96):
            for x in range(1,160):
                ray=add(add(scale(right,(2*x/160-1)/1.2),scale(up,(1-2*y/96)/1.7)),forward)
                if ray[2]>=-.001:continue
                water_z=-eye[2]/ray[2];water=add(eye,scale(ray,water_z))
                denominator=ray[2]-.2*ray[0]
                if abs(denominator)<.001:continue
                floor_z=-eye[2]/denominator
                if floor_z<=near:continue
                floor_point=add(eye,scale(ray,floor_z));in_mesh=-8<=water[0]<=8 and 8<=water[1]<=40
                mask=f32(water_z) if in_mesh else 0.;depth=reconstruct_depth(store_depth(floor_z,near,far),near,far)
                active=visible(mask,1 if in_mesh else 0,depth,near)
                if not in_mesh:counts['no_geometry']+=1;self.assertFalse(active)
                elif floor_point[2]>.08:counts['occluded_land']+=1;self.assertFalse(active)
                elif floor_point[2]<-.08:counts['wet']+=1;self.assertTrue(active)
        self.assertTrue(all(v>20 for v in counts.values()),counts)
        self.assertFalse(visible(12,0,30,near)) # retained original transparent alpha
        self.assertFalse(visible(12,1,8,near)) # opaque actor in front
        self.assertTrue(visible(12,1,18,near)) # submerged receiver behind
    def test_mask_distance_is_full_precision(self):
        # A half-float mask moves the water plane by more than the consumers' tolerance
        # at range; the float32 one stays inside it (the mask is G32R32F).
        z=256.12;tolerance=max(.012,z*.0007)
        self.assertGreater(abs(half(z)-z),.1);self.assertLess(abs(f32(z)-z),tolerance/1000)
    def test_multisample_decode_stays_inside_the_tolerance(self):
        # 0.3.214 (water MRT): the multisampled data target holds cover*distance and cover per sample; the resolve averages them and the
        # decode pass divides: the distance of the covered samples back, from 2 of 4 samples up, in float32.
        for distance in [.6,5.,37.25,256.12,999.9,2000.]:
            for covered in (2,3,4):
                mean_r=f32(sum([f32(distance)]*covered+[0.]*(4-covered))/4);mean_g=f32(covered/4)
                decoded=f32(mean_r*f32(1/mean_g))
                self.assertLessEqual(abs(decoded-distance),max(.012,distance*.0007)/100,(distance,covered,decoded))
                self.assertTrue(visible(decoded,1,distance,.5))   # still the same surface for the consumers
    def test_decode_threshold_is_a_majority_vote(self):
        # Fewer than half of the samples covered: the pixel is not water (the old single sample at the centre was covered about that often).
        for covered,water in [(0,False),(1,False),(2,True),(3,True),(4,True)]:self.assertEqual(f32(covered/4)-.5>=0,water)
    def test_mrt_contract_in_the_header(self):
        header=fp.src('water_renderer.h').read_text();patch=fp.src('water_shader_patch.h').read_text()
        self.assertGreaterEqual(header.count('D3DFMT_G32R32F'),2)           # the mask and the render target 1 data
        self.assertIn('constexpr float kMrtCover=0.003f;',patch)            # the consumers' m.y > .003
if __name__=='__main__':unittest.main()
