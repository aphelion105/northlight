#!/usr/bin/env python3
# northlight-test:
"""0.3.199 (fog temporal): wiring of the temporal accumulation of the half-resolution fog - source checks, no compiler.
The FogTemporal shader (own entry after FogClouds; every other entry byte-identical), c64.y as its only constant, the FogTemporal key,
the pass and the fogBlurred<->fogHistory rotation guarded by the fog effect, the shader and the history target (no key since the game tests), the composite on the
final buffer, and nothing in the stream or proxy files."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
from scripts.shaders.shader_sources import world_source_text
import json
import hashlib
import re
import subprocess

w=fp.src('world_renderer.h').read_text()
screen=fp.src('world_fog_screen.h').read_text()
moon=fp.src('world_moon_floor_fog.h').read_text()
resources=fp.src('world_screen_resources.inl').read_text()
h=world_source_text(fp.SHADERS/'world_effects.hlsl')
py=(fp.REPO/'scripts'/'shaders'/'compile_world_shaders.py').read_text()
q=fp.src('quality_settings.h').read_text()
manifest=json.loads((fp.SHADERS/'world-shader-build.json').read_text())['shaders']
ft=manifest.get('FogTemporal',{})
names=re.findall(r'\("(\w+)", "[vp]s_3_0"\)',py)
checks={}

checks['build: FogTemporal follows the two cloud variants']=('FogClouds' in names and names[names.index('FogClouds'):names.index('FogClouds')+3]==['FogClouds','FogCloudsActor','FogTemporal'])
checks['manifest: FogTemporal ps_3_0, <= 512 slots, < 32 temporaries']=(ft.get('target')=='ps_3_0' and ft.get('static_instruction_slots',999)<=512 and ft.get('temporary_registers',99)<32)
checks['compiled: FogTemporal.bin and generated kFogTemporalShader']=((fp.COMPILED/'FogTemporal.bin').exists() and 'static const DWORD kFogTemporalShader[]' in fp.src('world_compiled_shaders.h').read_text())
# The temporal pass must not consume bytecode missing from its build manifest.
checks['compiled: WorldFog bytecode matches its manifest']=json.loads((fp.SHADERS/'world-shader-build.json').read_text())['shaders']['WorldFog']['sha256']==hashlib.sha256((fp.COMPILED/'WorldFog.bin').read_bytes()).hexdigest()

body=h[h.index('float4 FogTemporal('):]
body=body[:body.index('\n}\n')]
checks['HLSL: placed after FogBlur and FogClouds']=(h.index('float4 FogBlur(')<h.index('float4 fogClouds(')<h.index('float4 FogTemporal('))
checks['HLSL: c64 is the only new constant, only .y read; s14 history, s15 depth history']=(
    'float4 FogTemporalInfo : register(c64);' in h and 'FogTemporalInfo.y' in body and len(re.findall(r'FogTemporalInfo\.[xzw]',body))==0
    and 'sampler2D FogHistory : register(s14);' in h and 'DepthHistory' in body)
checks['HLSL: no other shader reads c64.y (0.3.200: FogStepInfo, c64.z, only in WorldFog\'s interval)']=(len(re.findall(r'register\(c64\)',h))==3 and re.findall(r'HorizonEdge\.\w+',re.sub(r'//[^\n]*','',h))==['HorizonEdge.w'] and 'LocalLightFog[5].' not in h
    and re.findall(r'FogStepInfo\.\w+',re.sub(r'//[^\n]*','',h))==['FogStepInfo.z','FogStepInfo.z'] and 'mad(FogInfo.w,1.0/48,FogStepInfo.z)' in h[h.index('float4 worldFog('):h.index('float4 FogBlur(')])
checks['HLSL: sky reprojects the direction only, clamp over the neighbourhood, lerp by the weight, passthrough at 0']=(
    'sky' in body and 'PreviousView[0].xyz' in body and 'min(lo,s)' in body and 'clamp(history,lo,hi)' in body and 'lerp(current,' in body
    and 'if(FogTemporalInfo.y<=0)return current;' in body and 'max(.25,w*.03)' in body)

local_lighting=fp.src('world_local_lighting.h').read_text()
lamp=local_lighting[local_lighting.index('bool drawFog('):]
g=fp.src('world_atmospheric_fog.inl').read_text()
actor_binding=g[g.index('    void bindFogActorSource('):g.index('    struct AtmosphericFogInput')]
gpu=fp.src('world_atmosphere_commands.h').read_text()
actor_commands=gpu[gpu.index('    void bindActor('):gpu.index('    void bindProbeOrTiles(')]
checks['host: current lamp/spell fog follows history and both blur passes, without writing fog history']=(
    g.index('fogScreen.resolveHistory(')<g.index('fogScreen.blur(')<g.index('if(effects.fog&&localFogActive&&')
    and w.index('if(!renderAtmosphericFog(')<w.index('const NorthlightWorldComposite::Input compositeInputs')
    and 'const auto& finalFog=fogScreen.final(fogResolved);' in g
    and '!renderLocalFog(w,h,finalFog.surface,localLights,fogRects,c[21],lightingCapture))return false;' in g
    and '"local fog glow combine"' in lamp
    and 'SetRenderTarget(0,currentFogSurface)' in lamp and 'fogScreen.history()' not in lamp
    and 'SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE)' in lamp
    and 'SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE)' in lamp)
local=h[h.index('float4 LocalFog('):h.index('float4 LocalFogCombine(')]
checks['HLSL: lamp rays rotate without adding/subtracting large world positions']=(
    'view.x*InverseView[0].xyz+view.y*InverseView[1].xyz+view.z*InverseView[2].xyz' in local
    and 'endpoint-Camera.xyz' not in local)

checks['setting: no key (always on since the game tests)']=('FogTemporal' not in q and 'fogTemporal' not in q)  # the docs side (no FogTemporal in the ini or README.txt) is test_quality_docs

pas=screen[screen.index('bool resolveHistory('):screen.index('bool blur(')]
checks['host: pass and swap guarded by the setting, the shader, the history target, fog and debug==0']=(
    'effects.fog&&debug==0,fogActive,volumeFog,useHistory,in.clouds,c[53],history.pending().distance.texture' in g
    and 'if(!in.enabled||!temporalShader_||!history_.texture)' in pas and 'if(in.volumeAtlas)' in pas
    and 'std::swap(scratch_,history_)' in pas and 'if(!resolved){invalidateHistory();return false;}' in pas)
checks['host: history weight 0 unless fogHistoryValid&&useHistory; weight constant is c64 only, previous view c53 from the bank']=(
    'historyValid_&&in.useHistory?weight:0.f' in pas and 'weight=in.clouds?.5f:.85f;' in pas
    and 'SetPixelShaderConstantF(64,info,1)' in pas and 'SetPixelShaderConstantF(53,in.previousView,4)' in pas
    and re.findall(r'SetPixelShaderConstantF\((6[4-7]),',w+moon+actor_commands+screen)==['65','65','66','64']
    and 'fogActor?actorFogBounds[source]:wholeActorMap,fogActor' in actor_binding
    and 'SetPixelShaderConstantF(66,bounds,1)' in actor_commands)
checks['host: reads raw fog s9, fog history s14 and previous distance s15; s14/s15 put back']=(
    'SetRenderTarget(0,scratch_.surface)' in pas and 'SetTexture(9,raw_.texture)' in pas
    and 'SetTexture(14,history_.texture)' in pas and 'SetTexture(15,in.previousDistance)' in pas
    and 'SetTexture(14,nullptr)' in pas and 'SetTexture(15,nullptr)' in pas and 'D3DSAMP_MINFILTER,D3DTEXF_POINT' in pas)
# Worst case: world literal marks, 8 per-source names and 10 proxy marks including HDR base/sky.
maxMarks=int(re.search(r'MaxMarks=(\d+)',fp.src('gpu_profile.h').read_text())[1])
profile_units=(w+g+local_lighting+fp.src('world_screen_lighting.inl').read_text()
               +fp.src('world_soft_shadows.inl').read_text()+fp.src('world_directional_pass.inl').read_text())
checks['host: FogTemporal profile mark after the pass (worst case <= MaxMarks)']=(g.index('fogScreen.resolveHistory(')<g.index('if(fogResolved&&profile)profile->mark("FogTemporal")') and
    len(set(re.findall(r'profile->mark\("(\w+)"\)',profile_units)))+8+10<=maxMarks)
bl=screen[screen.index('bool blur('):screen.index('private:')]
checks['host: blur unchanged when the pass did not run (fog -> fogBlurred -> fog), resolved: fogHistory -> fog -> fogBlurred']=(
    'horizontal=resolved?raw_:scratch_' in bl and 'SetRenderTarget(0,horizontal.surface)' in bl
    and 'SetTexture(9,(resolved?history_:raw_).texture)' in bl
    and 'SetRenderTarget(0,final(resolved).surface)' in bl and 'SetTexture(9,horizontal.texture)' in bl
    and 'return resolved?scratch_:raw_' in screen)
profile_sources=profile_units+fp.src('world_light_history.h').read_text()
checks['host: FogBlur closes both quads inside the active-fog guard, before c34/composite; worst case fits 48 marks']=(
    bl.count('quad(d,width,height)')==2
    and bl.index('if(!active)return true;')<bl.index('"volume blur horizontal"')<bl.index('"volume blur vertical"')
    and g.index('fogScreen.blur(')<g.index('if(fogActive&&profile)profile->mark("FogBlur")')<g.index('result={finalFog,')
    and maxMarks==48 and len(set(re.findall(r'profile->mark\("(\w+)"\)',profile_sources)))+10+8<=maxMarks)
products=w[w.index('void captureProducts('):w.index('bool captureSkippedDiagnostic(')]
checks['host: composite and current-frame diagnostic product read the final buffer']=(
    'fogResult.image.texture?fogResult.image.texture:screenFallbacks.ao()' in w
    and 'd->SetTexture(9,in.fogColor);' in fp.src('world_composite.h').read_text()
    and 'diagnosticFogResolved=fogResult.resolved;' in w and 'captureProducts(directory,capture,rendererFrame,w,h,true);' in w
    and 'ready=current&&composited&&lastRenderDebug==0;' in products
    and 'image("fog",fogScreen.final(diagnosticFogResolved).surface,ready&&effects.fog&&(menuDomain||regionalFog.field()),w/2,h/2,D3DFMT_A16B16G16R16F);' in products
    and 'NorthlightWorldDiagnostics::product(' in products)
checks['host: history target created with the others, dropped and invalidated in releaseGPU; shader optional']=(
    'fogScreen.create(pixel,image)' in resources and 'fogScreen.createScratch(image)' in resources
    and 'image(D3DFMT_A16B16G16R16F,history_.texture,history_.surface)' in screen
    and 'releaseScreenColorTargets();' in w and 'fogScreen.reset()' in resources and 'historyValid_=rawNeutral_=historyNeutral_=false;' in screen
    and 'fogScreen.createOptionalHistory(d)' in resources and 'CreatePixelShader(kFogTemporalShader,&temporalShader_)' in screen)
checks['host: no per-draw work (nothing in src/stream or src/proxy mentions it)']=all(
    not re.search(r'fogTemporal|FogTemporal|fogHistory',p.read_text(errors='ignore')) for d in ('stream','proxy') for p in (fp.REPO/'src'/d).rglob('*') if p.is_file())
for k,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+k)
sys.exit(0 if all(checks.values()) else 1)
