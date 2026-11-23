#!/usr/bin/env python3
# northlight-test: requires=zig
"""0.3.209 (task 29) actor shadow map: native model test of actor_shadow_map.h (clang++, plain and ASan/UBSan) and a wiring
audit of world_renderer.h / quality_settings.h (static source analysis): the actor map is drawn in the near
map's iteration (cascade 0), including retained-environment frames, only cascade 0 skips replays inside the inset, ActorShadowDetail=0
(or ActorShadows=0) draws nothing and WorldLighting reads a neutral white, s12 carries the actor map for the ActorShadow passes and the
visibility for the WorldLighting loop only (0.3.214: the water appearance pass that also received the maps is gone). No game or GPU."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
from source_units import source_text, world_render_path
import os,re,subprocess
HERE=Path(__file__).resolve().parent
w=source_text(fp.src('world_renderer.h'));q=fp.src('quality_settings.h').read_text()
geometry=fp.src('world_geometry_resources.inl').read_text()
screen=fp.src('world_screen_resources.inl').read_text()
h=fp.src('actor_shadow_map.h').read_text()
owner=fp.src('world_actor_shadows.h').read_text()
actor=fp.src('world_directional_actor_draw.inl').read_text()
render=world_render_path(w)
checks={}
checks['constants: Radius 12, Inset .80, Size 1024; unsnapped centre (XY from the leashed centre, depth from the pivot); matrix rows from shadowMatrixFrom']=('inline constexpr float Radius=12.f;' in h and 'inline constexpr float Inset=.80f;' in h and 'inline constexpr unsigned Size=1024;' in h
    and 'f.cx=dot(center,f.x);f.cy=dot(center,f.y);' in h and 'NorthlightWorldMath::shadowMatrixFrom(frame(center,pivot,sun),Radius,m);' in h
    and 'if(!(distance>Leash))return std::fabs(dz)>Leash?center+f.z*dz:center;' in h and 'if(!(distance<=JumpDistance))return pivot;' in h
    and 'dot(delta,delta)>JumpDistance' not in h) # the jump test is light-space XY (a 3D one re-centred while flying under a high sun)
# Own Keys entry only; its position in Keys[] is test_quality_docs.py's business.
checks['quality: ActorShadowDetail 0..1, presets 1/1/0, actorShadowMap needs ActorShadows']=(re.search(r'\{\s*"ActorShadowDetail"\s*,\s*&Settings::actorShadowDetail\s*,\s*0\s*,\s*1\s*,\s*\{\s*1\s*,\s*1\s*,\s*0\s*\}\s*\}',q) is not None
    and 'inline bool actorShadowMap(const Settings& s,bool shadows){return s.shadowProvider&&actorShadowWork(s,shadows)&&s.actorShadowDetail;}' in q and 'unsigned actorShadowDetail=1;' in q)
# Frame matrices from the raw pivot, before the cascade anchor (the anchor test keeps every later `pivot` out).
raw=render.index('NorthlightActorShadowMap::leash(actorCenter[source],actorCenterValid[source],pivot,rawSourceDirections[source])')
checks['matrix from the raw pivot, before the anchored cascade pivot; off = nothing computed']=(raw<render.index('cascadeAnchor.update(')
    and 'const bool actorMapOn=NorthlightQuality::actorShadowMap(quality,directionalShadows)&&actorMaps.ready(0)&&actorMaps.ready(1);' in render
    and 'actorFrameOn[source]=NorthlightActorShadowMap::finiteMatrix(actorFrameMatrix[source]);if(!actorFrameOn[source])actorCenterValid[source]=false;' in render
    and 'NorthlightActorShadowMap::matrix(actorCenter[source],pivot,rawSourceDirections[source],actorFrameMatrix[source]);' in render
    and 'if(anchorNewMap)actorCenterValid[source]=false;' in render and render.index('if(anchorNewMap)actorCenterValid[source]=false;')<raw
    and 'actorCenterValid[s]=false;' in w and 'releaseShadowMapsOnReset();' in w
    and (w+geometry).count('actorCenterValid[0]=actorCenterValid[1]=false;')==1)
checks['leash: 1.5 yd, 40 yd jump re-centre; diagnostic dump s%d-actor']=('inline constexpr float Leash=1.5f;' in h and 'inline constexpr float JumpDistance=40.f;' in h
    and 'std::snprintf(name,sizeof name,"s%d-actor",source);NorthlightWorldDiagnostics::dump(d,actorMaps.depth(source).surface' in actor)
checks['a source without a near map keeps no actor map']='if(!actorMapOn||!sourceActive[source]){actorValid[source]=false;actorHold[source].invalidate();}' in render
# Pairing: invalid when the near map is redrawn, valid only after the actor draw; a reused/deferred near map continues before any of it.
loop=render[render.index('        for(int source=0;source<2;++source){\n          if(!directionalShadows||!sourceActive[source])continue;'):]
reuse=loop.index('if(action!=NorthlightQuality::CascadeAction::Render){')
begin=loop.index('reuse.begin(interval);')
invalid=loop.index('if(cascade==0)actorValid[source]=nearActorSkip[source]=false;')
draw=loop.index('if(cascade==0&&actorFrameOn[source]){')
submit=loop.index('if(!drawDirectionalActors(gpuPass,source,context.inverseView,actorFrameMatrix[source],nearSkip,sideDiag,')
union=loop.index('if(!unionShadow(directionalMaps.current(slot).surface,dynamicShadow,directionalMaps.nearEnvironment(source).texture,0,0,0,1024))return false;')
checks['actor map is current: only unsplit reuse continues; actor draw precedes final union and environment commit']=(
    reuse<begin<invalid<draw<submit<union and loop.index('continue;',reuse)<begin and 'if(!splitNear){' in loop[reuse:begin] and 'if(!environmentReuse)reuse.begin(interval);' in loop
    and actor.index('runDirectionalReplays(')<actor.index('memcpy(actorMatrices[source],am,64);actorValid[source]=true;')
    and submit<loop.index('reuse.commit(interval,shadowPasses,matrices[cascade])'))
checks['transition dynamic target retains baseline scratch and current poses without extra actor replay']=('dynamicShadow=(historyReady||transition.state.fading())?transition.dynamic:directionalMaps.scratch().texture' in loop and 'dynamicSurface=(historyReady||transition.state.fading())?transition.dynamicSurface:directionalMaps.scratch().surface' in loop and 'beginNearReplay(dynamicSurface,historyReady||transition.state.fading())' in loop)
checks['actor draw: own target, shared 1024 depth, cleared white/1, cascade replay machinery, actor matrix, exactly the near-skipped replays']=(
    'gpuPass.beginActor(actorMaps.depth(source).surface)' in actor and '},directionalMaps.depth());' in render
    and '0xffffffff,1,0' in fp.src('directional_gpu_pass.h').read_text()
    and 'replayProjection(inverseView,am,actorRows)' in actor and 'submitReplay(p.get(),actorPass,actorRows,' in actor
    and 'if(nearSkip){' in actor and 'runDirectionalReplays(actorReplays,actorRows,am,actorPass,actorReplayWork,actorIssue,actorTrack)' in actor
    and 'else for(const uint32_t index:actorReplays)' in actor and 'for(size_t index=0;index<replays.size();++index)' not in actor
    and 'staticProofMask' not in actor)
# The near skip: cascade 0 only, valid bounds only (insideInset rejects invalid bounds), actor matrix.
checks['near skip: cascade 0 only, never the far map, by the inset test on valid bounds']=(
    'bool nearSkip=cascade==0&&actorFrameOn[source];' in loop and 'if(nearSkip){actorReplays.clear();try{actorReplays.reserve(replays.size());}catch(...){nearSkip=false;}}' in loop
    and 'InsetProjection actorInset(nearSkip?actorFrameMatrix[source]:nullptr' in loop
    and 'actorInset.extent(p->pointBounds.valid,p->pointBounds.low,p->pointBounds.high,NorthlightActorShadowMap::Stay);' in loop
    and 'if(inActor){actorReplays.push_back(uint32_t(index));++nearSkipped;' in loop and 'extent<=NorthlightActorShadowMap::Inset' in loop
    and loop.count('insideInset(')==1 and 'insideInset(true,p->captureRoot,p->captureRoot,actorFrameMatrix[source])){if(p->shared)side.invalidNear();else side.unsupportedNear();}' in loop and 'if(!valid||!low||!high||!m)return false;' in h
    and h.count('std::isfinite(m[i])')==1 and h.index('std::isfinite(m[i])')<h.index('inline bool insideInset(')) # the matrix once per frame (finiteMatrix), not per replay
# A near map drawn with the skip lacks those casters: reusable only while its actor map is valid (the source went inactive, the detail went off).
checks['near map with skipped casters is redrawn, not reused, once its actor map is gone']=(
    'const bool actorLost=cascade==0&&!splitNear&&nearActorSkip[source]&&!actorValid[source];' in loop
    and 'NorthlightQuality::cascadeAction(reuse,interval,shadowPasses,!reason&&key.valid&&!pull&&!actorLost&&!transition.state.fading(),' in loop
    and loop.index('const bool actorLost=')<reuse
    and 'if(cascade==0)actorValid[source]=nearActorSkip[source]=false;' in loop and 'if(cascade==0)nearActorSkip[source]=nearSkip;' in loop
    and 'nearShadow[s].invalidate();actorValid[s]=false;nearActorSkip[s]=false;' in w)
checks['profile marks: the near replays before the actor map have their own name (SunNear/MoonNear stay unique)']=(
    'profile->mark(source==0?"SunNearReplays":"MoonNearReplays")' in loop and loop.count('"SunNear"')==2)
# Shared soft shadows resolve both maps together. Hard shadows and failed resolves use the separate visibility target.
resolve=fp.src('world_soft_shadows.inl').read_text()
screen_lighting=fp.src('world_screen_lighting.inl').read_text()
atmospheric_fog=fp.src('world_atmospheric_fog.inl').read_text()
screen_owner=fp.src('world_screen_lighting.h').read_text()
light=screen_owner[:screen_owner.index('if(profile)profile->mark(mergedGI?"WorldLightingGI":"WorldLighting");')]
distance_only=screen_owner[screen_owner.index('static void bindDistanceOnly('):screen_owner.index('template<class Pixel>')]
before_local=render[render.index('history.resolveLighting('):render.index('if(!renderLocalLighting(')]
tiles=owner[owner.index('bool buildTiles('):owner.index('bool resolveVisibility(')]
visibility=owner[owner.index('bool resolveVisibility('):owner.index('private:')]
checks['separate resolve only for admitted actor sources, including failed shared resolution']=(
    resolve.index('actorPass[source]=!(')<resolve.index('if(anyActorPass){')<resolve.index('!anyActorPass||actorMaps.resolveVisibility(')
    and '||!mapsReady||!sourceActive[source]||!actorValid[source]||!actorMaps.ready(source));' in resolve
    and 'const bool fogShadowMapsReady=geometryReady&&applyDirectionalShadows;' in render
    and 'resolveDirectionalVisibility(w,h,firstSource,sourceActive,fogShadowMapsReady,anyShadowTransition,' in render
    and 'if(!sharedResolveRequested&&!resolveActorVisibility())return false;' in resolve
    and 'if(!softResolved&&!resolveActorVisibility())return false;' in resolve
    and 'if(!active[source])continue;' in visibility and visibility.count('0xffffffff')==1)
checks['tile reduction uses shared scratch, then a per-source target, then marks the pass']=(
    tiles.index('SetRenderTarget(0,tileScratch_.surface)')<tiles.index('drawViewportQuad(d,Mid,Mid)')<
    tiles.index('SetRenderTarget(0,tiles(source).surface)')<tiles.index('SetTexture(12,tileScratch_.texture)')<
    tiles.index('drawViewportQuad(d,Tiles,Tiles)')<tiles.index('wideCurrent[source]=wide;')
    and resolve.index('actorMaps.buildTiles(')<resolve.index('profile->mark("ActorTiles")')<resolve.index('actorMaps.resolveVisibility(')
    and 'square(Tiles,tiles_[0])&&square(Tiles,tiles_[1])' in owner)
checks['ActorShadow binds its matrix and inputs, resolves sun red or moon green, and restores the borrowed slot']=(
    visibility.index('"clear actor visibility"')<visibility.index('SetPixelShaderConstantF(68,matrices[source],4)')<
    visibility.index('SetTexture(12,depth(source).texture);d->SetTexture(10,tiles(source).texture)')<
    visibility.index('SetPixelShader(soft?filter_:uniform_)')<visibility.index('SetTexture(10,restoreMetadata)')
    and 'SetPixelShaderConstantF(72,Info,1)' in visibility
    and 'source?D3DCOLORWRITEENABLE_GREEN:D3DCOLORWRITEENABLE_RED' in visibility
    and 'D3DRS_COLORWRITEENABLE,15' in visibility)
checks['s12: actor visibility for WorldLighting, then baseline visibility for later lighting readers']=(
    resolve.index('actorMaps.bindLightingVisibility(d,!sharedResolved&&anyActorPass,screenFallbacks.white());')<resolve.index('result.sharedResolved=sharedResolved;')
    and 'd->SetTexture(12,useMask?visibility_.texture:neutral);' in owner
    and render.index('if(!worldNormals.draw(')<render.index('},visibility))return false;')<render.index('if(!renderCelestialLighting(celestialInputs,visibility,')<render.index('history.resolveLighting(')
    and 'd->SetPixelShader(unshadowedLighting?unshadowed_:in.softResolved?in.resolvedLighting:directional_);' in light
    and 'directionalVisibility.lighting(visibility.sharedResolved)' in screen_lighting
    and 'd->SetTexture(12,baseline_.texture);' in light and light.rstrip().endswith('d->SetTexture(15,nullptr);')
    and 'textures[]={foldScene?foldScene:composite.scratchTexture(),depth,directionalMaps.current(0).texture,directionalMaps.current(1).texture,probeAtlas.channel(0),probeAtlas.channel(1),probeAtlas.channel(2),probeAtlas.channel(3),nullptr,nullptr,probeAtlas.channel(4),waterMask,nullptr,' in render
    # The no-lighting branch substitutes zero. No subsequent pass may inherit
    # the actor mask as a lighting baseline.
    and before_local.count('screenLighting.bindDistanceOnly(')==1 and 'SetTexture(12,' not in before_local
    and re.findall(r'SetTexture\(12,([^)]*)\)', distance_only) == ['neutralZero']
    and atmospheric_fog.index('"fog clouds raymarch"')<atmospheric_fog.index('commands.restoreLighting(textures,screenLighting.baseline().texture')<atmospheric_fog.index('if(!drawAtmosphericFog(')<atmospheric_fog.index('        if(effects.fog&&localFogActive&&'))
checks['actor targets follow optional shader admission and release through their owner']=(
    'if(!actorMaps.createTargets(w,h,makeTarget))' in geometry
    and 'if(deferred||hr==E_OUTOFMEMORY||hr==D3DERR_OUTOFVIDEOMEMORY)return false;' in geometry
    and geometry.index('actorMaps.dropResources();')<geometry.index('WORLD actor shadow map disabled: render targets unavailable')
    and 'if(!filter_)return true;' in owner
    and 'allocate(w/2,h/2,D3DFMT_A8R8G8B8,&visibility_.texture,&visibility_.surface)' in owner
    and 'quality.actorShadows&&quality.actorShadowDetail,regionalFogGpu.actorVariants()' in w
    and 'drop(filter_);drop(uniform_);drop(tilesShader_);' in owner
    and 'prepareActorShaders(caps);' in screen and 'if(actorMaps.pending())' in geometry
    and 'actorMaps.releaseTextures();' in geometry and 'actorMaps.settingsChanged();' in w
    and 'releaseShadowMapsOnReset();' in w and 'actorMaps.reset();\n        screenFallbacks.releaseShadow();' in geometry
    and 'drop(tileScratch_.texture);drop(tileScratch_.surface);' in owner
    and 'drop(visibility_.texture);drop(visibility_.surface);' in owner
    and 'solid(d,white_,0xffffffffu,' in fp.src('world_screen_fallbacks.h').read_text()
    and 'nearShadow[s].invalidate();actorValid[s]=false;' in w)
# Hysteresis hold and the near-first bounds order: wired once, gated diagnostics, shown on the actor map log line.
hold=fp.src('actor_side_hold.h').read_text();job=fp.src('replay_bounds_job.h').read_text();near=fp.src('replay_bounds_near.h').read_text();point=fp.src('world_shadow_bounds.inl').read_text()
checks['hold: per-source table, begun with the diagnostics flag (or invalidated when the pass does not run); skinned casters take the root or the valid bounds centre as identity, others today\'s rule']=(
    'NorthlightActorSideHold::Hold actorHold[2];' in w and 'if(!actorMapOn||!sourceActive[source]){actorValid[source]=false;actorHold[source].invalidate();}' in w and 'if(nearSkip)actorHold[source].begin(sideDiag||actorParity,unsigned(frames)+1);else if(cascade==0)actorHold[source].invalidate();' in loop
    and 'if(!p->shadowSkinned)inActor=extent<=NorthlightActorShadowMap::Inset;' in loop and 'const float* position=p->shadowSkinned?p->position():nullptr;' in loop and 'const auto model=NorthlightActorSideHold::holdModel(*p);float centre[3];const float* at=position;' in loop and 'return p.captureRootValid?Model{h,placement}:Model{~h,~placement};' in hold and loop.count('holdModel(*p)')==2 and 'if(!p->captureRootValid){' in loop and 'if(!at){for(unsigned k=0;k<3;++k)centre[k]=.5f*(p->pointBounds.low[k]+p->pointBounds.high[k]);at=centre;}}' in loop
    and 'inActor=actorHold[source].decide(model,at,extent);' in loop and loop.count('actorHold[source].decide(')==1
    and 'if(inActor&&position)actorHold[source].stamp(model,position,matrix,extent);' in loop and 'else if(p->shadowSkinned&&!p->pointBounds.valid&&p->shared){' in loop
    and 'inActor=actorHold[source].carry(NorthlightActorSideHold::holdModel(*p),position,matrix);' in loop and loop.count('actorHold[source].carry(')==1)
checks['hold diagnostics: overflow and band counters only when the pass was begun with diagnostics']=(
    'void begin(bool diagnostics=false,unsigned realFrame=0){advance(1);real_=realFrame?realFrame:frame_;diagnostics_=diagnostics;bandHeld=0;carried=0;}' in hold and 'if(diagnostics_)++overflows;' in hold and 'if(diagnostics_&&!inside)++bandHeld;' in hold)
checks['actor map log line: hold band, overflows, near list size and near walk stops']=(
    ' holdBand=%u holdOverflows=%lu nearListSize=%u nearWalkStops=%lu sideFlipsRootedSinceLog=%u sideFlipsUnrootedSinceLog=%u invalidBoundsNearSinceLog=%u unrootedNoBounds=%u unrootedNoBoundsSinceLog=%u selfUnrooted=%u selfNoBoundsFramesSinceLog=%u holdCarried=%u holdCarriedTotal=%lu"' in actor
    and 'actorHold[source].bandHeld,actorHold[source].overflows,replayNearSize,replayNearStops,' in actor and 'actorSide[source].sideFlipsRooted,actorSide[source].sideFlipsUnrooted,' in actor and 'actorHold[source].carried,actorHold[source].carriedTotal);' in actor
    and 'if(NorthlightDiagnostics::enabled()){replayNearSize=unsigned(replayBoundsWorker.nearList().size());replayNearStops+=result.totals.nearStops;}' in point)
checks['bounds near-first: the list is filled by gather with the items, walks rotate per phase and take at most half of a phase budget']=(
    'priority->consider(i,p.position(),p.selfShadow)' in job and 'schedule(*cache_,items_,inverse_,&cancel_,priority_.on?&priority_:nullptr)' in job
    and 'halfLeft(budget.operations,budget.maxOperations)' in job and 'halfLeft(budget.vertices,budget.maxVertices)' in job and 'used+(max>used?(max-used)/2:0)' in job and 'cache.phaseNanoseconds(phase)<until' in job and job.count('nearWalk(')==3 and 'NorthlightReplaySchedule::pass(index.size(),list->cursor[phase][r]%index.size()' in near
    and not re.search(r'\bnear[(=]',job+near+hold)) # near is a Windows macro
rigidm=fp.src('world_rigid_memory.inl').read_text()
checks['position without bounds: the palette root, or the origin of an injected rigid prop (not a palette root, never mixed into captureRoot)']=(
    'float anchor[3]={};bool anchorValid=false;' in w and 'const float* position()const{return captureRootValid?captureRoot:anchorValid?anchor:nullptr;}' in w
    and 'p->captureRootValid=rooted;if(rooted)std::memcpy(p->captureRoot,root,sizeof p->captureRoot);p->anchorValid=false;' in w
    and 'p->captureRootValid=false;p->selfShadow=false;std::memcpy(p->anchor,e.world+9,sizeof p->anchor);p->anchorValid=true;' in rigidm)
checks['near list: current actor maps and self; hard half-budget caps, then every unfinished packet keeps its rotating turn']=(
    'nearList().begin(actorMatrices[0],actorValid[0],actorMatrices[1],actorValid[1])' in point
    and 'if(priority)priority->consider(i,p.position(),p.selfShadow);' in job
    and 'budget.maxOperations=operations;if(maxVertices)budget.maxVertices=vertices;' in job
    and 'budget.maxOperations=maxOperations;budget.maxVertices=maxVertices;' in job
    and job.index('budget.maxOperations=operations;')<job.index('NorthlightReplayBoundsNear::nearFirst(')<job.index('budget.maxOperations=maxOperations;')
    and all('return visit(index,'+str(phase)+');}));' in job for phase in range(3))
    and 'skipOuter' not in job and 'inline constexpr float Limit=NorthlightActorShadowMap::Stay+(NorthlightActorShadowMap::Leash+CasterYards)/NorthlightActorShadowMap::Radius;' in near)
sel=fp.src('world_shadow_experiment.inl').read_text()
checks['ring 0 is the self: the selection marks the self\'s draws every frame (not only with diagnostics), injected props never inherit the mark']=(
    'for(const auto& a:actorShadowScratch.actors)if(a.self)for(size_t i=a.first;i<a.first+a.count;++i)replays[actorShadowDraws[i].index]->selfShadow=true;' in sel
    and 'if(NorthlightDiagnostics::enabled())for(const auto& a:actorShadowScratch.actors)if(a.self)' not in sel and 'p->selfShadow=false;std::memcpy(p->anchor' in rigidm)
for name,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+name)
assert all(checks.values())
out=fp.output_dir()
compiler=[str(fp.zig()),'c++','-target','x86-windows-gnu'] if os.name=='nt' else ['clang++']
variants=[('O2',['-O2']),('O0',['-O0'])] if os.name=='nt' else [
    ('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined'])]
for label,flags in variants:
    exe=out/('test-'+label+('.exe' if os.name=='nt' else ''))
    subprocess.run([*compiler,'-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG',*flags,*fp.test_include_flags(),
                    str(HERE/'test_actor_shadow_map.cpp'),str(fp.src('world_gi.cpp')),'-o',str(exe)],
                   env=fp.zig_env(),check=True)
    print(label,subprocess.check_output([str(exe)],text=True),end='',flush=True)
print('PASS actor shadow map: model and wiring')
