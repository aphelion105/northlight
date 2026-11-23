#!/usr/bin/env python3
# northlight-test:
"""Analytic D3D9 integer raster centers versus texture texel centers.

CPU reference only: no game, graphics device or GPU is started. Includes odd
viewport heights, half-resolution representatives, LH/RH projections, viewport
depth range and planar shadow PCF. Run this file to regenerate its JSON report.
"""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
from scripts.shaders.shader_sources import world_source_bytes
import hashlib
import json
import math
from pathlib import Path

HERE = Path(__file__).resolve().parent


def depth_pixel(uv, dimensions):
    return tuple(max(0, min(n-1, math.floor(u*n))) for u, n in zip(uv, dimensions))


def depth_uv(uv, dimensions):
    return tuple((p+.5)/n for p, n in zip(depth_pixel(uv, dimensions), dimensions))


def reconstruct(uv, depth, dimensions, scales, sign, near=.2, far=1277):
    distance = near*far/(far-depth*(far-near))
    raster = tuple(u-.5/n for u, n in zip(uv, dimensions))
    return ((raster[0]*2-1)*distance/scales[0],
            (1-raster[1]*2)*distance/scales[1], sign*distance)


def run():
    maximum_projection_error = 0
    count = 0
    mappings = []
    for dimensions in ((1728,1117), (1728,1116), (1921,1081), (1920,1080)):
        half = tuple(n//2 for n in dimensions)
        changed_rows = 0
        for j in range(half[1]):
            # Test every half-resolution scanline, including odd-height drift.
            for i in (0, 1, half[0]//2, half[0]-2, half[0]-1):
                output_uv = ((i+.5)/half[0], (j+.5)/half[1])
                pixel = depth_pixel(output_uv, dimensions)
                canonical = depth_uv(output_uv, dimensions)
                assert depth_pixel(canonical, dimensions) == pixel
                if i == 0 and pixel[1] != 2*j+1:
                    changed_rows += 1
                for sign in (-1, 1):
                    for distance in (.2, 1, 48, 192, 900, 1277):
                        # Forward projection is independent of inverse code.
                        near, far = .2, 1277
                        ndc_depth = far/(far-near)-near*far/((far-near)*distance)
                        raw_depth = ndc_depth*.94
                        p = reconstruct(canonical, raw_depth/.94, dimensions,
                                        (1.2686,1.9626),sign)
                        clip_w = sign*p[2]
                        raster_x = (p[0]*1.2686/clip_w+1)*dimensions[0]/2
                        raster_y = (1-p[1]*1.9626/clip_w)*dimensions[1]/2
                        error = max(abs(raster_x-pixel[0]),abs(raster_y-pixel[1]),
                                    abs(p[2]-sign*distance))
                        maximum_projection_error=max(maximum_projection_error,error)
                        assert error < 2e-8
                        count += 1
        mappings.append({'dimensions':dimensions,'half_dimensions':half,
                         'rows_different_from_naive_2j_plus_1':changed_rows})
        # Canonical full-resolution centers stay stable, including both edges.
        for x in (0,1,dimensions[0]//2,dimensions[0]-1):
            for y in (0,1,dimensions[1]//2,dimensions[1]-1):
                uv=((x+.5)/dimensions[0],(y+.5)/dimensions[1])
                assert depth_pixel(uv,dimensions)==(x,y)
                assert depth_uv(uv,dimensions)==uv

    # Shadow-map planar depths are evaluated at raster positions j/N, while
    # texture samples are at (j+.5)/N. The receiver and taps must use the same
    # translated coordinates for receiver-plane depth compensation.
    size=1024
    plane_error=0
    old_plane_error=0
    shadow_cases=0
    for receiver in ((.235,.619),(.5,.5),(.832,.179)):
        for gradient in ((.6,-.2),(-.4,.9),(.1,.1)):
            depth=.5+sum(g*(r-.5) for g,r in zip(gradient,receiver))
            sample_receiver=tuple(u+.5/size for u in receiver)
            base=tuple(math.floor(u*size) for u in sample_receiver)
            for ox in (-1,0,1):
                for oy in (-1,0,1):
                    pixel=(base[0]+ox,base[1]+oy)
                    tap=tuple((p+.5)/size for p in pixel)
                    stored=.5+sum(g*(p/size-.5) for g,p in zip(gradient,pixel))
                    corrected=depth+sum(g*(t-r) for g,t,r in zip(gradient,tap,sample_receiver))
                    old=depth+sum(g*(t-r) for g,t,r in zip(gradient,tap,receiver))
                    plane_error=max(plane_error,abs(stored-corrected))
                    old_plane_error=max(old_plane_error,abs(stored-old))
                    assert abs(stored-corrected)<1e-14
                    shadow_cases+=1
    assert old_plane_error>.0002
    # A sloped plane must reconstruct as that same plane at full and half
    # resolution. This catches pairing one pixel's depth with another's ray.
    plane_position_error=0
    normal_cases=0
    for dimensions in ((1728,1117),(1920,1080)):
        for half_resolution in (False,True):
            target=tuple(n//2 for n in dimensions) if half_resolution else dimensions
            for sign in (-1,1):
                for distance in (8,192,900):
                    normal=(.2,-.3,-sign)
                    sample_uv=depth_uv(((target[0]//2+.5)/target[0],
                                        (target[1]//2+.5)/target[1]),dimensions)
                    points=[]
                    for ox,oy in ((0,0),(1,0),(0,1)):
                        uv=(sample_uv[0]+ox/dimensions[0],sample_uv[1]+oy/dimensions[1])
                        ray=reconstruct(uv,(1277/(1277-.2))*(1-.2),dimensions,
                                        (1.2686,1.9626),sign)
                        dot=sum(n*r for n,r in zip(normal,ray))
                        t=-distance/dot
                        ndc=(1277/(1277-.2))*(1-.2/t)
                        p=reconstruct(uv,ndc,dimensions,(1.2686,1.9626),sign)
                        error=abs(sum(n*c for n,c in zip(normal,p))+distance)
                        plane_position_error=max(plane_position_error,error)
                        assert error<2e-8
                        points.append(p)
                    a=tuple(v-u for u,v in zip(points[0],points[1]))
                    b=tuple(v-u for u,v in zip(points[0],points[2]))
                    cross=(a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
                    cosine=abs(sum(v*n for v,n in zip(cross,normal)))/math.sqrt(
                        sum(v*v for v in cross)*sum(n*n for n in normal))
                    assert cosine>1-1e-10
                    normal_cases+=1
    report={
        'result':'pass', 'game_launched':False, 'gpu_test':False,
        'source_sha256':hashlib.sha256(world_source_bytes(fp.src('world_effects.hlsl'))).hexdigest(),
        'test_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'projection_cases':count,'maximum_projection_error':maximum_projection_error,
        'viewport_depth_range':[0,.94], 'mappings':mappings,
        'shadow_plane_cases':shadow_cases,'corrected_shadow_plane_max_error':plane_error,
        'old_shadow_plane_max_error':old_plane_error,
        'sloped_plane_normal_cases':normal_cases,
        'sloped_plane_position_max_error':plane_position_error,
        'old_full_resolution_x_error_world_units_at_1277':1277/(1728*1.2686),
        'documentation':[
            'https://learn.microsoft.com/en-us/windows/win32/direct3d9/directly-mapping-texels-to-pixels',
            'https://learn.microsoft.com/en-us/windows/win32/direct3d9/nearest-point-sampling'],
        'limitations':['Analytic CPU reference, not GPU execution or screenshot validation.',
                       'Does not model depth quantization, MSAA sample positions, or D9VK subpixel bias.']}
    output=fp.output_dir()/'raster-reconstruction-validation.json'
    output.write_text(json.dumps(report,indent=2)+'\n')
    print(f'PASS {count} projection cases, {shadow_cases} planar shadow cases; {output}')


if __name__=='__main__':
    run()
