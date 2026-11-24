// Production point-pass batching and offscreen static retention; no game.
#undef NDEBUG
#include "../src/proxy/renderer.cpp"
#include "fixtures/replay_float_bank.h"
#include <cassert>
#include <chrono>
#include <fstream>
#include "actor_client_programs.h"
static void must(HRESULT hr){assert(SUCCEEDED(hr));}
struct PointTerrainCoverageTest {
#ifndef NORTHLIGHT_POINT_TERRAIN_MEMO_REFERENCE
 static void terrainCandidates(IDirect3DDevice9* d){
  WorldRenderer w(d);w.meshGeneration=1;
  NorthlightLocalLights::Light light{{0,0,4},{1,.4f,.1f},1,12,17,3,NorthlightLocalLights::CastShadow};
  const auto fill=[&](size_t count){w.batches.clear();
   for(size_t i=0;i<count;++i){WorldRenderer::Batch b{};b.terrain=i%7!=0;
    const float x=float(int(i%41)-20),y=float(int(i%53)-26),z=float(int(i%19)-9);
    b.boundsLow={x,y,z};b.boundsHigh={x+2,y+2,z+2};w.batches.push_back(b);}
   ++w.meshGeneration;w.rebuildTerrainLists();};
  const auto clear=[&]{for(auto& list:w.pointTerrainCandidates)list.clear();w.pointCulled=0;};
  // Uncached terrain classification from adaeaa4a, kept as a differential
  // oracle. It does not populate or invalidate the new cache while timing.
  const auto reference=[&](const NorthlightPointShadow::FaceClip& clip,unsigned faces){
   w.pointBatchMasks.assign(w.batches.size(),63);
   const auto append=[&](size_t i){const auto& b=w.batches[i];const unsigned mask=w.pointMask(b.boundsLow,b.boundsHigh,clip,faces);
    w.pointBatchMasks[i]=static_cast<unsigned char>(mask);w.pointAppendCandidates(w.pointTerrainCandidates,i,mask,faces);};
   if(w.terrainBatchListGeneration==w.meshGeneration&&w.terrainBatchListSize==w.batches.size()){
    for(size_t i:w.terrainBatchList)append(i);
   }else for(size_t i=0;i<w.batches.size();++i)if(w.batches[i].terrain)append(i);
  };
  const auto check=[&](unsigned faces){w.pointSlot().selected=light;float matrices[6][16]={};
   for(unsigned f=0;f<6;++f)if(faces&(1u<<f))assert(NorthlightPointShadow::faceMatrix(light.position,.1f,light.attenuationEnd,f,matrices[f],WorldRenderer::PointResolution));
   const NorthlightPointShadow::FaceClip clip(matrices,light,faces);
   clear();reference(clip,faces);const auto masks=w.pointBatchMasks;const auto lists=w.pointTerrainCandidates;const unsigned culled=w.pointCulled;
   clear();w.preparePointTerrain(clip,faces);
   if(w.pointBatchMasks!=masks||w.pointTerrainCandidates!=lists||w.pointCulled!=culled)std::fprintf(stderr,"terrain count=%zu faces=%u maskMatch=%u listMatch=%u rejected=%u expected=%u\n",w.batches.size(),faces,unsigned(w.pointBatchMasks==masks),unsigned(w.pointTerrainCandidates==lists),w.pointCulled,culled);
   assert(w.pointBatchMasks==masks&&w.pointTerrainCandidates==lists&&w.pointCulled==culled);
  };
  for(size_t count:{size_t(0),size_t(128),size_t(4096),size_t(65536),size_t(65537)}){
   fill(count);
#ifdef NORTHLIGHT_POINT_OVERLAP_TEST
   w.pointTree=NorthlightWorldMeshPages::PointTree::build(w.batches,w.pointSubBatches);
#endif
   check(63);assert(w.pointSlot().terrainMemo.valid==(count<=WorldRenderer::PointTerrainMemo::Limit));
   check(63);for(unsigned faces:{1u,2u,4u,8u,16u,32u,21u,42u})check(faces);
   w.terrainBatchListGeneration=UINT64_MAX;check(63); // list fallback is equivalent
   light.position[0]+=.25f;check(1);assert(!w.pointSlot().terrainMemo.valid);check(63);
   light.attenuationEnd+=.25f;check(63);
   if(count){w.batches[0].terrain=true;w.batches[0].boundsLow={-1,-1,0};w.batches[0].boundsHigh={1,1,8};}
#ifdef NORTHLIGHT_POINT_OVERLAP_TEST
   w.pointTree.reset();
#endif
   ++w.meshGeneration;check(63);
   assert(w.pointSlot().terrainMemo.masks.capacity()<=WorldRenderer::PointTerrainMemo::Limit);
  }
  fill(2048);float matrices[14][6][16]={};
#ifdef NORTHLIGHT_POINT_OVERLAP_TEST
  w.pointTree.reset();
#endif
  for(unsigned l=0;l<14;++l){w.pointIndex=l;light.position[0]=float(l)*.2f;w.pointSlot().selected=light;
   for(unsigned f=0;f<6;++f)assert(NorthlightPointShadow::faceMatrix(light.position,.1f,light.attenuationEnd,f,matrices[l][f],WorldRenderer::PointResolution));
   const NorthlightPointShadow::FaceClip clip(matrices[l],light);clear();w.preparePointTerrain(clip,63);}
  for(unsigned block=0;block<16;++block){const bool cached=block%4==1||block%4==2;const auto start=std::chrono::steady_clock::now();
   for(unsigned frame=0;frame<32;++frame)for(unsigned l=0;l<14;++l){w.pointIndex=l;const NorthlightPointShadow::FaceClip clip(matrices[l],w.pointSlot().selected);
    clear();if(cached)w.preparePointTerrain(clip,63);else reference(clip,63);}
   const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/32;
   std::printf("POINT_TERRAIN_CANDIDATES_TIMING block=%u warm=%u cached=%u lights=14 batches=2048 cpuMs=%.6f\n",block,unsigned(block<4),unsigned(cached),ms);
  }
#ifndef NORTHLIGHT_POINT_EMPTY_TERRAIN_REFERENCE
  // Match the last gameplay log: thousands of terrain batches, all outside
  // each interior lamp. Compare with the former warm memo expansion exactly.
  fill(12854);for(auto& b:w.batches){b.boundsLow.z=-100;b.boundsHigh.z=-99;}
  ++w.meshGeneration;w.rebuildTerrainLists();
  std::vector<unsigned char> previousEmptyMasks(w.batches.size(),63);
  for(auto i:w.terrainBatchList)previousEmptyMasks[i]=0;
  for(unsigned l=0;l<14;++l){w.pointIndex=l;w.pointSlot().terrainMemo.valid=false;
   const NorthlightPointShadow::FaceClip clip(matrices[l],w.pointSlot().selected);clear();w.preparePointTerrain(clip,63);
   assert(w.pointSlot().terrainMemo.valid&&!w.pointSlot().terrainMemo.occupiedFaces);
#ifdef NORTHLIGHT_POINT_OVERLAP_TEST
   assert(w.pointSlot().terrainMemo.masks.capacity()==0);
#endif
   const auto masks=w.pointBatchMasks;const auto lists=w.pointTerrainCandidates;const unsigned culled=w.pointCulled;
   for(unsigned faces:{63u,1u,21u,42u}){clear();reference(clip,faces);const auto expected=w.pointBatchMasks;const auto candidates=w.pointTerrainCandidates;const auto rejected=w.pointCulled;
    clear();w.preparePointTerrain(clip,faces);assert(w.pointBatchMasks==expected&&w.pointTerrainCandidates==candidates&&w.pointCulled==rejected);}
   clear();w.preparePointTerrain(clip,63);assert(w.pointBatchMasks==masks&&w.pointTerrainCandidates==lists&&w.pointCulled==culled);
  }
  for(unsigned block=0;block<16;++block){const bool cached=block%4==1||block%4==2;const auto start=std::chrono::steady_clock::now();
   for(unsigned frame=0;frame<32;++frame)for(unsigned l=0;l<14;++l){w.pointIndex=l;clear();
    if(cached){const NorthlightPointShadow::FaceClip clip(matrices[l],w.pointSlot().selected);w.preparePointTerrain(clip,63);}
    else{w.pointBatchMasks=previousEmptyMasks;
     for(size_t i:w.terrainBatchList)w.pointAppendCandidates(w.pointTerrainCandidates,i,w.pointBatchMasks[i],63);}
   }
   const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/32;
   std::printf("POINT_EMPTY_TERRAIN_TIMING block=%u warm=%u cached=%u lights=14 batches=12854 cpuMs=%.6f\n",block,unsigned(block<4),unsigned(cached),ms);
  }
  std::puts("POINT_EMPTY_TERRAIN_PASS warmMemo=exact partialFaces=exact rejectionCounts=exact");
#endif
#ifdef NORTHLIGHT_POINT_OVERLAP_TEST
  // A few nearby chunks amid distant terrain. Compare compact reuse to the
  // former warm dense expansion, including partial cube refreshes.
  fill(12854);for(size_t i=0;i<w.batches.size();++i)if(i%32)w.batches[i].boundsLow.z=w.batches[i].boundsHigh.z=-100;
  ++w.meshGeneration;w.rebuildTerrainLists();
  std::vector<unsigned char> previous[14];
  for(unsigned l=0;l<14;++l){w.pointIndex=l;const NorthlightPointShadow::FaceClip clip(matrices[l],w.pointSlot().selected);
   clear();w.preparePointTerrain(clip,63);previous[l]=w.pointBatchMasks;
   assert(!w.pointSlot().terrainMemo.visible.empty()&&w.pointSlot().terrainMemo.masks.capacity()==0);
   for(unsigned faces:{63u,1u,21u,42u}){
    clear();reference(clip,faces);const auto masks=w.pointBatchMasks;const auto lists=w.pointTerrainCandidates;const auto rejected=w.pointCulled;
    for(unsigned staticFaces:{0u,faces}){clear();w.preparePointTerrain(clip,faces,staticFaces);
     assert(w.pointBatchMasks==masks&&w.pointTerrainCandidates==lists&&w.pointCulled==rejected);}
   }
  }
  for(unsigned block=0;block<16;++block){const bool compact=block%4==1||block%4==2;const auto start=std::chrono::steady_clock::now();
   for(unsigned frame=0;frame<32;++frame)for(unsigned l=0;l<14;++l){w.pointIndex=l;clear();
    const NorthlightPointShadow::FaceClip clip(matrices[l],w.pointSlot().selected);
    if(compact)w.preparePointTerrain(clip,63,0);
    else{w.pointBatchMasks=previous[l];for(size_t i:w.terrainBatchList)w.pointAppendCandidates(w.pointTerrainCandidates,i,w.pointBatchMasks[i],63);}
   }
   const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/32;
   std::printf("POINT_SPARSE_TERRAIN_TIMING warm=%u compact=%u lights=14 batches=12854 cpuMs=%.6f\n",unsigned(block<4),unsigned(compact),ms);
  }
#endif
  w.pointIndex=0;w.active=std::make_shared<WorldRenderer::Snapshot>();w.active->map="terrain-memo";
  auto& slot=w.pointSlot();slot.map=w.active->map;slot.cacheValid=true;slot.cacheSource=slot.selected.sourceId;
  w.keepPointSlot(slot,1);assert(w.pointTargets.keeps()[0].slot.terrainMemo.masks.empty());
  w.releasePointGPU();for(const auto& p:w.pointTargets.slots())assert(p.terrainMemo.masks.empty());
  std::puts("POINT_TERRAIN_CANDIDATES_PASS reference=exact partialFaces=exact meshLightRangeMutation=exact fallback=exact boundedKiB=4096 keeps=empty reset=empty");
 }
#endif
 static void captureDemand(IDirect3DDevice9* d){
  WorldRenderer w(d);w.active=std::make_shared<WorldRenderer::Snapshot>();w.active->bvh=std::make_shared<NorthlightGI::BVH>();w.active->map=w.lastRequest.map="capture-demand";
  w.valid=w.celestialValid=true;w.sourceColors[0]=w.sourceColors[1]={};w.quality.actorShadows=w.quality.localLights=w.quality.pointShadows=w.quality.equipmentLights=1;
  w.quality.localLightLimit=w.quality.pointShadowLimit=64;w.quality.gi=0;w.effects.shadows=true;w.effects.gi=false;w.captureDemand=false;w.lastRenderDebug=0;
  unsigned cases=0;auto expect=[&](bool skip){w.captureMode=WorldRenderer::CaptureUndecided;w.actorCaptureDecided=false;w.actorJob.reset();assert(w.modelCaptureSkipped()==skip);++cases;};
  expect(true); // Both current-frame directional sources are zero, no local light.
  w.sourceColors[0]={.1f,0,0};expect(false);w.sourceColors[0]={};w.sourceColors[1]={0,.1f,0};expect(false);w.sourceColors[1]={};expect(true);
  w.celestialValid=false;expect(false);w.celestialValid=true;w.valid=false;expect(false);w.valid=true;
  w.active->map="different-map";expect(false);w.active->map=w.lastRequest.map;
  auto bvh=w.active->bvh;w.active->bvh.reset();expect(true);w.active->bvh=bvh;
  w.captureDemand=true;expect(false);expect(true);w.lastRenderDebug=1;expect(false);w.lastRenderDebug=0;
  w.quality.gi=1;w.effects.gi=true;w.lastActorCapture=GetTickCount();expect(true);w.lastActorCapture=0;expect(false);w.quality.gi=0;w.effects.gi=false;
  NorthlightLocalLights::Light light{{0,0,4},{1,.4f,.1f},1,12,17,3,NorthlightLocalLights::CastShadow};
  w.active->localLights.push_back(light);expect(false);w.quality.pointShadows=0;expect(true);w.quality.pointShadows=1;
  w.quality.localLights=0;expect(true);w.quality.localLights=1;w.active->localLights.clear();
  w.equipmentLights.output[0]=light;w.equipmentLights.outputCount=1;expect(false);w.equipmentLights.outputCount=0;
  NorthlightParticleLights::Sample particle{{0,0,4},{1,.4f,.1f},1,0};w.particleLights.observe(particle,w.context.camera);expect(false);w.particleLights.reset();
  particle.flashRadius=100;w.particleLights.observe(particle,w.context.camera);expect(false);w.particleLights.reset();
  w.lightAttachments.positions[1]={0,0,4};expect(false);w.lightAttachments.positions.clear();expect(true);
  std::printf("SHADOW_CAPTURE_DEMAND_PASS cases=%u zeroToSunMoon=1 unknownContext=1 mapChange=1 geometryAbsent=1 localEquipmentParticleFlash=1 giCadence=1 poseDemand=1\n",cases);
 }
 struct WarmDevice:ForwardIDirect3DDevice9 {
  using ForwardIDirect3DDevice9::ForwardIDirect3DDevice9;unsigned creates=0;bool failCube=false;
  HRESULT STDMETHODCALLTYPE CreateCubeTexture(UINT e,UINT l,DWORD u,D3DFORMAT f,D3DPOOL p,IDirect3DCubeTexture9** t,HANDLE* h)override{++creates;if(failCube){failCube=false;*t=nullptr;return E_OUTOFMEMORY;}return real->CreateCubeTexture(e,l,u,f,p,t,h);}
  HRESULT STDMETHODCALLTYPE CreateTexture(UINT w,UINT h,UINT l,DWORD u,D3DFORMAT f,D3DPOOL p,IDirect3DTexture9** t,HANDLE* x)override{++creates;return real->CreateTexture(w,h,l,u,f,p,t,x);}
  HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(UINT w,UINT h,D3DFORMAT f,D3DMULTISAMPLE_TYPE m,DWORD q,BOOL z,IDirect3DSurface9** t,HANDLE* x)override{++creates;return real->CreateDepthStencilSurface(w,h,f,m,q,z,t,x);}
  HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD* c,IDirect3DVertexShader9** s)override{++creates;return real->CreateVertexShader(c,s);}
  HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD* c,IDirect3DPixelShader9** s)override{++creates;return real->CreatePixelShader(c,s);}
 };
 static void prewarmPoints(IDirect3DDevice9* d){
  WarmDevice meter(d);WorldRenderer w(&meter);w.active=std::make_shared<WorldRenderer::Snapshot>();w.active->map="prewarm";
  w.quality.pointShadowLimit=2;w.quality.pointShadowDistance=48;w.quality.pointShadows=w.quality.localLights=1;w.quality.localLightLimit=64;w.effects.shadows=true;
  NorthlightLocalLightSelection::Selection lights;lights.count=3;for(unsigned i=0;i<3;++i)lights.sources[i]={{float(i)*3,0,4},{1,.4f,.1f},1,12,100+i,3,NorthlightLocalLights::CastShadow};
  auto resident=[&]{unsigned n=0;for(const auto& p:w.pointTargets.slots())n+=p.cube!=nullptr;return n;};
  auto step=[&](const std::string& map="prewarm"){const unsigned before=meter.creates;const auto selection=w.pointSelection;
   NorthlightStreaming::Budget budget(4u<<20,1000);w.prewarmPointResources(lights,map,budget);assert(meter.creates-before<=1);
   assert(w.pointSelection.initialized==selection.initialized&&w.pointSelection.updatedAt==selection.updatedAt);
   for(unsigned i=0;i<w.pointTargets.slots().size();++i){const auto& p=w.pointTargets.slots()[i];assert(!p.ready&&!p.schedule.usable&&!p.retryAt&&!p.cacheValid);for(auto* f:p.cacheFace)assert(!f);
    assert(w.pointSelection.entries[i].id==selection.entries[i].id&&w.pointSelection.entries[i].gain==selection.entries[i].gain&&w.pointSelection.entries[i].occupied==selection.entries[i].occupied);}
  };
  NorthlightStreaming::Budget exhausted(0,1000);w.prewarmPointResources(lights,"prewarm",exhausted);assert(!meter.creates);
  for(unsigned i=0;resident()<2;++i){assert(i<16);step();}
#ifdef NORTHLIGHT_POINT_UNION_REFERENCE
  constexpr unsigned creates=8;
#else
  constexpr unsigned creates=7;assert(!w.pointTargets.empty());
#endif
  assert(meter.creates==creates&&w.pointCold.cubes==2); // Three shaders, shared targets, two desired cubes.
  assert(w.pointTargets.slots()[0].selected.sourceId==100&&w.pointTargets.slots()[1].selected.sourceId==101);for(unsigned i=0;i<8;++i)step();assert(meter.creates==creates);
  w.quality.pointShadowLimit=1;step();assert(resident()==1);
  lights.sources[0].position[0]=1000;step();assert(resident()==1&&w.pointTargets.slots()[0].selected.sourceId==101);
  w.memoryPressure=true;step();assert(!resident());w.memoryPressure=false;
  step();assert(resident()==1);w.active->map="other";step();assert(!resident());step("other");assert(resident()==1);
  w.releasePointGPU();assert(!resident()&&!w.pointTargets.scratch()&&!w.pointTargets.depth()&&!w.pointTargets.empty());
  for(unsigned i=0;
#ifdef NORTHLIGHT_POINT_UNION_REFERENCE
      !w.pointTargets.empty();
#else
      !w.pointTargets.depth();
#endif
      ++i){assert(i<8);step("other");}
  meter.failCube=true;step("other");assert(!resident()&&!meter.failCube&&!w.pointFailures); // Failure cannot delay the next live allocation.
  assert(w.pointResources());assert(w.pointSlot().cube&&!w.pointSlot().retryAt);w.releasePointGPU();
  w.active->map="prewarm";w.pointSelection.initialized=true;w.pointSelection.updatedAt=17;w.pointSelection.entries[0]={101,.35f,true};const unsigned before=meter.creates;step();assert(meter.creates==before&&!resident());
  std::printf("POINT_PREWARM_GPU_PASS desiredCubes=2 allocationsPerFrame=1 unpublished=1 selectionUnchanged=1 changedChoice=1 limitReduction=1 mapPressureReset=1 failureNoBackoff=1\n");
 }
 static void prewarm(IDirect3DDevice9* d){
  WorldRenderer reference(d),staged(d);
  for(auto* w:{&reference,&staged})assert(w->resources(64,64,D3DFMT_A8R8G8B8));
  auto resources=[](WorldRenderer& w){std::vector<IDirect3DResource9*> r;
   for(unsigned slot=0;slot<4;++slot){r.push_back(w.directionalMaps.current(slot).texture);r.push_back(w.directionalMaps.cache(slot).texture);}
   w.actorMaps.forEachTexture([&](auto* p){r.push_back(p);});
   for(unsigned i=0;i<5;++i)r.push_back(w.probeAtlas.channel(i));
   for(auto* p:{w.probeAtlas.previous(),w.directionalMaps.scratch().texture})r.push_back(p);
   for(auto* p:{w.directionalMaps.depth(),w.directionalMaps.cacheDepth()})r.push_back(p);return r;
  };
  NorthlightStreaming::Budget exhausted(0,2);assert(!staged.geometryResources(64,64,&exhausted));
  auto before=resources(staged);assert(std::none_of(before.begin(),before.end(),[](auto* p){return p!=nullptr;}));
  unsigned frames=0;
  while(!staged.geometryTargetsReady){
   NorthlightStreaming::Budget budget(4u<<20,2);staged.geometryResources(64,64,&budget);++frames;assert(frames<64&&!staged.geometryTargetsRetry);
   auto after=resources(staged);unsigned created=0;for(size_t i=0;i<before.size();++i){if(before[i])assert(after[i]==before[i]);else created+=after[i]!=nullptr;}assert(created<=1);before=std::move(after);
   if(frames==3){staged.releaseGPU(true);assert(staged.resources(64,64,D3DFMT_A8R8G8B8));before=resources(staged);assert(std::none_of(before.begin(),before.end(),[](auto* p){return p!=nullptr;}));}
  }
  assert(reference.geometryResources(64,64));const auto expected=resources(reference),actual=resources(staged);assert(expected.size()==actual.size());
  for(size_t i=0;i<expected.size();++i){assert(bool(expected[i])==bool(actual[i]));if(!expected[i])continue;
   const auto type=expected[i]->GetType();assert(type==actual[i]->GetType());D3DSURFACE_DESC a{},b{};
   if(type==D3DRTYPE_TEXTURE){must(static_cast<IDirect3DTexture9*>(expected[i])->GetLevelDesc(0,&a));must(static_cast<IDirect3DTexture9*>(actual[i])->GetLevelDesc(0,&b));}
   else{must(static_cast<IDirect3DSurface9*>(expected[i])->GetDesc(&a));must(static_cast<IDirect3DSurface9*>(actual[i])->GetDesc(&b));}
   assert(a.Width==b.Width&&a.Height==b.Height&&a.Format==b.Format&&a.Pool==b.Pool&&a.Usage==b.Usage&&a.MultiSampleType==b.MultiSampleType);
  }
  for(unsigned i=0;i<3;++i)staged.prewarmPointShaders();assert(staged.pointTargets.casterVertex()&&staged.pointTargets.casterPixel()&&staged.pointLighting.hardShader());
  std::printf("GEOMETRY_PREWARM_GPU_PASS frames=%u allocationsPerFrame=1 exhausted_no_allocation=1 partial_reset=1 complete_resources=1 point_shaders=1\n",frames);
 }
 struct UnionMeter:ForwardIDirect3DDevice9 {
  using ForwardIDirect3DDevice9::ForwardIDirect3DDevice9;unsigned quads=0,cubes=0;bool failClear=false;
  HRESULT STDMETHODCALLTYPE Clear(DWORD count,const D3DRECT* rects,DWORD flags,D3DCOLOR color,float depth,DWORD stencil)override{
   if(failClear){failClear=false;return E_FAIL;}return real->Clear(count,rects,flags,color,depth,stencil);
  }
  HRESULT STDMETHODCALLTYPE CreateCubeTexture(UINT e,UINT l,DWORD u,D3DFORMAT f,D3DPOOL p,IDirect3DCubeTexture9** t,HANDLE* h)override{
   ++cubes;return real->CreateCubeTexture(e,l,u,f,p,t,h);
  }
  HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE type,UINT count,const void* vertices,UINT stride)override{
   quads+=type==D3DPT_TRIANGLESTRIP&&count==2;return real->DrawPrimitiveUP(type,count,vertices,stride);
  }
 };
 static void run(IDirect3DDevice9* d,const char* skinPath){
#ifndef NORTHLIGHT_POINT_TERRAIN_MEMO_REFERENCE
  terrainCandidates(d);
#endif
  captureDemand(d);prewarm(d);prewarmPoints(d);
  UnionMeter meter(d);WorldRenderer w(&meter);
  // Deliberate fail-open oracle: no usable bounds, and no hidden warmup.
  auto unboundedReference=[&]{w.replayBoundsBegin();for(auto& p:w.replays)p->pointBounds={};w.replayBoundsResolved=true;};w.valid=true;w.frames=1;w.quality.pointShadowLimit=64;w.quality.pointShadowFacesPerFrame=6;
  w.active=std::make_shared<WorldRenderer::Snapshot>();w.active->map="point-stutter-proof";w.active->bvh=std::make_shared<NorthlightGI::BVH>();w.uploadedMap=w.active->map;w.meshGeneration=1;
  NorthlightGI::WorldVertex vertices[]={{{-1,-1,0},{0,0,1},0,0},{{1,-1,0},{0,0,1},1,0},{{-1,1,0},{0,0,1},0,1},{{1,1,8},{0,0,-1},1,1},{{-1,1,8},{0,0,-1},0,1},{{1,-1,8},{0,0,-1},1,0}};
  constexpr unsigned triangles=2048;std::vector<WORD> indices;
  for(unsigned i=0;i<triangles;++i)for(WORD index:{WORD(i%2?3:0),WORD(i%2?4:1),WORD(i%2?5:2)})indices.push_back(index);
  assert(w.meshStore.beginStaging({NorthlightMeshPageKey::keyOf(vertices,sizeof vertices,indices.data(),indices.size()*2)}));w.meshStore.commit();w.meshBuffers.resize(w.meshStore.entries().size());auto& page=w.meshBuffers[w.meshStore.bufferOf(0)];
  must(d->CreateVertexBuffer(sizeof vertices,0,0,D3DPOOL_MANAGED,&page.vb,nullptr));must(d->CreateIndexBuffer(UINT(indices.size()*2),0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&page.ib,nullptr));
  void* data=nullptr;must(page.vb->Lock(0,0,&data,0));std::memcpy(data,vertices,sizeof vertices);must(page.vb->Unlock());must(page.ib->Lock(0,0,&data,0));std::memcpy(data,indices.data(),indices.size()*2);must(page.ib->Unlock());w.vertices=page.vb;w.indices=page.ib;
  const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,24,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};must(d->CreateVertexDeclaration(elements,&w.shadowPrograms.worldLayout_));must(d->CreatePixelShader(kShadowUnionShader,&w.shadowPrograms.union_));
  w.materials.push_back(nullptr);w.uploadedAlphaCutoffs.push_back(-1);
  for(unsigned i=0;i<triangles;++i){WorldRenderer::Batch b{};b.page=0;b.start=3*i;b.count=1;b.minVertex=i%2?3:0;b.vertexCount=3;b.boundsLow={-1,-1,i%2?8.f:0.f};b.boundsHigh={1,1,i%2?8.f:0.f};w.batches.push_back(b);}
  const std::pair<D3DRENDERSTATETYPE,DWORD> states[]={{D3DRS_ZENABLE,TRUE},{D3DRS_ZWRITEENABLE,TRUE},{D3DRS_ZFUNC,D3DCMP_LESSEQUAL},{D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_ALPHATESTENABLE,FALSE},{D3DRS_ALPHABLENDENABLE,FALSE},{D3DRS_FOGENABLE,FALSE},{D3DRS_STENCILENABLE,FALSE},{D3DRS_SCISSORTESTENABLE,FALSE},{D3DRS_CLIPPLANEENABLE,0},{D3DRS_SRGBWRITEENABLE,FALSE},{D3DRS_DEPTHBIAS,0},{D3DRS_SLOPESCALEDEPTHBIAS,0},{D3DRS_FILLMODE,D3DFILL_SOLID}};
  for(const auto& state:states)must(d->SetRenderState(state.first,state.second));
  D3DCAPS9 caps{};must(d->GetDeviceCaps(&caps));assert(caps.MaxPrimitiveCount>=triangles);
  IDirect3DSurface9* readback=nullptr;must(d->CreateOffscreenPlainSurface(256,256,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&readback,nullptr));
  IDirect3DQuery9* fence=nullptr;must(d->CreateQuery(D3DQUERYTYPE_EVENT,&fence));
  auto drain=[&](){must(fence->Issue(D3DISSUE_END));const DWORD started=GetTickCount();HRESULT hr;while((hr=fence->GetData(nullptr,0,D3DGETDATA_FLUSH))==S_FALSE){assert(DWORD(GetTickCount()-started)<10000);SwitchToThread();}must(hr);};
  auto image=[&](){std::vector<float> result(6*256*256);for(unsigned f=0;f<6;++f){must(d->GetRenderTargetData(w.pointSlot().faces[f],readback));D3DLOCKED_RECT lock{};must(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));for(unsigned y=0;y<256;++y)std::memcpy(result.data()+f*256*256+y*256,static_cast<char*>(lock.pBits)+y*lock.Pitch,256*4);must(readback->UnlockRect());}return result;};
  NorthlightLocalLights::Light light{{0,0,4},{1,.4f,.1f},1,12,17,3,NorthlightLocalLights::CastShadow};
  {
   const auto saved=w.batches;WorldRenderer::Batch shell{};shell.page=0;shell.count=2;shell.vertexCount=6;
   shell.boundsLow={-1,-1,0};shell.boundsHigh={1,1,8};shell.lampSourceId=light.sourceId;
   w.batches={shell};std::vector<float> expected;
   for(const auto source:{light.sourceId,uint64_t(0),light.sourceId^uint64_t(1),light.sourceId}){
    w.batches[0].lampSourceId=source;++w.meshGeneration;w.releasePointGPU();w.pointWork={};
    must(d->BeginScene());assert(w.renderPointShadow(light,true,false));must(d->EndScene());drain();const auto pixels=image();
    assert(w.pointWork.statics>0&&std::any_of(pixels.begin(),pixels.end(),[](float v){return v<1.f;}));
    if(expected.empty())expected=pixels;else assert(pixels==expected);
   }
   w.batches=saved;++w.meshGeneration;w.releasePointGPU();w.pointWork={};
   std::puts("POINT_LAMP_CASTER_PASS taggedUntaggedForeign=exact housingOccludes=1");
  }

  auto render=[&](){must(d->BeginScene());assert(w.renderPointShadow(light,true,false));must(d->EndScene());drain();};
  std::vector<float> expected;unsigned referenceDraws=0,mergedDraws=0;
  for(unsigned mode=0;mode<2;++mode){w.pointPrimitiveLimit=mode?caps.MaxPrimitiveCount:1;w.pointSlot().cacheValid=false;w.pointSlot().schedule.invalidate();w.pointWork={};render();
   const auto actual=image();if(!mode){expected=actual;referenceDraws=w.pointDraws;}else{assert(actual==expected);mergedDraws=w.pointDraws;assert(w.pointWork.bridged>0&&w.pointWork.bridged<=w.pointWork.triangles/2);}}
  assert(referenceDraws>=triangles&&mergedDraws<referenceDraws/16);
  std::printf("POINT_GAP_GPU_PASS faces=6 exact=1 referenceDraws=%u mergedDraws=%u\n",referenceDraws,mergedDraws);
#ifdef NORTHLIGHT_POINT_MATERIAL_BOUNDS_TEST
  {
   auto shaded=light;shaded.attenuationStart+=.5f;
   const auto savedQuality=w.quality;w.quality.pointShadowRefreshMs=10000;
   w.quality.diagnostics=w.quality.renderProfile=1;
   for(bool force:{false,true}){
    w.pointWork={};w.pointCold={};if(force)w.pointSlot().cacheValid=false;
    must(d->BeginScene());assert(w.renderPointShadow(shaded,true,false));must(d->EndScene());drain();
    assert(w.pointSlot().selected.attenuationStart==shaded.attenuationStart&&image()==expected);
    assert(w.pointWork.rebuilt==(force?6u:0u)&&(force?w.pointWork.statics>0:w.pointDraws==0));
    assert(w.pointCold.facesWithoutCache==w.pointWork.rebuilt);
   }
   auto changed=shaded;
   for(unsigned step=0;step<3;++step){
    if(step==0)changed.position[0]+=.25f;
    if(step==1)changed.attenuationEnd+=1;
    if(step==2){++w.meshGeneration;for(auto& known:w.pointSlot().faceKnown)known=false;}
    w.pointWork={};w.pointCold={};
    must(d->BeginScene());assert(w.renderPointShadow(changed,true,false));must(d->EndScene());drain();
    assert(w.pointWork.rebuilt==6&&w.pointCold.facesWithoutCache==0&&w.pointCold.facesMoved==(step==0?6u:0u)&&
           w.pointCold.facesResized==(step==1?6u:0u)&&w.pointCold.facesMeshInvalidated==(step==2?6u:0u));
   }
   w.quality=savedQuality;
   std::puts("POINT_SHADING_ONLY_PASS retainedFaces=6 forcedRebuildFaces=6 identical=1");
  }
#endif
  w.releasePointGPU();w.quality.pointShadows=w.quality.localLights=1;w.quality.localLightLimit=64;w.effects.shadows=true;
  NorthlightLocalLightSelection::Selection warmLights;warmLights.count=1;warmLights.sources[0]=light;
  for(unsigned i=0;!w.pointSlot().cube;++i){assert(i<10);NorthlightStreaming::Budget budget(4u<<20,1000);w.prewarmPointResources(warmLights,w.active->map,budget);}
  auto* warmedCube=w.pointSlot().cube;assert(w.pointSlot().prewarmed&&!w.pointSlot().ready&&!w.pointSlot().cacheValid);w.pointCold={};render();assert(w.pointSlot().cube==warmedCube&&image()==expected&&!w.pointCold.cubes&&!w.pointCold.shared&&w.pointCold.statics);
  std::printf("POINT_PREWARM_IMAGE_PASS cubeReused=1 sixFaces=exact firstRenderCubeSharedAllocations=0 staticFacesStillLazy=1\n");
  // Unknown/excluded gaps, page/index discontinuity, overflow and excessive
  // clipped work remain separate. The real GPU test above supplies the masks.
  const auto saved=w.batches;const auto masks=w.pointBatchMasks;
  const std::vector<size_t> pair={0,2};
  for(unsigned reason=0;reason<7;++reason){w.batches={saved[0],saved[1],saved[2]};w.pointBatchMasks={63,0,63};w.pointPrimitiveLimit=caps.MaxPrimitiveCount;
   if(reason==1)w.pointBatchMasks[1]=63;if(reason==2)++w.batches[1].page;if(reason==3)++w.batches[1].start;
   if(reason==4){w.batches[1].minVertex=UINT_MAX;w.batches[1].vertexCount=2;}
   if(reason==5){w.batches[1].count=3;w.batches[2].start=12;}if(reason==6)w.pointPrimitiveLimit=2;
   size_t at=0;const auto run=w.pointBatchRun(pair,at,5);assert(at==(reason?1u:2u));assert(run.count==(reason?1u:3u));
  }
  w.batches=saved;w.pointBatchMasks=masks;w.pointPrimitiveLimit=caps.MaxPrimitiveCount;
  // ABBA blocks exercise actual cold static faces, with unchanged GPU inputs.
  for(unsigned block=0;block<8;++block){const bool optimized=block%4==1||block%4==2;w.pointPrimitiveLimit=optimized?caps.MaxPrimitiveCount:1;
   double submit=0,complete=0;unsigned draws=0;for(unsigned frame=0;frame<8;++frame){w.pointSlot().cacheValid=false;w.pointSlot().schedule.invalidate();drain();auto start=std::chrono::steady_clock::now();must(d->BeginScene());assert(w.renderPointShadow(light,true,false));must(d->EndScene());submit+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();draws+=w.pointDraws;drain();complete+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}
   assert(image()==expected);std::printf("POINT_GAP_TIMING block=%u optimized=%u submitMs=%.6f completeMs=%.6f draws=%u\n",block,unsigned(optimized),submit/8,complete/8,draws/8);std::fflush(stdout);
  }
  w.pointPrimitiveLimit=caps.MaxPrimitiveCount;
#ifndef NORTHLIGHT_POINT_TERRAIN_REFERENCE
  {
   // Complete faces containing immutable terrain are reusable too. Force the
   // old path by clearing only the new proof; all submission code stays shared.
   const auto savedBatches=w.batches;for(auto& b:w.batches)b.terrain=true;
   ++w.meshGeneration;w.releasePointGPU();w.quality.pointShadowRefreshMs=0;
   const auto refresh=[&](bool retain){if(!retain)w.pointSlot().geometryOnlyFaces=0;w.pointSlot().schedule.updatedAt=GetTickCount()-10000;render();};
   refresh(false);const auto ground=image();const unsigned groundDraws=w.pointDraws;assert(groundDraws>0);
#ifndef NORTHLIGHT_POINT_TERRAIN_MEMO_REFERENCE
   assert(w.pointSlot().terrainMemo.valid);refresh(false);assert(image()==ground&&w.pointDraws==groundDraws);
   w.pointSlot().terrainMemo.valid=false;refresh(false);assert(image()==ground&&w.pointDraws==groundDraws);
   std::puts("POINT_TERRAIN_CANDIDATES_GPU_PASS sixFaces=exact warmCold=exact draws=unchanged");
#endif
   const auto weights=std::array<unsigned,6>{w.pointSlot().faceWeights[0],w.pointSlot().faceWeights[1],w.pointSlot().faceWeights[2],w.pointSlot().faceWeights[3],w.pointSlot().faceWeights[4],w.pointSlot().faceWeights[5]};
#ifndef NORTHLIGHT_POINT_EMPTY_TERRAIN_REFERENCE
   const auto memoHits=w.pointCold.terrainMemoHits,memoMisses=w.pointCold.terrainMemoMisses;
#endif
   refresh(true);assert(!w.pointDraws&&w.pointFacesReused==6&&image()==ground);
#ifndef NORTHLIGHT_POINT_EMPTY_TERRAIN_REFERENCE
   assert(w.pointCold.terrainMemoHits==memoHits&&w.pointCold.terrainMemoMisses==memoMisses);
#endif
   assert(std::equal(weights.begin(),weights.end(),w.pointSlot().faceWeights));
   NorthlightGpuFrameTimer timer(d);
   for(unsigned block=0;block<16;++block){const bool retain=block%4==1||block%4==2;
    drain();const auto start=std::chrono::steady_clock::now();unsigned draws=0;
    must(d->BeginScene());const bool timed=timer.begin(block);
    for(unsigned frame=0;frame<32;++frame){if(!retain)w.pointSlot().geometryOnlyFaces=0;w.pointSlot().schedule.updatedAt=GetTickCount()-10000;
     assert(w.renderPointShadow(light,true,false));draws+=w.pointDraws;}
    timer.end();must(d->EndScene());const double cpu=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/32;
    drain();const double complete=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/32;
    double gpu=-1;if(timed)timer.poll(gpu);assert(image()==ground);
    assert(retain?draws==0:draws==groundDraws*32);
    std::printf("POINT_TERRAIN_TIMING block=%u warm=%u retained=%u cpuMs=%.6f completeMs=%.6f gpuMs=%.6f draws=%u\n",block,unsigned(block<4),unsigned(retain),cpu,complete,gpu<0?-1:gpu/32,draws/32);
   }
   // Use the actual upload route: stable owners keep the revision, changed,
   // removed and reintroduced captures invalidate even without a mesh swap.
   const D3DVERTEXELEMENT9 liveElements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
   must(d->CreateVertexDeclaration(liveElements,&w.shadowPrograms.liveLayout_));
   auto live=std::make_shared<NorthlightTerrainCapture::MeshSnapshot>();
   live->positions={{-2,-2,2},{2,-2,2},{-2,2,2}};live->indices={0,1,2};
   live->bounds.low[0]=live->bounds.low[1]=-2;live->bounds.high[0]=live->bounds.high[1]=2;live->bounds.low[2]=live->bounds.high[2]=2;
   const auto coldExact=[&]{const auto actual=image();w.pointSlot().cacheValid=false;refresh(false);assert(image()==actual);};
   w.frameTerrain={live};assert(w.uploadLiveTerrain());const auto revision=w.liveTerrainRevision;
   refresh(true);assert(w.pointDraws>0&&image()!=ground);coldExact();refresh(true);assert(!w.pointDraws);
   assert(w.uploadLiveTerrain()&&w.terrainUploadReused&&w.liveTerrainRevision==revision);refresh(true);assert(!w.pointDraws);
   auto moved=std::make_shared<NorthlightTerrainCapture::MeshSnapshot>(*live);for(auto& p:moved->positions)p.z=3;
   moved->bounds.low[2]=moved->bounds.high[2]=3;w.frameTerrain={moved};assert(w.uploadLiveTerrain()&&w.liveTerrainRevision!=revision);
   refresh(true);assert(w.pointDraws>0);coldExact();
   w.frameTerrain.clear();assert(w.uploadLiveTerrain());refresh(true);assert(w.pointDraws>0&&image()==ground);coldExact();
   // Geometry, light and failed partial writes must always revoke the proof.
   for(auto& v:vertices)v.position.z+=.25f;for(auto& b:w.batches){b.boundsLow.z+=.25f;b.boundsHigh.z+=.25f;}
   must(page.vb->Lock(0,0,&data,0));std::memcpy(data,vertices,sizeof vertices);must(page.vb->Unlock());++w.meshGeneration;
   refresh(true);assert(w.pointDraws>0&&image()!=ground);coldExact();
   light.position[0]+=.5f;refresh(true);assert(w.pointDraws>0);coldExact();light.position[0]-=.5f;
   w.quality.pointShadowFacesPerFrame=1;
   for(unsigned i=0;i<6;++i)refresh(true);const auto cycle=image();
   w.quality.pointShadowFacesPerFrame=6;refresh(false);assert(image()==cycle);
   w.pointSlot().geometryOnlyFaces=0;meter.failClear=true;must(d->BeginScene());assert(!w.renderPointShadow(light,true,false));must(d->EndScene());
   assert(!w.pointSlot().geometryOnlyFaces&&!w.pointSlot().ready);w.pointSlot().retryAt=0;refresh(true);coldExact();
   for(auto& v:vertices)v.position.z-=.25f;must(page.vb->Lock(0,0,&data,0));std::memcpy(data,vertices,sizeof vertices);must(page.vb->Unlock());
   w.batches=savedBatches;++w.meshGeneration;w.releasePointGPU();
   std::printf("POINT_TERRAIN_REUSE_PASS beforeDraws=%u afterDraws=0 sixFaces=exact liveUpload=exact unchangedUpload=retained meshLightMutation=exact partialCycles=exact failure=revoked\n",groundDraws);
  }
#endif
  w.releasePointGPU();w.localScissorView={};
  NorthlightLocalLightSelection::Selection selection;selection.count=1;selection.sources[0]=light;
  auto pool=[&](bool fresh=true){must(d->BeginScene());w.renderPointShadows(selection,fresh,false);must(d->EndScene());drain();w.pointIndex=0;};
  auto keeps=[&](){unsigned n=0;for(const auto& keep:w.pointTargets.keeps()){n+=keep.slot.cacheValid;assert(!keep.slot.cube);for(auto* face:keep.slot.faces)assert(!face);}return n;};
  pool();const auto original=image();auto* retained=w.pointSlot().cacheFace[5];assert(retained);
  const auto toggle=[&](){auto effects=w.effects;effects.shadows=false;w.setEffects(effects);w.releaseStateCache(true);effects.shadows=true;w.setEffects(effects);assert(w.effects.shadows&&!w.pointReady);};
  toggle();assert(w.pointSlot().cacheValid&&!w.pointSlot().schedule.usable&&!w.pointSlot().ready);
  pool(false);assert(!w.pointSlot().ready&&w.captureDemand);
  pool();assert(w.pointWork.statics==0&&w.pointSlot().cacheFace[5]==retained&&image()==original);
  unsigned staticFaces=0;for(auto* t:w.pointSlot().cacheFace)staticFaces+=t!=nullptr;assert(staticFaces==2);
  std::printf("POINT_LAZY_STATIC_GPU_PASS staticFaces=%u staticKiB=%u oldStaticKiB=1536 cubePixels=exact\n",staticFaces,staticFaces*256);
  selection.count=0;pool();assert(keeps()==1&&!w.pointSlot().cube);
  toggle();assert(keeps()==1);
  selection.count=1;pool(false);assert(w.pointWork.restored==1&&!w.pointSlot().ready&&!w.pointSlot().schedule.usable&&w.captureDemand);
  pool();assert(w.pointWork.statics==0&&w.pointSlot().cacheFace[5]==retained&&image()==original&&w.pointSlot().schedule.complete);
  for(unsigned mutation=0;mutation<7;++mutation){selection.count=0;pool();assert(keeps()==1);
   if(mutation==0){for(auto& b:w.batches)b.boundsLow.z=b.boundsHigh.z+=.25f;for(auto& v:vertices)v.position.z+=.25f;must(page.vb->Lock(0,0,&data,0));std::memcpy(data,vertices,sizeof vertices);must(page.vb->Unlock());++w.meshGeneration;}
   if(mutation==1)selection.sources[0].position[0]+=4.f;
   if(mutation==2){w.setMemoryPressure(true,true);assert(!keeps());w.setMemoryPressure(false,false);}
   if(mutation==3){w.active->map="replacement-map";w.uploadedMap=w.active->map;}
   if(mutation==4){w.invalidateShadowCache();assert(!keeps());}
   if(mutation==5){w.releaseStateCache();assert(!keeps());}
   if(mutation==6){w.failed=true;w.releaseStateCache(true);assert(!keeps());w.failed=false;}
   selection.count=1;pool();assert(w.pointWork.statics>0&&w.pointSlot().ready);const auto changed=image();
   w.releasePointGPU();pool();assert(image()==changed);
  }
  const auto populated=w.batches;w.batches.clear();++w.meshGeneration;pool();
  for(auto* t:w.pointSlot().cacheFace)assert(!t);const auto empty=image();assert(std::all_of(empty.begin(),empty.end(),[](float v){return v==1.f;}));
  selection.count=0;pool();assert(keeps()==1);selection.count=1;pool();assert(w.pointWork.restored==1&&image()==empty);
  w.batches=populated;++w.meshGeneration;pool();assert(w.pointWork.statics>0);
  // Hard limits, zero budget, LRU saturation and device-resource release.
  selection.count=9;for(unsigned i=0;i<9;++i){selection.sources[i]=light;selection.sources[i].sourceId=100+i;}
  pool();selection.count=0;pool();assert(keeps()==8);
  w.quality.pointShadowLimit=1;pool();assert(keeps()<=1);selection.count=1;pool();assert(!keeps());
  w.quality.pointShadowLimit=0;pool();assert(!keeps());for(const auto& slot:w.pointTargets.slots())assert(!slot.cube);
  w.releasePointGPU();assert(!keeps());
  std::puts("POINT_STATIC_KEEP_GPU_PASS unchangedReentry=exact freshDynamic=required geometryLightMapMutation=exact pressureLimitInvalidation=cleared lruMax=8");
  // A replacement source can overwrite the same storage, but no previous face,
  // refresh period, partial cycle or retained static key may be published for it.
  w.quality.pointShadowLimit=3;w.quality.pointShadowFacesPerFrame=1;w.quality.pointShadowRefreshMs=10000;
  selection.count=1;selection.sources[0]=light;selection.sources[0].sourceId=200;selection.sources[0].position[0]=-2;
  pool();auto* recycledCube=w.pointSlot().cube;const unsigned cubeStart=meter.cubes;
  // Changed live geometry must still start a partial refresh; an unchanged
  // complete cube now skips even that cycle.
  ++w.liveTerrainRevision;w.pointSlot().schedule.updatedAt=GetTickCount()-10001;pool();assert(w.pointSlot().faceCycle.active()&&!w.pointSlot().schedule.complete);
  std::vector<std::vector<float>> switched;
  for(unsigned i=0;i<8;++i){selection.sources[0].sourceId=201-i%2;selection.sources[0].position[0]=i%2?-2.f:2.f;w.pointSelection.reset();pool();
#ifndef NORTHLIGHT_POINT_RECYCLE_REFERENCE
   assert(w.pointSlot().cube==recycledCube&&meter.cubes==cubeStart);
#endif
   assert(w.pointSlot().ready&&w.pointSlot().schedule.complete&&!w.pointSlot().faceCycle.active());
   if(i)assert(w.pointWork.restored==1&&w.pointWork.statics==0);
   assert(keeps()==1);switched.push_back(image());
  }
  assert(switched[0]!=switched[1]);const unsigned switchCreates=meter.cubes-cubeStart;
#ifdef NORTHLIGHT_POINT_RECYCLE_REFERENCE
  assert(switchCreates==8);
#else
  assert(switchCreates==0);
#endif
  for(unsigned i=0;i<8;++i){w.releasePointGPU();selection.sources[0].sourceId=201-i%2;selection.sources[0].position[0]=i%2?-2.f:2.f;pool();assert(image()==switched[i]);}
  recycledCube=w.pointSlot().cube;const unsigned staleStart=meter.cubes;
  selection.sources[0].sourceId=202;w.pointSelection.reset();pool(false);
  assert(!w.pointSlot().ready&&!w.pointSlot().schedule.usable&&!w.pointSlot().cacheValid&&!w.pointSlot().staticOnlyFaces&&w.captureDemand);
#ifndef NORTHLIGHT_POINT_RECYCLE_REFERENCE
  assert(w.pointSlot().cube==recycledCube&&meter.cubes==staleStart);
#endif
  pool();assert(w.pointSlot().ready&&w.pointSlot().schedule.complete);
  ++selection.sources[0].sourceId;w.pointSelection.reset();meter.failClear=true;pool();
  assert(!meter.failClear&&!w.pointReady&&!w.pointSlot().ready&&!w.pointSlot().cacheValid&&!w.pointSlot().schedule.usable);
  w.pointSlot().retryAt=0;pool(false);assert(!w.pointSlot().ready);pool();const auto recovered=image();
  w.releasePointGPU();pool();assert(image()==recovered);
  for(unsigned reset=0;reset<2;++reset){if(reset)w.active->map=w.uploadedMap="recycle-map";else w.memoryPressure=true;
   ++selection.sources[0].sourceId;w.pointSelection.reset();const unsigned before=meter.cubes;pool();assert(meter.cubes==before+1);w.memoryPressure=false;}
  w.quality.pointShadowLimit=0;pool();for(const auto& slot:w.pointTargets.slots())assert(!slot.cube);assert(!keeps());w.releasePointGPU();
  w.quality.pointShadowFacesPerFrame=6;
  std::printf("POINT_RECYCLE_GPU_PASS switches=8 cubeCreates=%u sixFaces=coldExact changedLight=1 staticKeeps=1 staleCapture=unpublished partialCycle=reset failedFace=unpublished coldRecovery=exact pressureMapLimit=reset\n",switchCreates);
#ifdef NORTHLIGHT_POINT_PREPARATION_REFERENCE
  // Both pool methods share every resource, selection rule and draw method.
  // Warm preparation must submit no GPU work, even after its refresh is due.
  w.batches.resize(1);w.quality.renderProfile=0;w.quality.pointShadowFacesPerFrame=6;
  for(unsigned count:{0u,1u,4u,16u,64u}){
   w.releasePointGPU();w.quality.pointShadowLimit=std::max(count,1u);selection.count=count;
   for(unsigned i=0;i<count;++i){selection.sources[i]=light;selection.sources[i].sourceId=600+i;}
   pool();assert(w.pointWork.selected==count);
   const auto pixels=count?image():std::vector<float>{};const unsigned cubes=meter.cubes,quads=meter.quads;
   for(unsigned block=0;block<16;++block){const bool current=block%4==1||block%4==2;
    const auto start=std::chrono::steady_clock::now();
    for(unsigned frame=0;frame<256;++frame){
     if(current)w.renderPointShadows(selection,false,false);else w.renderPointShadowsReference(selection,false,false);
    }
    const double cpu=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/256;
    assert(w.pointWork.selected==count&&w.pointIndex==0&&meter.cubes==cubes&&meter.quads==quads);
    assert(w.pointCold.cubes==0&&w.pointWork.statics==0&&w.pointWork.terrain==0&&w.pointWork.replay==0);
    if(count)assert(w.pointSlot().ready&&image()==pixels);
    for(const auto& p:w.pointTargets.slots())if(!p.cube&&p.map.empty()){
     assert(!p.cacheValid&&!p.prewarmed&&!p.retryAt&&!p.schedule.usable);
     for(unsigned f=0;f<6;++f)assert(!p.faces[f]&&!p.cacheFace[f]&&!p.cacheSurface[f]);
    }
    std::printf("POINT_PREPARATION_TIMING lights=%u block=%u current=%u warm=%u frames=256 cpuMs=%.9f\n",count,block,unsigned(current),unsigned(block<4),cpu);
   }
  }
  w.releasePointGPU();w.quality.pointShadowLimit=64;w.quality.pointShadowRefreshMs=0;
  // A cube without a map and a failed mapped slot still require retirement.
  selection.count=0;must(w.pointCubeResource(w.pointTargets.slots()[63]));w.pointTargets.slots()[62].map=w.active->map;w.pointTargets.slots()[62].retryAt=GetTickCount()+1000;
  pool();assert(!w.pointTargets.slots()[63].cube&&w.pointTargets.slots()[62].map.empty()&&!w.pointTargets.slots()[62].retryAt);
  std::puts("POINT_PREPARATION_PASS counts=0,1,4,16,64 warmGpuWork=0 pixels=exact partialCubeAndRetry=retired emptyResources=checked");
#endif
  // Consecutive native model packets can share one exact draw after upload.
  // A strip, emitter exclusion, pose/material/state boundary remains separate.
  w.batches.clear();w.quality.pointShadowLimit=64;w.quality.pointShadowRefreshMs=0;
  for(unsigned i=0;i<16;++i)w.context.inverseView[i]=i%5==0?1.f:0.f;
  const uint32_t shaderSource[]={0xfffe0300,0x0200001f,0x80000000,0x900f0000,0x0200001f,0x80000005,0x900f0001,
   0x0200001f,0x80000000,0xe00f0000,0x0200001f,0x80000005,0xe0030001,
   0x03000009,0xe0010000,0xa0e40002,0x90e40000,0x03000009,0xe0020000,0xa0e40003,0x90e40000,
   0x03000009,0xe0040000,0xa0e40004,0x90e40000,0x03000009,0xe0080000,0xa0e40005,0x90e40000,
   0x02000001,0xe0030001,0x90e40001,0xffff};
  std::vector<uint32_t> patched;assert(NorthlightShadowShader::patch(shaderSource,sizeof shaderSource/4,patched));IDirect3DVertexShader9* shader=nullptr;must(d->CreateVertexShader(reinterpret_cast<const DWORD*>(patched.data()),&shader));
  for(unsigned i=0;i<32;++i){auto p=std::make_unique<WorldRenderer::Replay>();p->shader=shader;shader->AddRef();p->decl=w.shadowPrograms.worldLayout();w.shadowPrograms.worldLayout()->AddRef();p->stream[0]=page.vb;page.vb->AddRef();p->stride[0]=sizeof(NorthlightGI::WorldVertex);p->index=page.ib;page.ib->AddRef();
   p->type=D3DPT_TRIANGLELIST;p->indexed=true;p->base=0;p->min=0;p->vertices=6;p->start=3*i*64;p->count=64;p->projectionKind=2;p->constantUsage.floats={2,4};p->pointBounds.valid=true;
   const float low[]={-1,-1,0},high[]={1,1,9};std::memcpy(p->pointBounds.low,low,12);std::memcpy(p->pointBounds.high,high,12);w.replays.push_back(std::move(p));}
  shader->Release();
#ifdef NORTHLIGHT_POINT_MATERIAL_BOUNDS_TEST
  {
   std::vector<float> reference;unsigned draws[2]{};
   const auto savedBounds=w.replays.front()->pointBounds;
   for(unsigned i=0;i<w.replays.size();++i){
    auto& p=*w.replays[i];p.start=3*(64*i+i%2);p.count=1;
    auto& box=p.materialViewBounds;
    for(unsigned j=0;j<3;++j){
     const auto& pos=vertices[indices[p.start+j]].position;const float xyz[]={pos.x,pos.y,pos.z};
     for(unsigned axis=0;axis<3;++axis){
      box.low[axis]=j?std::min(box.low[axis],xyz[axis]):xyz[axis];
      box.high[axis]=j?std::max(box.high[axis],xyz[axis]):xyz[axis];
     }
    }
    box.valid=true;
   }
   auto renderCold=[&](bool borrow){
    w.replayBoundsBegin();w.replayBoundsResolved=true;w.pointSlot().schedule.invalidate();w.pointWork={};
    for(auto& p:w.replays)p->pointBounds={};
    if(borrow)w.completeMaterialBounds();
    assert(w.pointMaterialBoundsUsed==(borrow?w.replays.size():0));
    assert(w.renderPointShadow(light,true,true));
    return w.pointWork.replay;
   };
   for(unsigned mode=0;mode<2;++mode){
    must(d->BeginScene());draws[mode]=renderCold(mode);must(d->EndScene());drain();
    if(!mode){reference=image();assert(std::any_of(reference.begin(),reference.end(),[](float z){return z<1.f;}));}
    else assert(image()==reference&&draws[1]*6==draws[0]);
   }
   std::printf("POINT_MATERIAL_BOUNDS_PASS faces=6 identical=1 unboundedDraws=%u boundedDraws=%u\n",draws[0],draws[1]);
   NorthlightGpuFrameTimer timer(d);
   constexpr unsigned frames=24;
   for(unsigned block=0;block<16;++block){
    const bool borrow=block%4==1||block%4==2;unsigned submitted=0;
    drain();const auto start=std::chrono::steady_clock::now();
    must(d->BeginScene());const bool timed=timer.begin(block);
    for(unsigned frame=0;frame<frames;++frame)submitted+=renderCold(borrow);
    timer.end();must(d->EndScene());
    const auto elapsed=[&]{return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/frames;};
    const double cpu=elapsed();drain();const double complete=elapsed();double gpu=-1;
    if(timed)timer.poll(gpu);
    assert(submitted==frames*draws[borrow]&&image()==reference);
    std::printf("POINT_MATERIAL_BOUNDS_TIMING block=%u warm=%u borrow=%u cpuMs=%.6f completeMs=%.6f gpuMs=%.6f draws=%u\n",
                block,unsigned(block<4),unsigned(borrow),cpu,complete,gpu<0?-1:gpu/frames,submitted/frames);
   }
   for(unsigned i=0;i<w.replays.size();++i){auto& p=*w.replays[i];p.start=3*i*64;p.count=64;p.pointBounds=savedBounds;p.materialViewBounds={};}
   w.replayBoundsBegin();w.replayBoundsResolved=true;
  }
#endif
  IDirect3DTexture9* cutout=nullptr;must(d->CreateTexture(2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&cutout,nullptr));D3DLOCKED_RECT lock{};must(cutout->LockRect(0,&lock,nullptr,0));
  for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x)reinterpret_cast<DWORD*>(static_cast<char*>(lock.pBits)+y*lock.Pitch)[x]=(x==y)?0xffffffffu:0x00ffffffu;must(cutout->UnlockRect(0));
#ifndef NORTHLIGHT_POINT_TERRAIN_REFERENCE
  {
   auto actors=std::move(w.replays);WorldRenderer::Batch terrain{};terrain.page=0;terrain.count=1;terrain.vertexCount=3;terrain.terrain=true;
   terrain.boundsLow={-16,-16,-16};terrain.boundsHigh={16,16,32};w.batches={terrain};++w.meshGeneration;w.releasePointGPU();
   const auto refresh=[&]{w.replayBoundsBegin();w.replayBoundsResolved=true;for(auto& p:w.replays)p->pointBounds.valid=false;
    w.pointSlot().schedule.updatedAt=GetTickCount()-10000;must(d->BeginScene());assert(w.renderPointShadow(light,true,true));must(d->EndScene());drain();};
   refresh();const auto terrainImage=image();refresh();assert(!w.pointDraws&&image()==terrainImage);
   for(unsigned cycle=0;cycle<2;++cycle){
    w.replays=std::move(actors);for(auto& p:w.replays){p->cutoff=.5f;p->texture=cutout;cutout->AddRef();}
    refresh();assert(w.pointDraws>0&&w.pointSlot().geometryOnlyFaces==0);const auto actorImage=image();
#ifdef NORTHLIGHT_POINT_OVERLAP_TEST
    if(!cycle){
     NorthlightShadowRefreshBudget exhausted(3,std::chrono::milliseconds(5)), unlimited(0,{});
     w.quality.pointShadowRefreshMs=0;
     auto due=[&](const NorthlightShadowRefreshBudget& budget,DWORD extraAge=0){
      const auto period=NorthlightPointShadow::RefreshSchedule::period(w.replays.size());
      w.pointSlot().schedule.updatedAt=GetTickCount()-period-extraAge;
      must(d->BeginScene());assert(w.renderPointShadow(light,true,true,&budget));must(d->EndScene());drain();
      assert(w.pointSlot().ready&&w.pointSlot().schedule.complete);
     };
     due(exhausted);assert(w.pointSlot().budgetHeld&&!w.pointDraws&&image()==actorImage);
     due(exhausted);assert(!w.pointSlot().budgetHeld&&w.pointDraws&&image()==actorImage);
     const auto draws=w.pointDraws;
     due(exhausted,80);assert(!w.pointSlot().budgetHeld&&w.pointDraws==draws);
     due(unlimited);assert(!w.pointSlot().budgetHeld&&w.pointDraws==draws);
     ++w.liveTerrainRevision;due(exhausted);assert(!w.pointSlot().budgetHeld&&w.pointDraws);
     ++w.meshGeneration;due(exhausted);assert(!w.pointSlot().budgetHeld&&w.pointDraws);
     light.position[0]+=.5f;due(exhausted);assert(!w.pointSlot().budgetHeld&&w.pointDraws);
     light.position[0]-=.5f;due(exhausted);assert(!w.pointSlot().budgetHeld&&image()==actorImage);
     const auto originalLight=light;
     light.sourceId=0xfec0000000000001ull;light.kind=2;
     due(exhausted);assert(!w.pointSlot().budgetHeld&&image()==actorImage);
     light.position[0]+=.035f;
     due(exhausted);assert(w.pointSlot().budgetHeld&&!w.pointDraws&&image()==actorImage);
     assert(w.pointSlot().cubeOrigin()[0]==originalLight.position[0]&&w.pointSlot().selected.position[0]==light.position[0]);
     due(exhausted);assert(!w.pointSlot().budgetHeld&&w.pointDraws&&w.pointSlot().cubeOrigin()[0]==light.position[0]);
     const auto scissor=w.localScissorView;
     const float distantView[]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,-100,1},projection[]={1,1,1};
     w.localScissorView=NorthlightLocalLightScissor::view(distantView,projection,.1f,.1f,3840,2160);
     w.quality.pointShadowAdaptiveRefresh=1;
     const auto movedImage=image();const float previousOrigin=light.position[0];
     light.position[0]+=.035f;
     const auto adapt=[&](unsigned frame){
      // Beyond the ordinary motion grace period, inside the bounded adaptive hold.
      w.pointSlot().schedule.updatedAt=GetTickCount()-40;
      must(d->BeginScene());assert(w.renderPointShadow(light,true,true,&unlimited,frame));must(d->EndScene());drain();
     };
     adapt(5);assert(w.pointSlot().budgetHeld&&w.pointAdaptiveMotionHeld==1&&!w.pointDraws&&image()==movedImage);
     assert(w.pointSlot().cubeOrigin()[0]==previousOrigin);
     adapt(7);assert(!w.pointSlot().budgetHeld&&w.pointDraws&&w.pointSlot().cubeOrigin()[0]==light.position[0]);
     const auto adaptiveRefreshDraws=w.pointDraws;
     const auto currentImage=image();adapt(8);assert(!w.pointSlot().budgetHeld&&image()==currentImage);
     std::printf("POINT_MOVING_ADAPTIVE_GPU_PASS heldDraws=0 refreshDraws=%u nextDue=refreshed sixFaces=exact\n",adaptiveRefreshDraws);
     w.localScissorView=scissor;w.quality.pointShadowAdaptiveRefresh=0;
     light=originalLight;
     due(exhausted);assert(!w.pointSlot().budgetHeld&&image()==actorImage);
     std::printf("SHADOW_REFRESH_GPU_PASS fullDraws=%u heldDraws=0 nextDue=refreshed expired=moved=terrain=mesh=immediate sixFaces=exact\n",draws);
    }
#endif
    w.pointSlot().cacheValid=false;refresh();assert(image()==actorImage);
    actors=std::move(w.replays);refresh();assert(w.pointDraws>0&&image()==terrainImage);refresh();assert(!w.pointDraws);
    for(auto& p:actors){p->texture->Release();p->texture=nullptr;}
   }
   w.replays=std::move(actors);w.batches.clear();++w.meshGeneration;w.releasePointGPU();
   std::puts("POINT_TERRAIN_ACTORS_PASS enteringUnknownBounds=redrawn cutout=exact leaving=terrainRestored warm=zeroDraws");
  }
#endif
  unsigned replayBefore=0,replayAfter=0;
  for(unsigned alpha=0;alpha<5;++alpha){w.replayBoundsBegin();w.replayBoundsResolved=true;for(auto& p:w.replays){p->cutoff=alpha?.5f:-1.f;p->alphaMode=0;
   if(alpha>=2){const unsigned formats[]={D3DFMT_A8R8G8B8,D3DFMT_A16B16G16R16F,D3DFMT_A32B32G32R32F};p->alphaMode=NorthlightShadowAlpha::FixedTest|(128u<<8)|(formats[alpha-2]<<18);}
   if(p->texture)p->texture->Release();p->texture=alpha?cutout:nullptr;if(p->texture)p->texture->AddRef();}
   std::vector<float> reference;std::array<unsigned,6> weights{};
   for(unsigned mode=0;mode<2;++mode){w.pointPrimitiveLimit=mode?caps.MaxPrimitiveCount:1;w.pointSlot().cacheValid=false;w.pointSlot().schedule.invalidate();w.pointWork={};const auto quads=meter.quads;must(d->BeginScene());assert(w.renderPointShadow(light,true,true));must(d->EndScene());drain();
#ifdef NORTHLIGHT_POINT_UNION_REFERENCE
    constexpr unsigned expectedUnions=6;
#else
    constexpr unsigned expectedUnions=0;
#endif
    assert(w.pointWork.unions==expectedUnions&&meter.quads-quads==expectedUnions);
    const auto pixels=image();assert(std::any_of(pixels.begin(),pixels.end(),[](float depth){return depth<1.f;}));
    uint64_t hash=1469598103934665603ull;for(const auto byte:std::string(reinterpret_cast<const char*>(pixels.data()),pixels.size()*sizeof(float)))hash=(hash^static_cast<unsigned char>(byte))*1099511628211ull;
    std::printf("POINT_DIRECT_GPU_PASS alpha=%u merged=%u unions=%u imageHash=%llu nonempty=1\n",alpha,mode,expectedUnions,(unsigned long long)hash);
    if(!mode){reference=image();replayBefore=w.pointWork.replay;std::copy_n(w.pointSlot().faceWeights,6,weights.begin());}
    else{assert(image()==reference);replayAfter=w.pointWork.replay;assert(replayAfter<replayBefore/16&&w.pointWork.replayMerged>0);assert(std::equal(weights.begin(),weights.end(),w.pointSlot().faceWeights));}
   }
  }
#ifdef NORTHLIGHT_POINT_OVERLAP_TEST
  for(unsigned i=0;i<w.replays.size();++i)w.replays[i]->constantGroup=i;
  for(unsigned step:{0u,32u}){
   for(unsigned i=0;i<w.replays.size();++i)w.replays[i]->start=3*i*step;
   unsigned draws[2]{};uint64_t tris[2]{};std::vector<float> expected;std::array<unsigned,6> logicalWeights{};
   for(unsigned merge=0;merge<2;++merge){
    w.pointPrimitiveLimit=merge?caps.MaxPrimitiveCount:1;
    w.replayBoundsBegin();w.replayBoundsResolved=true;w.pointSlot().cacheValid=false;w.pointSlot().schedule.invalidate();w.pointWork={};
    must(d->BeginScene());assert(w.renderPointShadow(light,true,true));must(d->EndScene());drain();
    draws[merge]=w.pointWork.replay;tris[merge]=w.pointWork.triangles;
    if(!merge){expected=image();assert(std::any_of(expected.begin(),expected.end(),[](float depth){return depth<1.f;}));std::copy_n(w.pointSlot().faceWeights,6,logicalWeights.begin());}
    else{assert(image()==expected&&std::equal(logicalWeights.begin(),logicalWeights.end(),w.pointSlot().faceWeights));}
   }
   assert(draws[1]*32==draws[0]&&tris[1]<tris[0]&&w.pointWork.crossGroupMerged>0);
   std::printf("POINT_OVERLAP_PASS step=%u beforeDraws=%u afterDraws=%u beforeTriangles=%llu afterTriangles=%llu exactFaces=6\n",step,draws[0],draws[1],(unsigned long long)tris[0],(unsigned long long)tris[1]);
   if(!step){
    NorthlightGpuFrameTimer timer(d);
    for(unsigned block=0;block<16;++block){
     const bool merge=block%4==1||block%4==2;
     w.pointPrimitiveLimit=merge?caps.MaxPrimitiveCount:1;
     w.pointWork={};drain();must(d->BeginScene());const bool timed=timer.begin(block);
     const auto start=std::chrono::steady_clock::now();
     for(unsigned i=0;i<8;++i){
      auto moving=light;moving.position[0]+=.07f*i;
      w.pointSlot().schedule.invalidate();assert(w.renderPointShadow(moving,true,true));
     }
     const double cpu=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
     timer.end();must(d->EndScene());drain();double gpu=-1;if(timed)timer.poll(gpu);
     std::printf("POINT_POSE_RANGE_TIMING block=%u merge=%u cpuMs=%.6f gpuMs=%.6f draws=%u\n",block,unsigned(merge),cpu,gpu,w.pointWork.replay);
    }
   }
  }
  for(unsigned i=0;i<w.replays.size();++i){w.replays[i]->start=3*i*64;w.replays[i]->constantGroup=0;}
  w.replayBoundsBegin();w.replayBoundsResolved=true;w.pointSlot().cacheValid=false;w.pointSlot().schedule.invalidate();
  must(d->BeginScene());assert(w.renderPointShadow(light,true,true));must(d->EndScene());drain();
#endif
  // A shadow toggle retains only static coverage: changed dynamic geometry
  // must produce the same six faces as full invalidation, never the old cube.
  const auto oldDynamic=image();toggle();
  for(auto& v:vertices)v.position.x+=2.f;
  must(page.vb->Lock(0,0,&data,0));std::memcpy(data,vertices,sizeof vertices);must(page.vb->Unlock());
  for(auto& p:w.replays){p->pointBounds.low[0]+=2.f;p->pointBounds.high[0]+=2.f;}
  w.pointWork={};must(d->BeginScene());assert(w.renderPointShadow(light,true,true));must(d->EndScene());drain();
  const auto newDynamic=image();assert(newDynamic!=oldDynamic&&w.pointWork.replay>0&&w.pointWork.statics==0);
  w.invalidateShadowCache();must(d->BeginScene());assert(w.renderPointShadow(light,true,true));must(d->EndScene());drain();assert(image()==newDynamic);
  std::puts("POINT_TOGGLE_GPU_PASS staticRetained=1 keepsRetained=1 staleCaptureDeferred=1 changedDynamic=exact fullInvalidation=exact");
  const std::vector<size_t> adjacent={0,1};
  auto& next=*w.replays[1];const auto& firstReplay=*w.replays[0];
  for(unsigned reason=0;reason<10;++reason){
   // Group IDs must agree with the captured pose and register ranges.
   if(reason==1||reason==6||reason==9)++next.constantGroup;
   if(reason==9){w.replays[0]->constantUsage.floats={2,5};next.constantUsage.floats={2,5};replayFixtureFloats(next)[24]=1.f;}
   if(reason==2)++next.stride[0];if(reason==3)++next.start;if(reason==4)next.type=D3DPT_TRIANGLESTRIP;
   if(reason==5)next.cutoff=-1;if(reason==6)next.constantUsage.booleans={0,1};if(reason==7)next.offset[0]=4;
   if(reason==8){next.min=UINT_MAX;next.vertices=2;}
   size_t at=0;w.pointPrimitiveLimit=caps.MaxPrimitiveCount;const auto run=w.pointReplayRun(adjacent,at,false);
#ifdef NORTHLIGHT_POINT_GROUP_REFERENCE
   const bool merged=reason==0;
#else
   const bool merged=reason<=1;
#endif
   assert(at==(merged?2u:1u)&&run.count==(merged?128u:64u));
   if(reason==9){w.replays[0]->constantUsage.floats={2,4};next.constantUsage.floats={2,4};replayFixtureFloats(next)[24]=0.f;}
   next.constantGroup=firstReplay.constantGroup;next.stride[0]=firstReplay.stride[0];next.start=192;next.type=firstReplay.type;next.cutoff=firstReplay.cutoff;next.constantUsage=firstReplay.constantUsage;next.offset[0]=0;next.min=0;next.vertices=6;
  }
  w.replays.clear();cutout->Release();
  std::printf("POINT_REPLAY_COALESCE_GPU_PASS opaqueCutout=exact faces=6 beforeDraws=%u afterDraws=%u poseStateStripOverflow=barriers faceWeights=unchanged\n",replayBefore,replayAfter);
  // Camera rotation exposes many cold immutable casters at once. Completed
  // source envelopes must cull them this frame, without changing any cube texel.
  std::vector<uint32_t> coldSource(std::begin(shaderSource),std::end(shaderSource));
  const auto projection=std::find(coldSource.begin(),coldSource.end(),0x03000009u);assert(projection!=coldSource.end());
  const size_t firstProjection=size_t(projection-coldSource.begin());
  coldSource.insert(coldSource.begin()+firstProjection,{0x02000001,0x800f0000,0x90e40000});
  for(size_t i=firstProjection+3;i+3<coldSource.size();++i)if(coldSource[i]==0x03000009u)coldSource[i+3]=0x80e40000;
  auto program=std::make_shared<NorthlightActorDeformation::Program>();assert(NorthlightActorDeformation::compile(coldSource.data(),coldSource.size(),*program));
  IDirect3DVertexShader9* coldOriginal=nullptr;must(d->CreateVertexShader(reinterpret_cast<const DWORD*>(coldSource.data()),&coldOriginal));
  assert(NorthlightShadowShader::patch(coldSource.data(),coldSource.size(),patched));must(d->CreateVertexShader(reinterpret_cast<const DWORD*>(patched.data()),&shader));w.actorPrograms[coldOriginal]=program;
  for(unsigned scenario=0;scenario<3;++scenario){
  const unsigned coldPackets=scenario==0?171:scenario==1?24:40,sourceVertices=scenario?513:3;std::vector<NorthlightGI::WorldVertex> coldVertices;
  for(unsigned i=0;i<coldPackets;++i)for(unsigned k=0;k<sourceVertices;++k){const float x=i?1000.f+float(i):0.f;coldVertices.push_back({{x+(k%3==1?1.f:-1.f),k%3==2?1.f:-1.f,0},{0,0,1},float(k%3==1),float(k%3==2)});}
  IDirect3DVertexBuffer9* coldVB=nullptr;must(d->CreateVertexBuffer(UINT(coldVertices.size()*sizeof(coldVertices[0])),0,0,D3DPOOL_MANAGED,&coldVB,nullptr));must(coldVB->Lock(0,0,&data,0));std::memcpy(data,coldVertices.data(),coldVertices.size()*sizeof(coldVertices[0]));must(coldVB->Unlock());
  IDirect3DIndexBuffer9* coldIB=nullptr;std::vector<WORD> coldIndices(sourceVertices);for(unsigned k=0;k<sourceVertices;++k)coldIndices[k]=WORD(k);must(d->CreateIndexBuffer(UINT(coldIndices.size()*2),0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&coldIB,nullptr));must(coldIB->Lock(0,0,&data,0));std::memcpy(data,coldIndices.data(),coldIndices.size()*2);must(coldIB->Unlock());
  for(unsigned i=0;i<coldPackets;++i){auto p=std::make_unique<WorldRenderer::Replay>();auto source=std::make_shared<NorthlightDrawSnapshot::Mesh>();source->sourceIdentity=NorthlightSourceIdentity::make({0x525354u,scenario,i});source->vertexCount=sourceVertices;source->primitiveCount=sourceVertices/3;source->indexed=true;source->indices.assign(coldIndices.begin(),coldIndices.end());source->streams[0].stride=sizeof(coldVertices[0]);source->streams[0].bytes.resize(sourceVertices*sizeof(coldVertices[0]));std::memcpy(source->streams[0].bytes.data(),coldVertices.data()+sourceVertices*i,source->streams[0].bytes.size());p->shared=source;
   p->originalShader=coldOriginal;coldOriginal->AddRef();p->shader=shader;shader->AddRef();p->decl=w.shadowPrograms.worldLayout();w.shadowPrograms.worldLayout()->AddRef();p->stream[0]=coldVB;coldVB->AddRef();p->stride[0]=sizeof(coldVertices[0]);p->index=coldIB;coldIB->AddRef();p->type=D3DPT_TRIANGLELIST;p->indexed=true;p->base=INT(i*sourceVertices);p->min=0;p->vertices=sourceVertices;p->start=0;p->count=sourceVertices/3;p->projectionKind=2;p->constantUsage.floats={2,4};p->cutoff=-1;w.replays.push_back(std::move(p));}
  coldVB->Release();coldIB->Release();w.pointPrimitiveLimit=caps.MaxPrimitiveCount;w.pointSlot().cacheValid=false;
  std::vector<float> coldImage;unsigned coldBefore=0,coldAfter=0;
  for(unsigned mode=0;mode<2;++mode){w.pointWork={};if(!mode)unboundedReference();if(mode){
   const char* concurrent=std::getenv("NORTHLIGHT_TEST_ALLOW_RUNNING_GAME");const unsigned attempts=concurrent&&std::strcmp(concurrent,"1")==0?8:1;
   for(unsigned attempt=0;attempt<attempts;++attempt){
    w.replayBoundsBegin();w.pointCalculateReplayBounds();std::printf("POINT_COLD_BOUNDS scenario=%u attempt=%u valid=%u expected=%u vertices=%zu operations=%zu\n",scenario,attempt,w.pointBoundsValid,coldPackets,w.pointBoundVertices,w.pointBoundOperations);
    if(w.pointBoundsValid==coldPackets)break;
    // Concurrent game/driver compilation can consume the wall-clock budget.
    // In that correctness-only mode, a deferred bound must still draw exactly.
    if(attempts>1){w.pointSlot().schedule.invalidate();must(d->BeginScene());assert(w.renderPointShadow(light,true,true));must(d->EndScene());drain();assert(image()==coldImage);}
   }
   assert(w.pointBoundsValid==coldPackets);w.pointWork={};}
   for(unsigned lightIndex=0;lightIndex<9;++lightIndex){auto target=light;target.sourceId=5000+lightIndex;w.pointSlot().schedule.invalidate();must(d->BeginScene());assert(w.renderPointShadow(target,true,true));must(d->EndScene());drain();
    if(!mode&&lightIndex==0){coldImage=image();assert(std::any_of(coldImage.begin(),coldImage.end(),[](float depth){return depth<.99f;}));}else assert(image()==coldImage);}
   if(!mode)coldBefore=w.pointWork.replay;else coldAfter=w.pointWork.replay;
  }
  assert(coldBefore==coldPackets*9*6&&coldAfter>0&&coldAfter<coldBefore/50);
  // Exercise the production GPU-release boundary after the CPU envelope warmup.
  // Bounds on the packets are deliberately destroyed: only validated sources survive.
  const auto envelopeEntries=w.replayBoundsCache.envelopeEntries();assert(envelopeEntries);
  w.releaseGPU(true);assert(w.replayBoundsCache.envelopeEntries()==envelopeEntries);
  for(auto& p:w.replays){std::weak_ptr<const NorthlightDrawSnapshot::Mesh> old=p->shared;
   p->shared=std::make_shared<NorthlightDrawSnapshot::Mesh>(*p->shared);assert(old.expired());p->pointBounds={};p->boundsPrepared.reset();}
  must(d->CreateVertexDeclaration(elements,&w.shadowPrograms.worldLayout_));must(d->CreatePixelShader(kShadowUnionShader,&w.shadowPrograms.union_));
  w.replayBoundsBegin();w.pointCalculateReplayBounds();assert(w.pointBoundsValid==coldPackets&&!w.pointBoundVertices);
  w.pointWork={};must(d->BeginScene());assert(w.renderPointShadow(light,true,true));must(d->EndScene());drain();assert(image()==coldImage);
  assert(w.pointWork.replay==coldAfter/9);
  // The retained source must evaluate the new camera, not publish old world bounds.
  w.context.inverseView[12]=.5f;w.replayBoundsBegin();w.pointCalculateReplayBounds();assert(w.pointBoundsValid==coldPackets&&!w.pointBoundVertices);
  w.pointSlot().schedule.invalidate();must(d->BeginScene());assert(w.renderPointShadow(light,true,true));must(d->EndScene());drain();
  const auto movedImage=image();assert(movedImage!=coldImage);
  // A cleared cache is the negative control: fail-open rendering stays exact,
  // but the all-six-faces replay amplification returns immediately.
  unboundedReference();w.replayBoundsCache.clear();w.pointWork={};w.pointSlot().schedule.invalidate();
  must(d->BeginScene());assert(w.renderPointShadow(light,true,true));must(d->EndScene());drain();assert(image()==movedImage);
  assert(w.pointWork.replay==coldBefore/9);w.context.inverseView[12]=0;
  std::printf("POINT_RESET_BOUNDS_GPU_PASS packets=%u faces=6 exact=1 retainedEntries=%zu warmDraws=%u clearedDraws=%u packetBoundsDiscarded=1 reconstructedSources=1 changedCamera=exact\n",coldPackets,envelopeEntries,coldAfter/9,coldBefore/9);
  w.replays.clear();
  std::printf("POINT_COLD_COMPLETION_GPU_PASS packets=%u sourceVertices=%u lights=9 faces=6 exact=1 beforeDraws=%u afterDraws=%u unchangedQuality=1\n",coldPackets,sourceVertices,coldBefore,coldAfter);
  }
  // The real four-bone client program, at the two live arrival-wave sizes.
  // Compare every cube texel with conservative unbounded rendering, including
  // partial-source and changed-pose/camera controls.
  std::ifstream skinFile(skinPath,std::ios::binary|std::ios::ate);assert(skinFile&&skinFile.tellg()%4==0);
  std::vector<std::uint32_t> skinWords(size_t(skinFile.tellg())/4);skinFile.seekg(0);skinFile.read(reinterpret_cast<char*>(skinWords.data()),skinWords.size()*4);assert(skinFile);
  auto skinProgram=std::make_shared<NorthlightActorDeformation::Program>();assert(NorthlightActorDeformation::compile(skinWords.data(),skinWords.size(),*skinProgram));assert(NorthlightReplayBounds::SkinEnvelope::supports(*skinProgram));
  IDirect3DVertexShader9* skinOriginal=nullptr;must(d->CreateVertexShader(reinterpret_cast<const DWORD*>(skinWords.data()),&skinOriginal));
  assert(NorthlightShadowShader::patch(skinWords.data(),skinWords.size(),patched));IDirect3DVertexShader9* skinShader=nullptr;must(d->CreateVertexShader(reinterpret_cast<const DWORD*>(patched.data()),&skinShader));w.actorPrograms[skinOriginal]=skinProgram;
#ifdef NORTHLIGHT_POINT_OVERLAP_TEST
  const auto skinUsage=NorthlightShaderConstants::analyze(patched.data(),patched.size(),2);assert(skinUsage.weightedFloats.count);
#endif
  const D3DVERTEXELEMENT9 skinElements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_UBYTE4N,0,D3DDECLUSAGE_BLENDWEIGHT,0},{0,16,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},D3DDECL_END()};
  IDirect3DVertexDeclaration9* skinDecl=nullptr;must(d->CreateVertexDeclaration(skinElements,&skinDecl));
  struct SkinVertex {float at[3];BYTE weights[4],bones[4];};static_assert(sizeof(SkinVertex)==20);
#ifdef NORTHLIGHT_POINT_OVERLAP_TEST
  {
   const SkinVertex verts[]={{{-3,-1,0},{0,63,127,65},{0,1,2,3}},{{-1,-1,0},{0,63,127,65},{0,1,2,3}},{{-2,1,0},{0,63,127,65},{0,1,2,3}},
                             {{1,-1,0},{0,63,127,65},{0,1,2,12}},{{3,-1,0},{0,63,127,65},{0,1,2,12}},{{2,1,0},{0,63,127,65},{0,1,2,12}}};
   const WORD indices[]={0,1,2,3,4,5,3,4,5};
   IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;
   must(d->CreateVertexBuffer(sizeof verts,0,0,D3DPOOL_MANAGED,&vb,nullptr));must(vb->Lock(0,0,&data,0));std::memcpy(data,verts,sizeof verts);must(vb->Unlock());
   must(d->CreateIndexBuffer(sizeof indices,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&ib,nullptr));must(ib->Lock(0,0,&data,0));std::memcpy(data,indices,sizeof indices);must(ib->Unlock());
   for(unsigned i=0;i<2;++i){
    auto p=std::make_unique<WorldRenderer::Replay>();auto source=std::make_shared<NorthlightDrawSnapshot::Mesh>();
    source->vertexCount=3;source->primitiveCount=1;source->indexed=true;source->indices={0,1,2};source->streams[0].stride=sizeof(SkinVertex);
    source->streams[0].bytes.resize(3*sizeof(SkinVertex));std::memcpy(source->streams[0].bytes.data(),verts+3*i,3*sizeof(SkinVertex));p->shared=source;
    p->originalShader=skinOriginal;skinOriginal->AddRef();p->shader=skinShader;skinShader->AddRef();p->decl=skinDecl;skinDecl->AddRef();
    p->stream[0]=vb;vb->AddRef();p->stride[0]=sizeof(SkinVertex);p->index=ib;ib->AddRef();p->type=D3DPT_TRIANGLELIST;p->indexed=true;
    p->min=p->start=3*i;p->vertices=3;p->count=1;p->projectionKind=2;p->constantUsage=skinUsage;p->cutoff=-1;p->gpuCached=true;
    std::fill_n(replayFixtureFloats(*p),4*256,0.f);
    for(unsigned bone=0;bone<75;++bone){replayFixtureFloats(*p)[4*(31+3*bone)]=1;replayFixtureFloats(*p)[4*(32+3*bone)+1]=1;replayFixtureFloats(*p)[4*(33+3*bone)+2]=1;}
    replayFixtureFloats(*p)[4*(33+3*12)+3]=-1.5f;w.replays.push_back(std::move(p));
   }
   vb->Release();ib->Release();w.pointPrimitiveLimit=caps.MaxPrimitiveCount;
   auto& second=*w.replays[1];
   for(unsigned gap:{0u,3u}){
    second.start=3+gap;
    w.replayBoundsBegin();w.pointCalculateReplayBounds();assert(w.pointBoundsValid==2);
    assert(w.replays[0]->boundsWork.weightedPaletteEnd==4&&second.boundsWork.weightedPaletteEnd==13);
    const auto render=[&]{
     const float poison[12]={};must(d->SetVertexShaderConstantF(31+3*12,poison,3));
     w.pointWork={};w.pointCold={};w.pointIndexedPairDraws=0;w.pointSlot().schedule.invalidate();
     must(d->BeginScene());assert(w.renderPointShadow(light,true,true));must(d->EndScene());drain();
     assert(w.pointWork.replayMerged&&bool(w.pointIndexedPairDraws)==bool(gap));
     return image();
    };
    w.replays[0]->constantUsage.weightedFloats={};const auto expected=render();const auto fullBytes=w.pointCold.constantBytes;
    assert(std::any_of(expected.begin(),expected.end(),[](float depth){return depth<1.f;}));
    w.replays[0]->constantUsage.weightedFloats=skinUsage.weightedFloats;
    assert(render()==expected&&w.pointCold.weightedConstantDraws&&w.pointCold.constantBytes<fullBytes);
    const auto compactBytes=w.pointCold.constantBytes;
    second.boundsWork.weightedPaletteEnd=0;
    assert(render()==expected&&!w.pointCold.weightedConstantDraws&&w.pointCold.constantBytes==fullBytes);
    std::printf("POINT_MERGED_SKIN_GPU_PASS gap=%u fullBytes=%llu compactBytes=%llu exactFaces=6\n",gap,(unsigned long long)fullBytes,(unsigned long long)compactBytes);
   }
   w.replays.clear();w.replayBoundsCache.clear();
  }
#endif
  for(unsigned packets:{816u,969u}){
   constexpr unsigned perSource=33;std::vector<SkinVertex> skinVertices;
   for(unsigned i=0;i<packets;++i)for(unsigned n=0;n<perSource;++n)skinVertices.push_back({{(i?1000.f+float(i):0.f)+(n%3==1?1.f:-1.f),n%3==2?1.f:-1.f,0},{0,63,127,65},{0,1,2,3}});
   IDirect3DVertexBuffer9* skinVB=nullptr;must(d->CreateVertexBuffer(UINT(skinVertices.size()*sizeof(SkinVertex)),0,0,D3DPOOL_MANAGED,&skinVB,nullptr));must(skinVB->Lock(0,0,&data,0));std::memcpy(data,skinVertices.data(),skinVertices.size()*sizeof(SkinVertex));must(skinVB->Unlock());
   IDirect3DIndexBuffer9* skinIB=nullptr;WORD skinIndices[perSource];for(unsigned i=0;i<perSource;++i)skinIndices[i]=WORD(i);
   must(d->CreateIndexBuffer(sizeof skinIndices,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&skinIB,nullptr));must(skinIB->Lock(0,0,&data,0));std::memcpy(data,skinIndices,sizeof skinIndices);must(skinIB->Unlock());
   for(unsigned i=0;i<packets;++i){auto p=std::make_unique<WorldRenderer::Replay>();auto source=std::make_shared<NorthlightDrawSnapshot::Mesh>();source->vertexCount=perSource;source->primitiveCount=perSource/3;source->indexed=true;source->indices.assign(std::begin(skinIndices),std::end(skinIndices));source->streams[0].stride=sizeof(SkinVertex);source->streams[0].bytes.resize(perSource*sizeof(SkinVertex));std::memcpy(source->streams[0].bytes.data(),skinVertices.data()+i*perSource,source->streams[0].bytes.size());p->shared=source;
    p->originalShader=skinOriginal;skinOriginal->AddRef();p->shader=skinShader;skinShader->AddRef();p->decl=skinDecl;skinDecl->AddRef();p->stream[0]=skinVB;skinVB->AddRef();p->stride[0]=sizeof(SkinVertex);p->index=skinIB;skinIB->AddRef();p->type=D3DPT_TRIANGLELIST;p->indexed=true;p->base=INT(i*perSource);p->vertices=perSource;p->count=perSource/3;p->projectionKind=2;p->constantUsage.floats={0,256};p->cutoff=-1;
#ifdef NORTHLIGHT_POINT_OVERLAP_TEST
    p->constantUsage=skinUsage;p->gpuCached=true;
#endif
    std::fill_n(replayFixtureFloats(*p),4*256,0.f);for(unsigned bone=0;bone<75;++bone){replayFixtureFloats(*p)[4*(31+3*bone)]=1;replayFixtureFloats(*p)[4*(32+3*bone)+1]=1;replayFixtureFloats(*p)[4*(33+3*bone)+2]=1;}w.replays.push_back(std::move(p));}
   skinVB->Release();skinIB->Release();w.replayBoundsCache.clear();w.pointSlot().cacheValid=false;
   // Seed an interrupted immutable source, which must remain unbounded.
   auto& first=*w.replays[0];const auto prepared=NorthlightReplayBounds::EnvelopeCache::prepareProgram(*skinProgram,skinElements,4);assert(prepared);
   w.replayBoundsCache.beginFrame(false,packets);NorthlightReplayBounds::Budget partial;partial.maxVertices=1;NorthlightReplayBounds::Bounds b;
   assert(w.replayBoundsCache.buildEnclosed(*prepared,*first.shared,first.shared,first.constants,w.context.inverseView,partial,b)==NorthlightReplayBounds::Status::Budget&&!b.valid&&partial.vertices==1);
   unsigned before=0,after=0;
   for(unsigned pose=0;pose<3;++pose){
    const float angle=float(pose)*.71f;w.context.inverseView[0]=w.context.inverseView[5]=std::cos(angle);w.context.inverseView[1]=std::sin(angle);w.context.inverseView[4]=-std::sin(angle);w.context.inverseView[12]=float(pose)*.25f;
    for(auto& p:w.replays){p->pointBounds={};for(unsigned bone=0;bone<4;++bone)replayFixtureFloats(*p)[4*(31+3*bone)+3]=float(pose)*.35f;}
    for(unsigned l=0;l<4;++l){unboundedReference();auto target=light;target.position[0]=float(l)*.25f;target.sourceId=7000+l;w.pointWork={};w.pointSlot().schedule.invalidate();must(d->BeginScene());assert(w.renderPointShadow(target,true,true));must(d->EndScene());drain();const auto expected=image();assert(std::any_of(expected.begin(),expected.end(),[](float v){return v<.99f;}));if(!pose&&!l)before=w.pointWork.replay;
     // A real clock may defer work under driver contention. Every partial
     // result must produce the same image; eventual completion is mandatory.
     for(unsigned attempt=0;attempt<128;++attempt){w.replayBoundsBegin();w.pointCalculateReplayBounds();w.pointWork={};
#ifdef NORTHLIGHT_POINT_OVERLAP_TEST
      w.pointCold={};
#endif
      w.pointSlot().schedule.invalidate();must(d->BeginScene());assert(w.renderPointShadow(target,true,true));must(d->EndScene());drain();assert(image()==expected);if(w.pointBoundsValid==packets)break;assert(attempt<127);}
     assert(w.pointBoundsValid==packets);if(!pose&&!l)after=w.pointWork.replay;
#ifdef NORTHLIGHT_POINT_OVERLAP_TEST
     assert(w.pointCold.weightedConstantDraws);
     if(!pose&&!l){
      const auto compactBytes=w.pointCold.constantBytes;
      for(auto& p:w.replays)p->constantUsage.weightedFloats={};
      w.pointWork={};w.pointCold={};w.pointSlot().schedule.invalidate();must(d->BeginScene());assert(w.renderPointShadow(target,true,true));must(d->EndScene());drain();
      assert(image()==expected&&w.pointWork.replay==after&&compactBytes<w.pointCold.constantBytes);
      for(auto& p:w.replays)p->constantUsage.weightedFloats=skinUsage.weightedFloats;
     }
#endif
     for(auto& p:w.replays)p->pointBounds={};
    }
   }
   assert(before==packets*6&&after>0&&after<before/50);
   std::printf("POINT_SKIN_SOURCE_GPU_PASS packets=%u sourceVertices=33 lights=4 poses=3 faces=6 exact=1 beforeDraws=%u afterDraws=%u partialResume=1\n",packets,before,after);
   w.replays.clear();w.replayBoundsCache.clear();w.context.inverseView[0]=w.context.inverseView[5]=1;w.context.inverseView[1]=w.context.inverseView[4]=w.context.inverseView[12]=0;
  }
  skinShader->Release();skinOriginal->Release();
  // QA's exact six-op single-bone template: 16/21 bone groups used to enter
  // the 70us heavy queue even though they fit the existing 128-operation cap.
  const auto& oneWords=ClientShaders::OneBoneVs3;auto oneProgram=std::make_shared<NorthlightActorDeformation::Program>();
  assert(NorthlightActorDeformation::compile(oneWords,std::size(oneWords),*oneProgram)&&oneProgram->kernel.singleSkin());
  must(d->CreateVertexShader(reinterpret_cast<const DWORD*>(oneWords),&skinOriginal));assert(NorthlightShadowShader::patch(oneWords,std::size(oneWords),patched));must(d->CreateVertexShader(reinterpret_cast<const DWORD*>(patched.data()),&skinShader));w.actorPrograms[skinOriginal]=oneProgram;
  for(unsigned i=0;i<59;++i){const unsigned groups=i%2?16:21;std::vector<SkinVertex> v;
   for(unsigned g=0;g<groups;++g)for(unsigned k=0;k<3;++k)v.push_back({{(i?2000.f+float(i):0.f)+(k==1?1.f:-1.f),k==2?1.f:-1.f,0},{0,0,0,0},{BYTE(g),0,0,0}});
   IDirect3DVertexBuffer9* vb=nullptr;must(d->CreateVertexBuffer(UINT(v.size()*sizeof(SkinVertex)),0,0,D3DPOOL_MANAGED,&vb,nullptr));must(vb->Lock(0,0,&data,0));std::memcpy(data,v.data(),v.size()*sizeof(SkinVertex));must(vb->Unlock());
   std::vector<WORD> ix(v.size());for(unsigned k=0;k<ix.size();++k)ix[k]=WORD(k);IDirect3DIndexBuffer9* ib=nullptr;must(d->CreateIndexBuffer(UINT(ix.size()*2),0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&ib,nullptr));must(ib->Lock(0,0,&data,0));std::memcpy(data,ix.data(),ix.size()*2);must(ib->Unlock());
   auto p=std::make_unique<WorldRenderer::Replay>();auto source=std::make_shared<NorthlightDrawSnapshot::Mesh>();source->vertexCount=UINT(v.size());source->primitiveCount=groups;source->indexed=true;source->indices.assign(ix.begin(),ix.end());source->streams[0].stride=sizeof(SkinVertex);source->streams[0].bytes.resize(v.size()*sizeof(SkinVertex));std::memcpy(source->streams[0].bytes.data(),v.data(),source->streams[0].bytes.size());p->shared=source;
   p->originalShader=skinOriginal;skinOriginal->AddRef();p->shader=skinShader;skinShader->AddRef();p->decl=skinDecl;skinDecl->AddRef();p->stream[0]=vb;p->stride[0]=sizeof(SkinVertex);p->index=ib;p->type=D3DPT_TRIANGLELIST;p->indexed=true;p->vertices=UINT(v.size());p->count=groups;p->projectionKind=2;p->constantUsage.floats={0,256};p->cutoff=-1;
   std::fill_n(replayFixtureFloats(*p),4*256,0.f);for(unsigned bone=0;bone<75;++bone){replayFixtureFloats(*p)[4*(31+3*bone)]=1;replayFixtureFloats(*p)[4*(32+3*bone)+1]=1;replayFixtureFloats(*p)[4*(33+3*bone)+2]=1;}
   const auto prepared=NorthlightReplayBounds::EnvelopeCache::prepareProgram(*oneProgram,skinElements,4);NorthlightReplayBounds::Budget seed;NorthlightReplayBounds::Bounds b;w.replayBoundsCache.beginFrame(false,1595);
   assert(w.replayBoundsCache.calculatePreparedPhase(*prepared,*source,source,p->constants,w.context.inverseView,seed,b,NorthlightReplayBounds::EnvelopeCache::Stage::All)==NorthlightReplayBounds::Status::Valid);w.replays.push_back(std::move(p));
  }
  unsigned unboundedDraws=0,boundedDraws=0;
  for(unsigned pose=0;pose<3;++pose){const float angle=pose*.6f;w.context.inverseView[0]=w.context.inverseView[5]=std::cos(angle);w.context.inverseView[1]=std::sin(angle);w.context.inverseView[4]=-std::sin(angle);
   for(auto& p:w.replays)for(unsigned bone=0;bone<21;++bone)replayFixtureFloats(*p)[4*(31+3*bone)+3]=pose*.2f;
   for(unsigned l=0;l<14;++l){unboundedReference();auto target=light;target.sourceId=8000+l;target.position[0]=float(l)*.01f;for(auto& p:w.replays)p->pointBounds={};w.pointWork={};w.pointSlot().schedule.invalidate();must(d->BeginScene());assert(w.renderPointShadow(target,true,true));must(d->EndScene());drain();const auto expected=image();if(!pose)unboundedDraws+=w.pointWork.replay;
    for(unsigned attempt=0;attempt<128;++attempt){w.replayBoundsBegin();w.pointCalculateReplayBounds();w.pointWork={};w.pointSlot().schedule.invalidate();must(d->BeginScene());assert(w.renderPointShadow(target,true,true));must(d->EndScene());drain();assert(image()==expected);if(w.pointBoundsValid==59)break;assert(attempt<127);}
    assert(w.pointBoundsValid==59&&!w.pointBoundVertices);if(!pose)boundedDraws+=w.pointWork.replay;
   }
  }
  assert(unboundedDraws==59*14*6&&boundedDraws>0&&boundedDraws<unboundedDraws/50);
  std::printf("POINT_SINGLE_BONE_CHEAP_GPU_PASS packets=59 groups=16,21 lights=14 poses=3 faces=6 exact=1 unboundedDraws=%u boundedDraws=%u noSourceBuild=1\n",unboundedDraws,boundedDraws);
  auto proofCaster=std::move(w.replays[0]);w.replays.clear();skinDecl->Release();skinShader->Release();skinOriginal->Release();
  auto staticBatches=std::move(w.batches);w.batches.clear();w.replayBoundsCache.clear();
  for(unsigned i=0;i<16;++i)w.context.inverseView[i]=i%5==0?1.f:0.f;
  w.pointSlot().cacheValid=false;w.pointSlot().schedule.invalidate();
  auto whiteTarget=light;whiteTarget.sourceId=9000;
  const auto refresh=[&]{w.replayBoundsBegin();w.pointSlot().schedule.updatedAt=GetTickCount()-10000;must(d->BeginScene());assert(w.renderPointShadow(whiteTarget,true,true));must(d->EndScene());drain();};
  const auto white=[](const std::vector<float>& pixels){return std::all_of(pixels.begin(),pixels.end(),[](float value){return value==1;});};
  refresh();assert(w.pointCubeIsWhite(w.pointSlot())&&white(image()));
  const auto beforeSkip=w.pointWork.whiteLightingSkipped;
  assert(w.renderPointLighting(nullptr,nullptr,64,64,.1f,1000,0,1,1,1,1,1)&&w.pointWork.whiteLightingSkipped==beforeSkip+1);
  // Every missing proof fails closed, without touching an invalid test handle.
  for(unsigned face=0;face<6;++face){auto& slot=w.pointSlot();auto* saved=slot.faces[face];slot.faces[face]=nullptr;assert(!w.pointCubeIsWhite(slot));slot.faces[face]=saved;
   slot.cacheFace[face]=reinterpret_cast<IDirect3DTexture9*>(uintptr_t(1));assert(!w.pointCubeIsWhite(slot));slot.cacheFace[face]=nullptr;
   slot.cacheSurface[face]=reinterpret_cast<IDirect3DSurface9*>(uintptr_t(1));assert(!w.pointCubeIsWhite(slot));slot.cacheSurface[face]=nullptr;
   slot.staticOnlyFaces&=~(1u<<face);assert(!w.pointCubeIsWhite(slot));slot.staticOnlyFaces=63;}
  w.pointSlot().ready=false;assert(!w.pointCubeIsWhite(w.pointSlot()));w.pointSlot().ready=true;
  w.pointSlot().cacheValid=false;assert(!w.pointCubeIsWhite(w.pointSlot()));w.pointSlot().cacheValid=true;
  ++w.pointSlot().cacheSource;assert(!w.pointCubeIsWhite(w.pointSlot()));--w.pointSlot().cacheSource;
  const auto oldMap=w.pointSlot().map;w.pointSlot().map="wrong";assert(!w.pointCubeIsWhite(w.pointSlot()));w.pointSlot().map=oldMap;
  // A caster enters and leaves during six one-face refreshes. The proof tracks
  // stored content, never the current list or the desired unfinished image.
  w.quality.pointShadowFacesPerFrame=1;w.replays.push_back(std::move(proofCaster));
  for(unsigned frame=0;frame<6;++frame){refresh();if(w.pointCubeIsWhite(w.pointSlot()))assert(white(image()));}
  assert(!white(image())&&!w.pointCubeIsWhite(w.pointSlot()));w.replays.clear();
  for(unsigned frame=0;frame<6;++frame){refresh();if(w.pointCubeIsWhite(w.pointSlot()))assert(white(image()));}
  assert(white(image())&&w.pointCubeIsWhite(w.pointSlot()));
  assert(!w.pointCheck(E_FAIL,"injected white-proof failure")&&!w.pointCubeIsWhite(w.pointSlot()));
  std::puts("POINT_WHITE_CUBE_GPU_PASS sixFaces=exact emptyPassSkipped=1 casterEntryRemoval=1 partialCycles=1 missingProofs=failClosed mapSourceFailure=1");
  w.releaseGPU();assert(!w.replayBoundsCache.envelopeEntries());shader->Release();coldOriginal->Release();
  readback->Release();fence->Release();
 }
};
int main(int argc,char** argv){
 setvbuf(stdout,nullptr,_IONBF,0);
 assert(argc==4);SetEnvironmentVariableA("NORTHLIGHT_QUALITY_FILE",argv[2]);std::wstring path(argv[2],argv[2]+std::strlen(argv[2]));assert(GetFullPathNameW(path.c_str(),MAX_PATH,rootPath,nullptr));
 WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"NorthlightPointStutter";assert(RegisterClassW(&wc));auto hwnd=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(hwnd);
 auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);
 D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=hwnd;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
 IDirect3DDevice9* d=nullptr;must(api->CreateDevice(0,D3DDEVTYPE_HAL,hwnd,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED|D3DCREATE_FPU_PRESERVE,&pp,&d));PointTerrainCoverageTest::run(d,argv[3]);d->Release();api->Release();FreeLibrary(module);DestroyWindow(hwnd);
}
