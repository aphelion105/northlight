#!/usr/bin/env python3
# northlight-test:
"""0.3.171 bilinear lighting history (F1, fallback form) and the tighter A1 still gate (F2).

TemporalLight read the previous frame's lighting at the NEAREST history texel. While the camera
moves, a static shadow edge then lags or leads by up to .75 half-res px, with the sign flipping
where the fractional flow crosses .5: bands of opposite shift that move with speed and direction
("waves"). 0.3.171 reads LightHistory (s14) bilinearly at the unrounded reprojection (s14 LINEAR
in the temporal pass only). A per-tap weighted depth test did not fit the slot budget (515 > 500),
so the depth test stays at the nearest texel as before (the decisions' F1-HW fallback), and the
bilinear read is used only where all four footprint depths agree with this surface; at a
silhouette the nearest texel's centre is read (LINEAR returns it unmixed), exactly as 0.3.170.

A 1D emulation of the temporal pass (current, cross box, .75 blend) on static edges, and a
source audit. Reads files only; no Wine, GPU or game. Writes temporal-history-validation.json.
"""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
from scripts.shaders.shader_sources import world_source_text
import json
import math
import re
import struct

hlsl = world_source_text(fp.src('world_effects.hlsl'))
w = fp.src('world_renderer.h').read_text()
temporal = hlsl[hlsl.index('TemporalOutput temporalLight('):hlsl.index('// Keep current normals/distance')]
code = re.sub(r'//[^\n]*', '', temporal)
checks = {}

# ---- 1. source pins ----
NEAREST_TEST = ['    float2 pq=(clamp(floor(puv*half),0,half-1)+.5)/half;',
                '    float hz=tex2Dlod(historyDepth,float4(pq,0,0)).r;',
                '    if(abs(hz-w)>max(.25,w*.03))return o;']
FOOTPRINT = ['    float tol=max(.25,w*.03);',
             '    float2 texel=1/half,corner=(floor(puv*half-.5)+.5)*texel; // CLAMP addressing repeats the border',
             '    float4 footprint=float4(tex2Dlod(historyDepth,float4(corner,0,0)).r,',
             '        tex2Dlod(historyDepth,float4(corner+float2(texel.x,0),0,0)).r,',
             '        tex2Dlod(historyDepth,float4(corner+float2(0,texel.y),0,0)).r,',
             '        tex2Dlod(historyDepth,float4(corner+texel,0,0)).r);',
             '    bool agree=all(abs(footprint-w)<=tol);']
HISTORY = '    float4 history=tex2Dlod(historyLight,float4(agree?puv:pq,0,0));'
A1 = ['    float4 tight=clamp(history,lo,hi);',
      '    float still=saturate(2-4*length(puv*half-(base+.5)));',
      '    float margin=.15/max(RemovalInfo.z,1e-4)*RemovalInfo.y*still;',
      '    float loose=clamp(history.a,lo.a-margin,hi.a+margin);',
      '    float3 reach=abs(loose-tight.a)*RemovalInfo.z*TemporalReach.w;',
      '    float4 retained=float4(clamp(history.rgb,lo.rgb-reach,hi.rgb+reach),loose);']
checks['TemporalLight: nearest depth test unchanged; history read once, at puv only when the footprint depths agree'] = (
    all(temporal.count(l + '\n') == 1 for l in NEAREST_TEST + FOOTPRINT + [HISTORY])
    and temporal.index(NEAREST_TEST[-1]) < temporal.index(FOOTPRINT[0]) < temporal.index(HISTORY)
    and code.count('tex2Dlod(historyLight,') == 1 and code.count('tex2Dlod(historyDepth,') == 5 and 'pq,0,0)).r' in code
    and 'return temporalLight(uv,false,DepthHistory,LightHistory);' in hlsl)
checks['A1 lines unchanged except the F2 gate (full to .25 px, none from .5)'] = (
    all(temporal.count(l + '\n') == 1 for l in A1) and 'saturate(2-length(' not in temporal)

# ---- 2. sampler states: s14 LINEAR only in the temporal pass, s15 stays POINT ----
host = fp.src('world_light_history.h').read_text()
block = host[host.index('bool resolveLighting('):host.index('bool resolveDistance(')]
normals = fp.src('world_normals_pass.h').read_text()
normal_sampler = ('d->SetTexture(14,texture_);', 'd->SetSamplerState(14,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);',
                  'd->SetSamplerState(14,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);',
                  'd->SetSamplerState(14,D3DSAMP_MINFILTER,D3DTEXF_POINT);',
                  'd->SetSamplerState(14,D3DSAMP_MAGFILTER,D3DTEXF_POINT);')
samplers = host[host.index('static void bindHistorySampler('):host.index('void resetMerged(')]
checks['light history is bilinear and distance stays POINT in both temporal paths'] = (
    all(normals.count(site)==1 for site in normal_sampler)
    and sorted(normal_sampler,key=normals.index)==list(normal_sampler)
    and w.index('if(!worldNormals.draw(') < w.index('history.resolveLighting(')
    and all(site in block for site in ('bindHistorySampler(d,14,D3DTEXF_LINEAR);',
        'if(!removalDrawn)bindHistorySampler(d,15,D3DTEXF_POINT);',
        'bindHistorySampler(d,9,D3DTEXF_LINEAR);', 'bindHistorySampler(d,13,D3DTEXF_POINT);'))
    and block.index('if(removalDrawn){') < block.index('bindHistorySampler(d,15,D3DTEXF_POINT);')
    and all(f'SetSamplerState(slot,D3DSAMP_{state},{value});' in samplers for state,value in (
        ('ADDRESSU','D3DTADDRESS_CLAMP'),('ADDRESSV','D3DTADDRESS_CLAMP'),('MINFILTER','filter'),
        ('MAGFILTER','filter'),('MIPFILTER','D3DTEXF_NONE'),('SRGBTEXTURE','FALSE'))))
keys = {'BilinearHistory', 'TemporalHistory', 'HistoryFilter'}
config = [fp.src('quality_settings.h').read_text(), fp.src('windows-package/northlight-quality.ini').read_text(),
          fp.src('windows-package/README.txt').read_text(encoding='utf-8')]
checks['no new key in quality_settings.h, the ini or README'] = not any(k in text for k in keys for text in config)


# ---- 3. 1D emulation: a static edge under a camera moving m half-res px per frame ----
def ramp(x, edge, penumbra):
    return min(max((x - edge) / penumbra + .5, 0), 1)


def history_at(H, p, mode):
    """p: previous-frame screen position in half-res px (texel centres at k+.5)."""
    n = len(H)
    if mode == 'nearest':
        return H[min(max(math.floor(p), 0), n - 1)]
    hp = p - .5
    hb = math.floor(hp)
    hf = hp - hb
    a, b = H[min(max(hb, 0), n - 1)], H[min(max(hb + 1, 0), n - 1)]  # CLAMP addressing
    return a + (b - a) * hf


def crossing(v):
    for i in range(len(v) - 1):
        if v[i] < .5 <= v[i + 1]:
            return i + .5 + (.5 - v[i]) / (v[i + 1] - v[i])
    raise AssertionError('no edge')


def edge_offset(m, penumbra, mode, n=64, frames=300):
    """Mean steady-state offset (half-res px) of the stabilised edge from this frame's edge."""
    edge = n / 2 + (frames - 30) * m  # world position; mid-screen during the measured frames
    H, offsets = None, []
    for frame in range(frames):
        current = [ramp(i + .5 + frame * m, edge, penumbra) for i in range(n)]
        if H is None:
            H = current
            continue
        out = []
        for i in range(n):
            h = history_at(H, i + .5 + m, mode)
            box = current[max(i - 1, 0):i + 2]
            out.append(current[i] + .75 * (min(max(h, min(box)), max(box)) - current[i]))
        H = out
        if frame >= frames - 60:
            offsets.append(crossing(out) - crossing(current))
    return sum(offsets) / len(offsets)


table = {}
for penumbra in (2, 4, 8):
    table[penumbra] = {mode: [edge_offset(k / 10, penumbra, mode) for k in range(1, 10)] for mode in ('nearest', 'bilinear')}
bilinear_max = max(abs(x) for p in table.values() for x in p['bilinear'])
nearest_max = max(abs(x) for p in table.values() for x in p['nearest'])
flips = all(min(p['nearest']) < -.5 and max(p['nearest']) > .5 for p in table.values())
checks[f'moving camera: bilinear edge offset <= .05 px (max {bilinear_max:.3f}); nearest drifted {nearest_max:.2f} px with a sign flip'] = (
    bilinear_max <= .05 and nearest_max >= .5 and flips)

# still camera: the reprojection lands on a texel centre (hf 0) or a hair below the next one
H = [ramp(i + .5, 20.3, 4) for i in range(40)]
still = max(abs(history_at(H, i + .5 + d, 'bilinear') - history_at(H, i + .5 + d, 'nearest'))
            for i in range(40) for d in (0, 1e-6, -1e-6))
checks[f'still camera: bilinear equals the nearest texel within 1e-4 ({still:.1e})'] = still <= 1e-4

# silhouette: the footprint depth agreement picks the bilinear read on one surface and the
# nearest texel's centre (0.3.170's value, no mix across the depth edge) at a silhouette.
def history_2d(H, D, px, py, w):
    """TemporalLight's history read at previous-frame position (px, py) in half-res px."""
    ny, nx = len(H), len(H[0])
    at = lambda g, x, y: g[min(max(y, 0), ny - 1)][min(max(x, 0), nx - 1)]  # CLAMP addressing
    hx, hy = math.floor(px - .5), math.floor(py - .5)
    tol = max(.25, w * .03)
    agree = all(abs(at(D, hx + bx, hy + by) - w) <= tol for bx in (0, 1) for by in (0, 1))
    if not agree:
        return at(H, math.floor(px), math.floor(py))
    fx, fy = px - .5 - hx, py - .5 - hy
    top = at(H, hx, hy) + (at(H, hx + 1, hy) - at(H, hx, hy)) * fx
    bottom = at(H, hx, hy + 1) + (at(H, hx + 1, hy + 1) - at(H, hx, hy + 1)) * fx
    return top + (bottom - top) * fy


ground, player = 20., 12.
H = [[(x * 7 + y * 3) % 5 / 4 for x in range(8)] for y in range(8)]
flat = [[ground] * 8 for _ in range(8)]
edge = [[player if x >= 4 else ground for x in range(8)] for y in range(8)]
silhouette_ok = flat_ok = True
for px in (2.6, 3.3, 3.5, 3.9, 4.2, 4.6):
    for py in (2.5, 3.1, 3.8):
        nearest = H[math.floor(py)][math.floor(px)]
        hx, hy = math.floor(px - .5), math.floor(py - .5)
        bil = history_2d(H, flat, px, py, ground)
        fx, fy = px - .5 - hx, py - .5 - hy
        expected = (H[hy][hx] * (1 - fx) + H[hy][hx + 1] * fx) * (1 - fy) + (H[hy + 1][hx] * (1 - fx) + H[hy + 1][hx + 1] * fx) * fy
        flat_ok &= abs(bil - expected) < 1e-12
        straddles = hx < 4 <= hx + 1
        w = ground if math.floor(px) < 4 else player
        got = history_2d(H, edge, px, py, w)
        silhouette_ok &= (got == nearest) if straddles else abs(got - history_2d(H, flat, px, py, ground)) < 1e-12
checks['flat surface: bilinear history; footprint across a silhouette: exactly the nearest texel'] = flat_ok and silhouette_ok

# depth rejection: exactly today's nearest-texel rule, current only when it fails
def reject(hz, w):
    return abs(hz - w) > max(.25, w * .03)
checks['depth rejection unchanged: |hz-w| > max(.25, 3% w) returns current only'] = (
    not reject(10.2, 10) and reject(10.4, 10) and not reject(100 + 2.9, 100) and reject(100 + 3.1, 100))

def half_value(bits):
    return struct.unpack('<e', struct.pack('<H', bits))[0]


def half_round(value):
    return struct.unpack('<e', struct.pack('<e', value))[0]


def store_history(current, previous, stabilize):
    delta = previous - current
    band = max(abs(current), 2**-14) * (3 / 1024)
    return half_round(current + delta * (0 if stabilize and abs(delta) <= band else .75))


checks['FP16 history converges within the rounding band and preserves larger blends'] = (
    'float4 roundingBand=max(abs(current),0.00006103515625)*(3.0/1024.0);' in temporal and
    'float4 keep=1-step(abs(delta),roundingBand);' in temporal and
    'o.light=current+delta*(historyWeight*keep);' in temporal and
    all(store_history(half_value(bits), half_value(bits + 1), False) == half_value(bits + 1)
        for bits in (0x0208, 0x0408, 0x3808, 0xbc08, 0x4480)) and
    all(store_history(half_value(bits), half_value(bits + offset), True) == half_value(bits)
        for bits in (0x0208, 0x0408, 0x3808, 0xbc08, 0x4480) for offset in (-3, -2, -1, 1, 2, 3)) and
    all(store_history(current, previous, True) == store_history(current, previous, False)
        for current, previous in ((.5, .25), (.25, .5), (-.5, -.2), (-.5, .2), (8, 12))))

for name, ok in checks.items():
    print(('PASS ' if ok else 'FAIL ') + name)
assert all(checks.values())
out = fp.output_dir()
out.mkdir(parents=True, exist_ok=True)
(out / 'temporal-history-validation.json').write_text(json.dumps({
    'scope': 'Python 1D emulation of TemporalLight history resampling + source audit; no game, Wine or GPU.',
    'checks': checks, 'edge_offset_half_res_px': {str(p): v for p, v in table.items()}}, indent=2) + '\n')
print('PASS temporal history: bilinear history removes the moving-camera edge drift; silhouettes, still camera and rejection as 0.3.170')
