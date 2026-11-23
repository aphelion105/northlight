#!/usr/bin/env python3
# northlight-test:
"""Every shipped world shader agrees with its bytecode, header, assembly and device budget."""
from pathlib import Path
import hashlib,json,re,struct,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
from scripts.shaders.shader_sources import source_hashes, world_source_bytes
fp.use_source_modules()
from compile_world_shaders import ENTRIES, NORMAL_ENTRIES, normal_sources
from shader_compiler import bytecode,validate_assembly,DLL_SHA256,COMPATIBLE_DLL_SHA256

manifest=json.loads((fp.SHADERS/'world-shader-build.json').read_text())
assert manifest['normal_sources']==normal_sources()
assert all(manifest['shaders'][entry]['source_sha256']==manifest['normal_sources']['world_normals.hlsl'] for entry in NORMAL_ENTRIES)
header=fp.src('world_compiled_shaders.h').read_text()
assert manifest['source_sha256']==hashlib.sha256(world_source_bytes(fp.SHADERS/'world_effects.hlsl')).hexdigest()
assert manifest['sources']==source_hashes(fp.SHADERS/'world_effects.hlsl')
assert manifest['shaders']['WorldCompositeHdr']['source_sha256']==manifest['source_sha256']
assert 'world_effects_composite.hlsl' in manifest['sources']
assert manifest['bump_include_sha256']==hashlib.sha256((fp.SHADERS/'auto_bump_normal.hlsl').read_bytes()).hexdigest()
assert set(manifest['shaders'])=={entry for entry,_ in ENTRIES}
extended={'ForeverFogActor','ForeverFogActorNoNoise','WorldFogActor','FogCloudsActor',
          'TemporalLightMerged','TemporalLightMergedZeroWeight'}
tiled={'WorldFogActorTiled','FogCloudsActorTiled','WorldMaterialCompositeHdr'}
tiled.update(entry for entry,_ in ENTRIES if entry.startswith(('MoonFloorFog','ForeverFogActorTiled')))

def verify(entry,target,info,data,words):
    assert info['target']==target and info['bytes']==len(data)
    assert info['sha256']==hashlib.sha256(data).hexdigest(),entry
    assert info.get('compiler_sha256',manifest['compiler_sha256']) in (DLL_SHA256,COMPATIBLE_DLL_SHA256)
    assert re.fullmatch('[0-9a-f]{64}',info.get('source_sha256',manifest['source_sha256']))
    compiled=re.search(r'\bk'+entry+r'Shader\[\]\s*=\s*\{([^}]+)\}',header)[1]
    shipped=[int(value,16) for value in re.findall(r'0x[0-9a-fA-F]+',compiled)]
    assert shipped==list(words),entry
    assert struct.pack('<'+'I'*len(words),*words)==data
    stats=validate_assembly(fp.COMPILED/(entry+'.bin.asm'),target,
                            slot_limit=1024 if entry=='UnifiedDirectionalShadow' else 768 if entry in tiled else 600 if entry in extended else 512)
    assert all(info[key]==value for key,value in stats.items()),entry
    if entry in extended|tiled or entry=='UnifiedDirectionalShadow':
        assert f'k{entry}ShaderSlots = {stats["static_instruction_slots"]};' in header

for entry,target in ENTRIES:
    data,words=bytecode(fp.COMPILED/(entry+'.bin'),target)
    info=manifest['shaders'][entry]
    verify(entry,target,info,data,words)
    for wrong_data,wrong_info in ((data[:-4]+bytes(4),info),(data,{**info,'static_instruction_slots':0})):
        try:verify(entry,target,wrong_info,wrong_data,words)
        except AssertionError:pass
        else:raise AssertionError('corrupt bytecode/metadata was accepted: '+entry)
world=fp.src('world_renderer.h').read_text()
for name in ['world_screen_resources.inl','world_geometry_resources.inl','world_directional_visibility.h']:
    world=world.replace('#include "'+name+'"',fp.src(name).read_text())
regional=fp.src('world_regional_fog_gpu.h').read_text()
assert 'caps.MaxPixelShader30InstructionSlots>=std::max(kWorldFogActorShaderSlots,kFogCloudsActorShaderSlots)' in regional
assert 'regionalFogGpu.createOptional(d);' in world
assert 'caps.MaxPixelShader30InstructionSlots>=kUnifiedDirectionalShadowShaderSlots' in world
# This imported-atmosphere variant uses the existing optional extended budget;
# it must remain capability-gated rather than raising the baseline SM3 limit.
imported=fp.src('world_forever_fog_shaders.h').read_text()
if 'ForeverFogActor' in manifest['shaders']:
    assert 'caps.MaxPixelShader30InstructionSlots>=kForeverFogActorShaderSlots' in imported
    assert 'd->CreatePixelShader(kForeverFogActorShader,&actor_)' in imported
fog=fp.src('world_atmospheric_fog.inl').read_text()
assert 'foreverFog.reset();' in world
for entry in ('ForeverFogActorTiled','ForeverFogActorTiledNoNoise'):
    assert 'caps.MaxPixelShader30InstructionSlots>=k'+entry+'ShaderSlots' in imported
    assert 'd->CreatePixelShader(k'+entry+'Shader,' in imported
assert 'foreverFog.select(fogActorCurrent,fogActorTiledCurrent,fading,noise)' in fog
actor=fp.src('world_actor_shadows.h').read_text()
assert 'caps.MaxPixelShader30InstructionSlots>=std::max(kWorldFogActorTiledShaderSlots,kFogCloudsActorTiledShaderSlots)' in actor
assert world.count('d->GetDeviceCaps(&caps);prepareActorShaders(caps);')==2
moon=fp.src('world_moon_floor_fog.h').read_text()
assert 'caps.MaxPixelShader30InstructionSlots<slots[index]||caps.PS20Caps.NumTemps<temps[index]' in moon
assert all(f'k{entry}ShaderSlots' in moon for entry,_ in ENTRIES if entry.startswith('MoonFloorFog'))
print(f'PASS {len(ENTRIES)} shader binaries/header arrays, assembly metrics, capabilities; corrupt data/slot controls rejected')
