#!/usr/bin/env python3
"""Check the dead fog source-visibility output and retained state contract."""
import re
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from scripts.shaders.shader_sources import world_source_text
root = Path(__file__).resolve().parents[1]
host = (root / 'src/world/world_renderer.h').read_text()
shader = world_source_text(root / 'shaders/world_effects.hlsl')
assert 'sourceVis' not in host and 'kSourceVisibilityPSShader' not in host
assert len(re.findall(r'\bSourceVisibility\b', shader)) == 1
assert len(re.findall(r'\bSourceInfo\.[xy]\b', shader)) == 2
pass_start = shader.index('float4 SourceVisibilityPS(')
pass_end = min(i for marker in ('float4 WorldFog(', 'float4 worldFog(') if (i := shader.find(marker, pass_start)) >= 0)
assert all(x in shader[pass_start:pass_end] for x in ('SourceInfo.x', 'SourceInfo.y', 'PreviousSourceVisibility'))
fog = (root / 'src/world/world_atmospheric_fog.inl').read_text()
begin = fog.index('bool drawAtmosphericFog(')
raw_target = fog.index('commands.begin(fogScreen.raw().surface,regionalFogGpu.base(),fogAmbient);', begin)
gpu = (root / 'src/world/world_atmosphere_commands.h').read_text()
section = gpu[gpu.index('void begin('):gpu.index('d->SetRenderTarget(0,target);')]
for state in ('D3DSAMP_ADDRESSU','D3DSAMP_ADDRESSV','D3DSAMP_MINFILTER','D3DSAMP_MAGFILTER','D3DSAMP_MIPFILTER'):
    assert 'SetSamplerState(15,' + state in section
assert host.index('if(!renderLocalLighting(') < host.index('if(!renderAtmosphericFog(')
local = (root / 'src/world/world_local_lighting.h').read_text()
before_fog = local[local.index('bool drawSurface('):local.index('bool drawDirect(')] + fog[:raw_target]
for state, value in (('D3DRS_ALPHABLENDENABLE', 'FALSE'), ('D3DRS_COLORWRITEENABLE', '15')):
    writes = re.findall(r'SetRenderState\(' + state + r',([^)]*)\)', before_fog)
    assert writes and writes[-1].strip() == value
assert 'c[67][2]=haze.sun[0];c[67][3]=haze.sun[1]' in (root / 'src/world/world_frame_constants.inl').read_text()
assert (root / 'src/sky/celestial_disc_renderer.h').exists()
print('source-visibility dataflow and state contract PASS')
