#!/usr/bin/env python3
# northlight-test:
"""Check validated crossfade payloads and the current renderer integration contracts."""
import hashlib,json,re,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
from source_units import source_text
fp.use_source_modules()
from compile_shadow_transition_shaders import ENTRIES, source_hashes
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
manifest=json.loads((fp.SHADERS/'shadow-transition-shader-build.json').read_text())
header=fp.src('shadow_transition_compiled_shaders.h').read_text()
assert manifest['sources']==source_hashes()
assert set(manifest['shaders'])==set(ENTRIES)
for entry,record in manifest['shaders'].items():
 data=(fp.COMPILED/(entry+'.bin')).read_bytes();assert sha(fp.COMPILED/(entry+'.bin'))==record['sha256']
 limit=768
 assert record['target']=='ps_3_0' and record['static_instruction_slots']<=limit and record['temporary_registers']<=32
 body=re.search(r'k'+entry+r'Shader\[\] = \{(.*?)\};',header,re.S)[1]
 words=[int(word,16) for word in re.findall(r'0x[0-9a-fA-F]+',body)]
 assert struct.pack('<'+'I'*len(words),*words)==data
print(f'PASS {len(ENTRIES)} fresh compiled/embedded optional extended SM3 variants <=768 slots and 32 temps; baseline shader gates unchanged')
w=source_text(fp.src('world_renderer.h'))+'\n'+fp.src('world_geometry_resources.inl').read_text()
coordination=fp.src('world_shadow_transition.inl').read_text()
h=fp.src('world_shadow_transition.h').read_text()
resolve=fp.src('world_soft_shadows.inl').read_text()
visibility=fp.src('world_directional_visibility.h').read_text()
fog=fp.src('world_atmospheric_fog.inl').read_text()
assert 'void invalidateShadowCache(bool retainPointStatic=false){\n        resetShadowTransitions();' in w
assert 'releaseGeometryTargets(){\n        releaseShadowTransitions();releaseNearEnvironment();' in w
release=w[w.index('void releaseGPU(bool keepMesh=false){'):].split('\n    }\n',1)[0]
assert 'releaseShadowTransitions();releaseNearEnvironment();' in release
assert 'if(!next.shadows)releaseShadowTransitions();' in w
assert 'if(memoryPressure||geometryMemoryDemand.load(std::memory_order_relaxed))releaseShadowTransitions();' in w
assert 'item.history,ShadowMaps' not in w and 'inventory.texture(transition.history,ShadowMaps)' in w and 'inventory.texture(transition.dynamic,ShadowMaps)' in w
assert 'slotDirections[slot]=' in w and 'shadowFrame(cascade?farPivot:cascadePivot,slotDirections[source*2+cascade],radius)' in w
assert 'NorthlightActorShadowMap::matrix(actorCenter[source],pivot,rawSourceDirections[source]' in w
assert 'sliceBand||wasSlicing||slices.active||(reason&&!directionStep)' in w
assert 'localSame&&staticSame' in w and 'staticSignature(oldMatrix)' in w
assert w.index('historyReady=preserveShadowStatic(slot)')<w.index('renderCache(directionalMaps.cache(slot).surface,partialStatic,true)')
commit=w.index('transition.state.commit(');assert w.index('unionShadow(directionalMaps.current(slot).surface')<commit
assert 'if(!environmentReuse){\n            const V selected=' in w
assert 'if(!complete)owner->resetShadowTransitions()' in w
start=w.index('const auto action=NorthlightQuality::cascadeAction(');predicate=w[start:w.index('cascadeActions[slot]=',start)]
assert '!transition.state.fading()' in predicate and 'workingValid' not in predicate
assert 'beginNearReplay(dynamicSurface,historyReady||transition.state.fading())' in w
assert 'dynamicShadow=(historyReady||transition.state.fading())?transition.dynamic:directionalMaps.scratch().texture' in w
assert 'dynamicSurface=(historyReady||transition.state.fading())?transition.dynamicSurface:directionalMaps.scratch().surface' in w
assert 'gpuPass.beginCascade(dynamicSurface,' in w and 'gpuPass.beginNearReplay(dynamicSurface,' in w
assert 'unionShadow(directionalMaps.current(slot).surface,dynamicShadow,' in w
assert 'StretchRect(' not in w[w.index('bool historyReady=false;'):w.index('transitionFrame.complete=true;')]
print('PASS static-only history, success-only exact map/matrix publication, content/reset/pressure invalidation, baseline fallback cadence and current terrain/actor copy')
assert h.index('if(failed_||pressured)')<h.index('if(slots_[slot].history')
assert 'memoryPressure||geometryMemoryDemand.load(std::memory_order_relaxed)' in coordination
assert 'MaxPixelShader30InstructionSlots<required' in h and 'dynamicBudget(8ull<<20)' in coordination
assert 'std::max({kDirectionalShadowTransitionCurrentShaderSlots,kDirectionalShadowTransitionPreviousShaderSlots' in h
assert 'SetTexture(0,crop.white)' in h and 'SetTexture(1,crop.cache)' in h
assert 'screenFallbacks.white(),directionalMaps.cache(slot).texture,shadowPrograms.unionShader()' in coordination
assert 'check(' not in h and 'target(' not in h
assert 'quality.softShadows||anyShadowTransition' in resolve
assert 'if(input.transition){\n                bindTransition(source,false);\n                d->SetPixelShader(input.current);' in visibility
assert 'bindTransition(source,true);\n                d->SetPixelShader(input.previous);' in visibility
assert 'if(anyShadowTransition&&!softResolved)releaseShadowTransitions();' in resolve
borrowed='if(transition)for(unsigned sampler=6;sampler<10;++sampler)'
restore='if(input.transition)for(unsigned sampler=6;sampler<10;++sampler)'
assert fp.src('world_atmosphere_commands.h').read_text().count(borrowed)==1 and visibility.count(restore)==1
assert 'commands.restoreLighting(textures,screenLighting.baseline().texture,anyShadowTransition,c[17]);' in fog
assert visibility.index('drawSources(d,input,bindSource,bindTransition);')<visibility.index(restore)<visibility.index('return resolved;')
assert resolve.index('directionalVisibility.resolve(')<resolve.index('if(!softResolved&&!resolveActorVisibility())')<resolve.index('result.sharedResolved=sharedResolved;')
assert w.index('if(!resolveDirectionalVisibility(')<w.index('if(!renderCelestialLighting(celestialInputs,visibility,')
assert 'sharedResolveRequested=anyActorPass&&quality.softShadows&&!anyShadowTransition' in resolve
for selector in ['shadowTransition.fogShader(fogActorCurrent,fogActorTiledCurrent)','shadowTransition.cloudsShader(fogActorCurrent,fogActorTiledCurrent)','cloudFade=quality.godrays&&geometryReady&&shadowVolumeTransitionActive(firstSource)']:
 assert selector in fog,selector
assert 'if(fading)bindShadowTransition(source)' in fog and 'if(cloudFade)bindShadowTransition(firstSource)' in fog
print('PASS optional cap/budget/failure fallback; two-epoch resolve only during fades; GI/material sampler restore and current actor/tiled/HDR volume consumers')

assert 'SetPixelShaderConstantF(113,currentNear,1)' in h and 'SetPixelShaderConstantF(114,currentFar,1)' in h
