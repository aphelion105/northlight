#!/usr/bin/env python3
# northlight-test: requires=cxx
"""0.3.208 (task 29) actor shadow map: native model test of actor_shadow_map.h (clang++, plain and ASan/UBSan) and a wiring
audit of world_renderer.h / water_renderer.h / quality_settings.h (static source analysis): the actor map is drawn in the near
map's iteration (cascade 0, Render only) and reused with it, only cascade 0 skips replays inside the inset, ActorShadowDetail=0
(or ActorShadows=0) draws nothing and leaves the visibility white, s12 carries the actor map for the ActorShadow passes and the
visibility for the WorldLighting loop only, the water gets the maps. No game or GPU."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import re,subprocess,tempfile
HERE=Path(__file__).resolve().parent
w=fp.src('world_renderer.h').read_text();q=fp.src('quality_settings.h').read_text();wr=fp.src('water_renderer.h').read_text()
h=fp.src('actor_shadow_map.h').read_text()
render=w[w.index('    bool render(IDirect3DSurface9* targetSurface'):]
checks={}
checks['constants: Radius 12, Inset .80, Size 1024; unsnapped centre (XY from the leashed centre, depth from the pivot); matrix rows from shadowMatrixFrom']=('inline constexpr float Radius=12.f;' in h and 'inline constexpr float Inset=.80f;' in h and 'inline constexpr unsigned Size=1024;' in h
    and 'f.cx=dot(center,f.x);f.cy=dot(center,f.y);' in h and 'NorthlightWorldMath::shadowMatrixFrom(frame(center,pivot,sun),Radius,m);' in h
    and 'if(!(distance>Leash))return center;' in h)
checks['quality: ActorShadowDetail last key 0..1, presets 1/1/0, actorShadowMap needs ActorShadows']=('{"ActorShadowDetail",&Settings::actorShadowDetail,0,1,{1,1,0}}' in q and q.rstrip().count('ActorShadowDetail')>=1
    and 'inline bool actorShadowMap(const Settings& s,bool shadows){return actorShadowWork(s,shadows)&&s.actorShadowDetail;}' in q and 'unsigned actorShadowDetail=1;' in q)
# Frame matrices from the raw pivot, before the cascade anchor (the anchor test keeps every later `pivot` out).
raw=render.index('NorthlightActorShadowMap::leash(actorCenter[source],actorCenterValid[source],pivot,sourceDirections[source])')
checks['matrix from the raw pivot, before the anchored cascade pivot; off = nothing computed']=(raw<render.index('cascadeAnchor.update(')
    and 'const bool actorMapOn=NorthlightQuality::actorShadowMap(quality,effects.shadows)&&actorShadowPS&&actorShadow[0]&&actorShadow[1];' in render
    and 'NorthlightActorShadowMap::matrix(actorCenter[source],pivot,sourceDirections[source],actorFrameMatrix[source]);' in render
    and 'if(anchorNewMap)actorCenterValid[source]=false;' in render and render.index('if(anchorNewMap)actorCenterValid[source]=false;')<raw
    and 'actorCenterValid[s]=false;' in w and 'actorCenterValid[0]=actorCenterValid[1]=false;' in w and w.count('actorCenterValid[0]=actorCenterValid[1]=false;')==1) # releaseGPU (reset() calls it)
checks['leash: 1.5 yd, 40 yd jump re-centre; diagnostic dump s%d-actor; water actor flag under the cascades\' direction check']=('inline constexpr float Leash=1.5f;' in h and 'inline constexpr float JumpDistance=40.f;' in h
    and 'std::snprintf(name,sizeof name,"s%d-actor",source);NorthlightWorldDiagnostics::dump(d,actorShadowSurface[source]' in w and '&&c[28][source]>0.f;' in wr)
checks['a source without a near map keeps no actor map']='if(!actorMapOn||!sourceActive[source])actorValid[source]=false;' in render
# Pairing: invalid when the near map is redrawn, valid only after the actor draw; a reused/deferred near map continues before any of it.
loop=render[render.index('        for(int source=0;source<2;++source){\n          if(!effects.shadows||!sourceActive[source])continue;'):]
reuse=loop.index('if(action!=NorthlightQuality::CascadeAction::Render){')
begin=loop.index('reuse.begin(interval);')
invalid=loop.index('if(cascade==0)actorValid[source]=false;')
draw=loop.index('if(cascade==0&&actorMapOn){')
valid=loop.index('memcpy(actorMatrices[source],am,64);actorValid[source]=true;')
union=loop.index('if(!check(quad(1024,1024),"shadow union"))return false;')
checks['paired with the near map: reuse/defer continues before it, Render invalidates, the actor draw precedes the union and commit']=(
    reuse<begin<invalid<draw<valid<union and loop.index('continue;',reuse)<begin and 'actorMatrices' not in loop[reuse:begin]
    and 'reuse.commit(interval,shadowPasses,matrices[cascade])' in loop and valid<loop.index('reuse.commit(interval,shadowPasses,matrices[cascade])'))
actor=loop[draw:loop.index('} /* actorShadows */')]
checks['actor draw: own target, shared 1024 depth, cleared white/1, cascade replay machinery, actor matrix, exactly the near-skipped replays']=(
    'actorShadowSurface[source]' in actor and 'd->SetDepthStencilSurface(shadowDepth);' in actor and '0xffffffff,1,0' in actor
    and 'replayProjection(context.inverseView,am,actorRows)' in actor and 'submitReplay(p.get(),actorBindings,actorPoses,actorRows,' in actor
    and 'if(nearSkip)for(const uint32_t index:actorReplays){const auto& p=replays[index];' in actor and 'for(size_t index=0;index<replays.size();++index)' not in actor and 'staticProofMask' not in actor and 'actorFrameMatrix[source]' in actor and 'const float* am=actorFrameMatrix[source];' in actor)
# The near skip: cascade 0 only, valid bounds only (insideInset rejects invalid bounds), actor matrix.
checks['near skip: cascade 0 only, never the far map, by the inset test on valid bounds']=(
    'bool nearSkip=cascade==0&&actorMapOn;' in loop and 'if(nearSkip){actorReplays.clear();try{actorReplays.reserve(replays.size());}catch(...){nearSkip=false;}}' in loop
    and 'if(nearSkip&&NorthlightActorShadowMap::insideInset(p->pointBounds.valid,p->pointBounds.low,p->pointBounds.high,actorFrameMatrix[source])){actorReplays.push_back(uint32_t(index));++nearSkipped;continue;}' in loop
    and loop.count('insideInset(')==1 and 'if(!valid||!low||!high||!m)return false;' in h)
# ActorShadowDetail=0 / ActorShadows=0: the pass is never drawn; the visibility is still cleared white every frame.
light=render[render.index('actor visibility (half res, r sun, g moon)'):render.index('if(profile)profile->mark("WorldLighting");')]
checks['visibility cleared white every frame; the pass only for a valid actor map of a drawn, active source']=(
    light.index('0xffffffff,1,0),"clear actor visibility"')<light.index('actorShadowPS')
    and '||!effects.shadows||!sourceActive[source]||!actorValid[source]||!actorShadow[source]||!actorShadowPS)continue;' in light)
checks['ActorShadow pass: c68..c71 matrix, c72 info, actor map on s12, RED sun / GREEN moon, mask restored']=(
    'SetPixelShaderConstantF(68,actorMatrices[source],4)' in light and 'SetPixelShaderConstantF(72,actorInfo,1)' in light and 'const float actorInfo[4]={1.f,1.f/float(NorthlightActorShadowMap::Size),NorthlightActorShadowMap::Radius,0.f};' in light
    and 'd->SetTexture(12,actorShadow[source]);' in light and 'source?D3DCOLORWRITEENABLE_GREEN:D3DCOLORWRITEENABLE_RED' in light and 'D3DRS_COLORWRITEENABLE,15' in light)
checks['s12: visibility for the WorldLighting loop only, then the frame-start (null) binding; later readers rebind baselineLight']=(
    light.index('d->SetTexture(12,actorVisibility);')<light.index('d->SetPixelShader(lightingPS);')
    and light.rstrip().endswith('d->SetTexture(12,textures[12]); /* 0.3.208 (task 29): the frame-start binding (null) again; the visibility was WorldLighting\'s only */')
    and 'textures[]={foldScene?foldScene:color,depth,shadow[0],shadow[1],probe[0],probe[1],probe[2],probe[3],nullptr,nullptr,probe[4],waterMask,nullptr,' in render
    and render.count('d->SetTexture(12,baselineLight)')==2)
checks['resources: maps only with the shader (ActorShadowDetail=1), the visibility always; released with the rest; the near reuse invalidation clears validity']=(
    'if(actorShadowPS)for(int i=0;i<2;++i)if(!target(NorthlightActorShadowMap::Size,NorthlightActorShadowMap::Size,D3DFMT_R32F,&actorShadow[i],&actorShadowSurface[i]))return false;' in w
    and 'if(!target(w/2,h/2,D3DFMT_A8R8G8B8,&actorVisibility,&actorVisibilitySurface))return false;' in w
    and 'if(quality.actorShadows&&quality.actorShadowDetail&&FAILED(d->CreatePixelShader(kActorShadowShader,&actorShadowPS)))' in w
    and 'for(auto& t:actorShadow)drop(t);for(auto& t:actorShadowSurface)drop(t);drop(actorVisibility);drop(actorVisibilitySurface);drop(actorShadowPS);' in w
    and 'nearShadow[s].invalidate();actorValid[s]=false;' in w)
checks['water: maps on s8/s9, ActorMatrix c29..c36, ActorShadowInfo c37, filled from the stored matrices']=(
    'IDirect3DTexture9* actorShadows[2]={};' in wr and 'float actorShadowMatrices[2][16]={};' in wr and 'bool actorShadowValid[2]={};' in wr
    and 'float c[38][4]={}' in wr and 'memcpy(c[29+source*4],context.actorShadowMatrices[source],64)' in wr and 'c[37][2]=1.f/1024;' in wr and 'SetPixelShaderConstantF(0,c[0],38)' in wr
    and 'context.sourceShadows[1][1],context.actorShadows[0],context.actorShadows[1]};' in wr and 'for(unsigned i=0;i<10;++i){d->SetTexture(i,textures[i]);' in wr
    and 'out.actorShadows[source]=actorShadow[source];memcpy(out.actorShadowMatrices[source],actorMatrices[source],64);out.actorShadowValid[source]=true;' in w)
for name,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+name)
assert all(checks.values())
with tempfile.TemporaryDirectory(prefix='northlight-actormap-') as tmp:
    for label,flags in [('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined'])]:
        exe=Path(tmp)/('test-'+label)
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',*flags,*fp.test_include_flags(),str(HERE/'test_actor_shadow_map.cpp'),str(fp.src('world_gi.cpp')),'-o',str(exe)],check=True)
        print(label,subprocess.check_output([str(exe)],text=True),end='',flush=True)
print('PASS actor shadow map: model and wiring')
