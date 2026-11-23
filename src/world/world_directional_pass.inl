    void prepareWorldShadowState(){
        NorthlightDirectionalShadow::prepareDevice(d);
    }

    struct DirectionalPassResult {
        NorthlightShadowRefreshBudget::Clock::duration directionalShadowTime{};
        unsigned culledBatches[2]={},drawnBatches[2]={};
        size_t replayConstantBytes=0,replayConstantCalls=0,replayPosePrepared=0,replayPoseReused=0;
    };

#include "world_directional_cache_draw.inl"
#include "world_directional_actor_draw.inl"

    // Geometry is ready and atmosphere work has been submitted. Join shadow-only
    // workers and publish the maps before the point and screen passes consume them.
    bool renderDirectional(const bool (&sourceActive)[2],bool directionalShadows,bool directionalReplayBounds,
                           bool replayShadows,bool actorShadows,bool replaysComplete,bool jobs,
                           NorthlightStreaming::Budget& streamBudget,NorthlightGpuProfile* profile,
                           unsigned rendererFrame,unsigned diagnosticFiles,unsigned shadowDiagnosticCapture,
                           DirectionalPassResult& result,NorthlightLightingCapture::Capture* projectionAudit){
#ifdef NORTHLIGHT_REPLAY_STAND
        static const bool legacyActorInset=[] {
            const char* value=std::getenv("NORTHLIGHT_RETRACE_LEGACY_ACTOR_INSET");
            return value&&value[0]=='1'&&value[1]=='\0';
        }();
        static const bool legacyDirectionalTerrain=[] {
            const char* value=std::getenv("NORTHLIGHT_RETRACE_LEGACY_DIRECTIONAL_TERRAIN");
            return value&&value[0]=='1'&&value[1]=='\0';
        }();
#else
        constexpr bool legacyActorInset=false;
        constexpr bool legacyDirectionalTerrain=false;
#endif
        NorthlightDirectionalShadow::TerrainWork dynamicTerrain;
        DirectionalReplayWork cascadeReplayWork,actorReplayWork;
        const bool legacyReplay=legacyDirectionalReplay();
        unsigned replayFallbacks=0;
        // 0.3.211 (task 31): the colour gate is the lighting weight gate: a source colour is direct*weight, so a zero-weight moon
        // draws no cascade, actor map or water moon map (tests/test_moon_cascade_weight.py).
        // Terrain shadow candidates (the cached terrain chunks the game did not draw live): read by the directional passes' terrain draws.
        NorthlightTerrainCandidates::Selection<> terrainCandidates(directionalTerrainScratch);double terrainPrepareMs=0;
        auto terrainJob=[&]{
            std::chrono::steady_clock::time_point terrainPrepareStart;
            if(captureSampled)terrainPrepareStart=std::chrono::steady_clock::now();
            if(directionalShadows&&(sourceActive[0]||sourceActive[1])){
                // 0.3.175 (S2): this generation's terrain-outside-fixed list, when it belongs to these batches and fixed set.
                if(terrainBatchListGeneration==meshGeneration&&terrainBatchListSize==batches.size()&&terrainListFixed==&fixedTerrainChunks())
                    terrainCandidates.prepareListed(batches,shadowTerrainList,fixedTerrainChunks(),liveTerrainChunks,captureSampled);
                else terrainCandidates.prepare(batches,fixedTerrainChunks(),liveTerrainChunks,captureSampled);}
            if(captureSampled)terrainPrepareMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-terrainPrepareStart).count();
        };
        NorthlightJobs::Counter terrainDone;JobJoin terrainJoin{replayJobs_,terrainDone};
        const bool terrainAsync=jobs&&directionalShadows&&(sourceActive[0]||sourceActive[1]);
        if(terrainAsync)replayJobs_.kick(terrainDone,terrainJob);
        {auto phase=streamingPhases.measure(NorthlightStreaming::PhaseProfile::Static);
         if(!menuDomain)updateStaticCasters(directionalShadows,&streamBudget);
         if(directionalShadows&&replayShadows){auto proofs=streamingPhases.measure(NorthlightStreaming::PhaseProfile::Proofs);prepareStaticProofs(sourceActive[0]||sourceActive[1]);}}
        if(profile)profile->mark("StaticCasterUpload");
        bucket(NorthlightEffectsBuckets::Static);
        streamingCpuPeakMs=std::max(streamingCpuPeakMs,streamBudget.elapsedMs());
        streamingBudgetOverruns+=streamBudget.elapsedMs()>1.0;
        if(captureSampled){logf("WORLD streaming optionalCpuPeakMs=%.3f softBudgetOverruns=%u retiredMaterialMiB=%.2f",streamingCpuPeakMs,streamingBudgetOverruns,double(retiredMaterials.bytes())/1048576);streamingCpuPeakMs=0;streamingBudgetOverruns=0;}
        if(captureSampled){const auto& p=streamingPhases.peaks;
            logf("WORLD streaming phases peakMs admission=%.3f buffers=%.3f copies=%.3f preload=%.3f materials=%.3f commit=%.3f probes=%.3f static=%.3f",p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7]);
            const auto retired=NorthlightStreaming::cpuRetirement().stats();
            logf("WORLD streaming subphases peakMs owners=%.3f staticCache=%.3f proofs=%.3f retire=%.3f terrainUpload=%.3f replayUpload=%.3f retireAccepted=%llu retireOversized=%llu retireRefused=%llu retireParked=%zu retireDeferred=%llu retireSynchronous=%llu retireGenerationFrees=%llu generationDeferred=%u preparedOwners=%u",
                p[8],p[9],p[10],p[11],p[12],p[13],(unsigned long long)retired.accepted,(unsigned long long)retired.oversized,(unsigned long long)retired.refused,
                retirementBacklog.parked(),(unsigned long long)retirementBacklog.deferred,(unsigned long long)retirementBacklog.synchronous,(unsigned long long)retirementBacklog.generationFrees,generationDeferrals.load(),preparedOwnerCommits);
            logf("WORLD streaming splits peakMs replayExpire=%.3f replayBind=%.3f replayCreate=%.3f replayGrow=%.3f replayCopy=%.3f terrainArena=%.3f terrainIndices=%.3f terrainIndexUpload=%.3f staticPublish=%.3f replayCreatedPeak=%u replayTimeDeferred=%llu replayGrowths=%llu replayFallbacks=%llu replayBudgetOverrides=%llu terrainRollovers=%u ownerChangesPeak=%zu generationWaits=%u generationWaitPeakMs=%.3f",
                p[14],p[15],p[16],p[17],p[18],p[19],p[20],p[21],p[22],replayCreatedPeak,(unsigned long long)replayTimeDeferred,(unsigned long long)replayGrowths,(unsigned long long)replayFallbacks,(unsigned long long)replayBudgetOverrides,liveTerrainGPU.rollovers,ownerChangesPeak,generationWaits.load(),generationWaitPeakUs.load()*.001);
            replayCreatedPeak=0;ownerChangesPeak=0;
            streamingPhases.clear();}
        if(captureSampled){const auto& memory=admissionProbe.stats();
            logf("WORLD memory admission requests=%llu witnessHits=%llu fullScans=%llu regionQueries=%llu statusFailures=%llu malformedRegions=%llu fresh=1",
                (unsigned long long)memory.requests,(unsigned long long)memory.witnessHits,(unsigned long long)memory.fullScans,
                (unsigned long long)memory.regionQueries,(unsigned long long)memory.statusFailures,(unsigned long long)memory.malformedRegions);}
        const V pivot=staticPivotReady?staticFramePivot:shadowPivot();
        actorShadowOrigin[0]=pivot.x;actorShadowOrigin[1]=pivot.y;actorShadowOrigin[2]=pivot.z;actorShadowOriginValid=true;
        // Raw pivot above (ActorShadowRadius) and for the static caster request; the cascade frames,
        // their cache placement and the static prebuild use the jump-stable anchor.
        const bool anchorNewMap=cascadeAnchorMap!=lastRequest.map;if(anchorNewMap)cascadeAnchorMap=lastRequest.map; /* the requested map: the snapshot can lag */
        // 0.3.209 (task 29): the actor shadow map is centred on a leashed anchor of the raw pivot (not texel-snapped, not the snapped cascade pivot): the pivot is
        // camera-derived and drifts while the camera orbits an idle player, so the centre holds still within Leash and is dragged along beyond it. Per source; reset on a new map.
        const bool actorMapOn=NorthlightQuality::actorShadowMap(quality,directionalShadows)&&actorMaps.ready(0)&&actorMaps.ready(1);
        float actorFrameMatrix[2][16];bool actorFrameOn[2]={}; /* actorMapOn with a finite matrix: checked here once, not per replay in the near loop */
        for(int source=0;source<2;++source){
            if(anchorNewMap)actorCenterValid[source]=false;
            if(!actorMapOn){actorCenterValid[source]=false;memset(actorFrameMatrix[source],0,64);continue;}
            // The high-detail actor map is redrawn every frame; it need not share the static cache's direction lattice.
            actorCenter[source]=NorthlightActorShadowMap::leash(actorCenter[source],actorCenterValid[source],pivot,rawSourceDirections[source]);actorCenterValid[source]=true;
            NorthlightActorShadowMap::matrix(actorCenter[source],pivot,rawSourceDirections[source],actorFrameMatrix[source]);
            actorFrameOn[source]=NorthlightActorShadowMap::finiteMatrix(actorFrameMatrix[source]);if(!actorFrameOn[source])actorCenterValid[source]=false;
        }
        const uint32_t transitionNow=visualTime();V slotDirections[4];
        const int dominantSource=sourceWeights[1]>sourceWeights[0]?1:0;
        const bool handover=shadowTransitionSource>=0&&shadowTransitionSource!=dominantSource;shadowTransitionSource=dominantSource;
        const V frameCamera=vec(context.camera),travelCamera=frameCamera-V(cascadeAnchor.cameraX,cascadeAnchor.cameraY,cascadeAnchor.cameraZ);
        const bool teleported=cascadeAnchor.valid&&NorthlightGI::dot(travelCamera,travelCamera)>NorthlightCascadeAnchor::ResetDistance*NorthlightCascadeAnchor::ResetDistance;
        const V cascadePivot=cascadeAnchor.update(pivot,frameCamera,transitionNow,anchorNewMap);
        farShadowCoverage=flightCoverage(cascadePivot,lastRequest.map);
        const V farPivot=farShadowCoverage.center;const float farRadius=farShadowCoverage.radius;
        if(memoryPressure||geometryMemoryDemand.load(std::memory_order_relaxed))releaseShadowTransitions();
        for(int source=0;source<2;++source)for(int cascade=0;cascade<2;++cascade){
            const int slot=source*2+cascade;auto& f=shadowTransition[slot];const auto& key=shadowCacheKey[slot];
            const float radius=cascade?farRadius:NorthlightShadowCascades::NearRadius;long ox=0,oy=0;float dz=0;
            const auto sameBasis=NorthlightWorldMath::shadowFrame(cascade?farPivot:cascadePivot,key.direction,radius);
            const bool travel=key.valid&&(!NorthlightWorldMath::cacheOffset(sameBasis,key.frame,ox,oy,dz)||std::labs(ox)>long(ShadowCacheMargin)||std::labs(oy)>long(ShadowCacheMargin));
            const bool jump=key.valid&&different(key.direction,sourceDirections[source],.002f);
            f.state.request({sourceDirections[source].x,sourceDirections[source].y,sourceDirections[source].z},transitionNow,
                anchorNewMap||handover||teleported||travel||jump||!directionalShadows||!sourceActive[source]||!key.valid||!replaysComplete||shadowTransition.failed()||memoryPressure);
            const auto direction=f.state.renderDirection();slotDirections[slot]=V(direction.x,direction.y,direction.z);
        }
        NorthlightWorldMath::ShadowFrame cascadeFrames[2][2];
        for(int source=0;source<2;++source)for(int cascade=0;cascade<2;++cascade){
            const float radius=cascade==0?NorthlightShadowCascades::NearRadius:farRadius;
            cascadeFrames[source][cascade]=NorthlightWorldMath::shadowFrame(cascade?farPivot:cascadePivot,slotDirections[source*2+cascade],radius);
            NorthlightWorldMath::shadowMatrixFrom(cascadeFrames[source][cascade],radius,sourceMatrices[source][cascade]);
        }
        struct TransitionFrame {WorldRenderer* owner;bool complete=false;~TransitionFrame(){if(!complete)owner->resetShadowTransitions();}} transitionFrame{this};
        const uint64_t chunkHash=liveChunkHash();
        // 0.3.152: each drawn slot's cache placement and matrix, once per frame (the
        // cascade loop reuses both; a key changes only in its own slot's iteration).
        NorthlightWorldMath::ShadowCachePlacement slotPlacement[4];
        for(int source=0;source<2;++source)if(directionalShadows&&sourceActive[source])for(int cascade=0;cascade<2;++cascade){
            const int slot=source*2+cascade;const float radius=cascade==0?NorthlightShadowCascades::NearRadius:farRadius;const ShadowCacheKey& key=shadowCacheKey[slot];
            slotPlacement[slot]=NorthlightWorldMath::shadowCachePlacement(cascadeFrames[source][cascade],key.frame,key.valid,
                !different(key.direction,slotDirections[slot],1e-6f),long(ShadowCacheMargin));
            NorthlightWorldMath::shadowMatrixFrom(slotPlacement[slot].frame,radius*float(ShadowCacheSize)/1024.f,staticSlotMatrix[slot]);
        }
        struct StaticPlanJoin {WorldRenderer& r;~StaticPlanJoin(){try{r.staticCasters.settle();}catch(...){}}} staticPlanJoin{*this}; /* before any return below */
        const auto directionalStart=NorthlightShadowRefreshBudget::Clock::now();
        if(directionalShadows){const auto kickStart=std::chrono::steady_clock::now();staticPlanKick(slotPlacement,sourceActive);staticKickMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-kickStart).count();}
        if(directionalReplayBounds)pointCalculateReplayBounds(); // point-only refreshes prepare on demand
        NorthlightDirectionalShadow::GpuPass gpuPass(d,[&](HRESULT hr,const char* operation){return check(hr,operation);},directionalMaps.depth());
        gpuPass.configureCasterSampler();
        bucket(NorthlightEffectsBuckets::ShadowSetup);
        if(!terrainAsync)terrainJob();
        bucket(NorthlightEffectsBuckets::TerrainSelection);
        // 0.3.200 (jobs): the replay culling is kicked once (replayCullKick; 0.3.217: from the first rendering slot, before its static and terrain draws), joined per slot and here.
        bool replayCullsKicked=false,directionalPalettesReady=false;struct CullSettle {WorldRenderer& r;~CullSettle(){r.replayCullSettle();}} cullSettle{*this};
        bool anyShadowCacheRender=false;
        moonFloorShadowCurrent=false;
        ++shadowPasses;
        const bool alignFarShadows=NorthlightFrameParityClock::alignFarShadowPresents();
        if(alignFarShadows){
            static const bool logged=[] {logf("FRAME_PARITY_FAR_SHADOWS aligned=1");return true;}();
            (void)logged;
        }
        if(NorthlightStaticPrebuild::Scheduler::Enabled)for(int source=0;source<2;++source)staticPrebuild.observe(source,directionalShadows&&sourceActive[source],rawSourceDirections[source],GetTickCount());
        for(int source=0;source<2;++source)if(!actorMapOn||!sourceActive[source]){actorValid[source]=false;actorHold[source].invalidate();} /* 0.3.209 (task 29): a source that draws no near map keeps no actor map */
        for(int source=0;source<2;++source){
          if(!directionalShadows||!sourceActive[source])continue;
          auto& matrices=sourceMatrices[source];bool nearRendered=false; /* this source's near map redrawn with fresh replays */
          for(int cascade=0;cascade<2;++cascade){
            const auto phaseStart=std::chrono::steady_clock::now();
            auto phaseNow=[&](){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-phaseStart).count();};
            double selectMs=0,localMs=0,staticMs=0,dynamicMs=0,selectPlanMs=0;const double planBefore=staticCasters.stats().planMs;
            uint64_t staticDraws=0,staticInstances=0,replayDraws=0,replayTriangles=0;
            unsigned localDraws=0;
            const int slot=source*2+cascade;
            ShadowCacheKey& key=shadowCacheKey[slot];
            const auto& placement=slotPlacement[slot]; /* 0.3.152: placed once per frame, before the plan kick */
            long offX=placement.offX,offY=placement.offY;float dz=placement.dz;const char* reason=placement.reason;
            gpuPass.wrapCasterSampler();
            const float* cachedMatrix=staticSlotMatrix[slot];
            if(!reason&&NorthlightLocalShadowSignature::needsRefresh(key.localContentKnown,key.localContent,key.serial,
                bool(uploadedLocalShadowRecords),localShadowSignature(key,cachedMatrix),meshGeneration))reason="local-content";
            bool partialStatic=false;
            // 0.3.151 StaticCacheSlices>1 (static_cache_slices.h): a rect redraw in horizontal bands,
            // one per frame. Any other reason redraws everything now. The key keeps the pre-cycle
            // content until the last band, which takes the rect path below with the cycle's reason.
            auto& slices=staticSlices[slot];const bool wasSlicing=slices.active;if(reason)slices.reset();
            bool sliceBand=false; /* an intermediate band: drawn before the cascade action, without a reason */
            const uint64_t staticNow=reason?0:staticSignature(cachedMatrix);
            const bool staticChanged=!reason&&key.staticSignature!=staticNow;
            if(slices.current(staticNow)&&staticChanged&&!shadowDiagnosticCapture&&key.staticContent.valid&&key.staticContent.instancing==staticCasters.stats().instancing){
                staticDirty=slices.band(); /* unchanged since the cycle (re)started: its next band */
                if(slices.last()){reason=slices.reason;partialStatic=true;}else sliceBand=true;
            }else if(staticChanged){reason="static-models";
                partialStatic=StaticCacheDirtyRects&&key.valid&&staticDirtyRects(key,cachedMatrix);
                if(partialStatic)reason="static-models-partial";else ++shadowCacheStaticFullRenders;
                if(NorthlightStaticSlices::sliceable(quality.staticCacheSlices,partialStatic,shadowDiagnosticCapture!=0,StaticCacheDirtyRectVerify)){
                    staticSliceRestarts+=slices.active;
                    slices.start(staticDirty,quality.staticCacheSlices,reason,staticNow,long(ShadowCacheSize),StaticCacheDirtyTile,StaticCacheDirtyMaxRects);
                    staticDirty=slices.band();if(!slices.last()){reason=nullptr;sliceBand=true;}
                }else if(partialStatic&&slices.active){ /* finish now; drawn bands hold content newer than the key */
                    staticDirtyFootprints=staticDirty;staticDirtyFootprints.insert(staticDirtyFootprints.end(),slices.drawn.begin(),slices.drawn.end());
                    NorthlightShadowBounds::dirtyRects(staticDirtyFootprints,long(ShadowCacheSize),StaticCacheDirtyTile,StaticCacheDirtyMaxRects,staticDirty);}
            }else if(slices.active){ /* content is back at the recorded key: redraw the bands drawn so far */
                NorthlightShadowBounds::dirtyRects(slices.drawn,long(ShadowCacheSize),StaticCacheDirtyTile,StaticCacheDirtyMaxRects,staticDirty);
                if(staticDirty.empty())slices.reset();
                else if(key.staticContent.valid&&key.staticContent.instancing==staticCasters.stats().instancing){reason=slices.reason;partialStatic=true;}
                else{reason="static-models";++shadowCacheStaticFullRenders;}
            }
            selectMs=phaseNow();selectPlanMs=staticCasters.stats().planMs-planBefore;
            if(reason||sliceBand)anyShadowCacheRender=true;
            auto& transition=shadowTransition[slot];const bool directionStep=reason&&!std::strcmp(reason,"direction");
            if(sliceBand||wasSlicing||slices.active||(reason&&!directionStep))transition.state.cancel();
            bool historyReady=false;
            // One cache render: the full clear+redraw, or (partial) the identical
            // ordered sequence limited to the disjoint dirty rects. -1: device failure.
            // `real` false is the debug self-check's full render into a scratch target;
            // `band` an intermediate StaticCacheSlices band (counted as a band, not a partial render).
            double staticStart=0;
            auto renderCache=[&](IDirect3DSurface9* target,bool partial,bool real,bool band=false){
                return drawDirectionalCache(gpuPass,target,cachedMatrix,cascade,result,partial,real,band,[&](unsigned submitted){
                    localDraws=submitted;localMs=phaseNow()-selectMs;staticStart=phaseNow();
                });
            };
            if(sliceBand){
                // Into the cache target only; the cascade's own schedule decides whether this frame's map
                // unites it. Any failure drops the cycle and the key: a full redraw follows.
                const int drawn=renderCache(directionalMaps.cache(slot).surface,true,true,true);if(drawn<0)return false;
                staticMs=phaseNow()-staticStart;staticDraws=staticCasters.stats().drawCalls;staticInstances=staticCasters.stats().instances;
                if(drawn>0){slices.drew();++staticSliceBands;}else{slices.reset();key.valid=false;key.staticContent.valid=false;}
                gpuPass.finishCacheBand();
            }
            // Retain far cascades or the near environment together with their projection.
            // Near replays and the fine actor map still draw from this frame's poses.
            auto& reuse=cascade?farShadow[source]:nearShadow[source];
            const bool splitNear=cascade==0&&actorShadows&&quality.nearShadowInterval>1&&nearEnvironmentResources(source);
            const unsigned interval=cascade?quality.farShadowInterval:(actorShadows&&!splitNear?1:quality.nearShadowInterval);
            // An absolute Present boundary brings independent diagnostic runs
            // back onto the same cadence after a late cache publication.
            // Cache invalidation, age and missing poses retain their own guards.
            const bool alignedRefresh=cascade==1&&alignFarShadows&&NorthlightFrameParityClock::farShadowBoundary(rendererFrame,interval);
            const bool pull=cascade==1&&(alignedRefresh||NorthlightQuality::pullFar(quality.nearShadowInterval,interval,shadowPasses,reuse,nearRendered));
            // 0.3.209 (task 29): a near map drawn with the actor-map skip has no shadow of those casters of its own; once its actor map is gone (the source went
            // inactive, ActorShadowDetail off, a non-finite frame) it is redrawn instead of reused.
            const bool actorLost=cascade==0&&!splitNear&&nearActorSkip[source]&&!actorValid[source];
            const auto action=NorthlightQuality::cascadeAction(reuse,interval,shadowPasses,!reason&&key.valid&&!pull&&!actorLost&&!transition.state.fading(),shadowDiagnosticCapture!=0,replaysComplete);
            // The optional below-roof path cannot consume an old union during
            // a content refresh/defer or an unfinished static slice cycle.
            moonFloorReuseProof(slot,action,reason,staticChanged,sliceBand);
            cascadeActions[slot]=action==NorthlightQuality::CascadeAction::Render?'R':action==NorthlightQuality::CascadeAction::Defer?'D':'U';
            const bool environmentReuse=splitNear&&action!=NorthlightQuality::CascadeAction::Render;
            if(action!=NorthlightQuality::CascadeAction::Render){
                if(action==NorthlightQuality::CascadeAction::Defer){captureDemand=true;++captureDeferrals;}else ++(cascade?farReuses:nearReuses);
                memcpy(matrices[cascade],reuse.matrix,64);
                if(!splitNear){if(profile)profile->mark(source==0?(cascade==0?"SunNear":"SunFar"):(cascade==0?"MoonNear":"MoonFar"));bucket(NorthlightEffectsBuckets::Bucket(NorthlightEffectsBuckets::SunNear+source*2+cascade));continue;}}
            if(!environmentReuse)reuse.begin(interval);
            if(cascade==0)actorValid[source]=nearActorSkip[source]=false;
            if(cascade==0)nearRendered=!environmentReuse&&captureMode==CaptureFresh&&interval>1&&actorShadows;
            // 0.3.217 (jobs): this slot's matrices (set before the loop), the packets and the static proofs are final here; with the bounds job already finished
            // its own cull runs on a worker while the slot draws its static cache and terrain. Not finished: nothing here, the old order (inline test) holds.
            if(jobs&&actorShadows&&!replayCullsKicked)replayCullKickOwn(slot);
            if(NorthlightStaticPrebuild::Scheduler::Enabled&&reason)staticPrebuild.rerender(source,cascade,cachedMatrix,!std::strcmp(reason,"direction"),staticCasters.planReady(cachedMatrix),[&](const float* m){staticCasters.discardPlan(m);});
            if(!environmentReuse&&reason){
                // Static batches only, over the margin extent, keyed for reuse.
                // Reuse the already selected matrix for content-only refreshes.
                // The union below retains its integer offset and depth delta.
                if(directionStep&&transition.state.canPreserveHistory()&&transition.workingValid&&key.valid&&!wasSlicing&&!slices.active&&!shadowTransition.failed()){
                    // Direction has priority in the cache reason. Independently
                    // reject simultaneous content changes before retaining old texels.
                    float oldMatrix[16];NorthlightWorldMath::shadowMatrixFrom(key.frame,(key.frame.texel*512.f)*float(ShadowCacheSize)/1024.f,oldMatrix);
                    const bool localSame=!NorthlightLocalShadowSignature::needsRefresh(key.localContentKnown,key.localContent,key.serial,
                        bool(uploadedLocalShadowRecords),localShadowSignature(key,oldMatrix),meshGeneration);
                    const bool staticSame=key.staticContent.valid&&key.staticContent.instancing==staticCasters.stats().instancing&&key.staticSignature==staticSignature(oldMatrix);
                    if(localSame&&staticSame)historyReady=preserveShadowStatic(slot);else transition.state.cancel();
                    if(profile)profile->mark("ShadowHistory");
                }
                const int rendered=renderCache(directionalMaps.cache(slot).surface,partialStatic,true);if(rendered<0){transition.state.cancel();return false;}
                if(slices.active){staticSliceBands+=partialStatic;slices.reset();} /* the last band (or a full/finishing redraw) completes the cycle */
                const bool staticOK=rendered>0;
                staticMs=phaseNow()-staticStart;staticDraws=staticCasters.stats().drawCalls;staticInstances=staticCasters.stats().instances;
                // A rect redraw relies on the recorded content matching what is
                // in the texture: any doubt (failure, instancing mode flip) forces a full redraw.
                const bool recordable=StaticCacheDirtyRects&&staticOK&&(!partialStatic||key.staticContent.instancing==staticCasters.stats().instancing);
                if(recordable){try {staticCasters.record(cachedMatrix,key.staticContent);}catch(...){key.staticContent.valid=false;}}else key.staticContent.valid=false;
                if(StaticCacheDirtyRectVerify&&partialStatic&&staticOK&&!verifyStaticCache(slot,[&](IDirect3DSurface9* t){return renderCache(t,false,false);}))return false;
                key.valid=staticOK&&(recordable||!StaticCacheDirtyRects);key.frame=placement.frame;key.direction=slotDirections[slot];key.serial=meshGeneration;key.chunkHash=chunkHash;key.staticSignature=staticSignature(cachedMatrix);key.localContent=localShadowSignature(key,cachedMatrix);key.localContentKnown=bool(uploadedLocalShadowRecords);++shadowCacheRenders;
                DWORD tick=GetTickCount();if(NorthlightDiagnostics::enabled()&&(!shadowCacheLogAt||tick-shadowCacheLogAt>=1000)){shadowCacheLogAt=tick;logf("WORLD shadow cache rerender source=%d cascade=%d reason=%s anchorRetained=%u prebuilds=%u prebuildHits=%u prebuildMisses=%u prebuildDiscards=%u rendererFrame=%u tick=%lu",source,cascade,reason,unsigned(!placement.reason),staticPrebuild.stats.built,staticPrebuild.stats.hits,staticPrebuild.stats.misses,staticPrebuild.stats.discarded,rendererFrame,(unsigned long)tick);}
            } else {key.serial=meshGeneration;++shadowCacheReuses;}
            if(diagnosticFiles){
                char name[80];std::snprintf(name,sizeof name,"s%d-c%d-static",source,cascade);
                NorthlightWorldDiagnostics::dump(d,directionalMaps.cache(slot).surface,gpuDiagnosticDirectory,name,diagnosticFiles);
                NorthlightWorldDiagnostics::shadowFrame(gpuDiagnosticDirectory,diagnosticFiles,source,cascade,cachedMatrix,matrices[cascade],offX,offY,dz,key.serial,key.staticSignature,reason);
            }
            // The slot's cache is complete for cachedMatrix now: no intermediate band, no pending cycle, unchanged or just re-rendered, and its content record
            // describes that texture (staticM2 skip, below). A failed render cleared key.valid / staticContent.valid.
            const bool cacheCurrent=key.valid&&key.staticContent.valid&&!sliceBand&&!slices.active&&(reason||!staticChanged)&&staticCasters.recordCurrent(key.staticContent,cachedMatrix);
            const double dynamicStart=phaseNow();
            // Draw this frame's existing terrain/replays directly into the optional
            // current-dynamic map. No in-scene StretchRect or extra geometry pass.
            IDirect3DTexture9* dynamicShadow=(historyReady||transition.state.fading())?transition.dynamic:directionalMaps.scratch().texture;
            IDirect3DSurface9* dynamicSurface=(historyReady||transition.state.fading())?transition.dynamicSurface:directionalMaps.scratch().surface;
            // Dynamic part of this frame: live terrain and replays into the selected map.
            if(!gpuPass.beginCascade(dynamicSurface,{shadowPrograms.worldLayout(),vertices,indices,sizeof(NorthlightGI::WorldVertex)},
                                     matrices[cascade],shadowPrograms.casterVertex(),shadowPrograms.casterPixel()))return false;
            const bool replayAffine=NorthlightShadowBounds::affineLightMatrix(matrices[cascade]);
            if(!environmentReuse){
            // Cached-mesh terrain chunks that the game did not draw live this frame.
            shadowDeviceCaps();
            if(!gpuPass.terrain(shadowPrograms.casterPixel(),pointPrimitiveLimit,legacyDirectionalTerrain,uploadedAlphaCutoffs.data(),materials.data(),dynamicTerrain,
                [&](const auto& submit){
                    if(jobs)replayJobs_.wait(terrainDone);
                    return terrainCandidates.forEach(batches,fixedTerrainChunks(),liveTerrainChunks,[&](const Batch& b){
                        if(replayAffine&&NorthlightShadowBounds::clipRejectAffine(b.boundsLow,b.boundsHigh,matrices[cascade])){
                            ++result.culledBatches[cascade];return true;
                        }
                        ++result.drawnBatches[cascade];
                        return submit(b);
                    });
                },[&](const Batch& batch,UINT& page){return bindMeshPage(batch,page);}))return false;
            if(liveDirectionalIndexCount){
                if(!gpuPass.liveTerrain({shadowPrograms.liveLayout(),liveTerrainGPU.vertices(),liveTerrainIndices.buffer(),sizeof(StaticShadow::Vertex)},
                    shadowPrograms.worldLayout(),liveTerrainGPU.vertexCapacity(),UINT(liveTerrainIndices.base()+liveTerrainIndexCount),UINT(liveDirectionalIndexCount/3),
                    [&]{liveTerrainIndices.touch(fence());}))return false;
            }
            if(diagnosticFiles){
                char name[80];std::snprintf(name,sizeof name,"s%d-c%d-terrain",source,cascade);
                NorthlightWorldDiagnostics::dump(d,dynamicSurface,gpuDiagnosticDirectory,name,diagnosticFiles);
            }
            if(splitNear){
                if(!unionShadow(directionalMaps.nearEnvironment(source).surface,dynamicShadow,directionalMaps.cache(slot).texture,float(long(ShadowCacheMargin)+offX),float(long(ShadowCacheMargin)-offY),dz,ShadowCacheSize))return false;
                // The old static crop still needs current terrain during a fade.
                if(!gpuPass.beginNearReplay(dynamicSurface,historyReady||transition.state.fading()))return false;
            }
            }
            // Original model transforms and bone palette survive; replace view->clip only.
            if(actorShadows){ /* 0.3.158: ActorShadows=0 draws no replays; terrain above and the union below stay */
            replayBoundsJoin(); /* 0.3.143: first pointBounds reader */
            if(!directionalPalettesReady){
                NorthlightReplayPoses::groupDirectionalPalettes(replays);
                directionalPalettesReady=true;
            }
            if(jobs&&!replayCullsKicked){replayCullsKicked=true;replayCullKick(slot,sourceActive);} /* 0.3.200 (jobs): the bounds are final; the later slots; this slot tests inline unless 0.3.217 kicked its own job early */
            const uint8_t* cull=replayCullFor(slot,cachedMatrix,matrices[cascade]); /* null: tested inline below, as before */
            float rows[16];NorthlightWorldMath::replayProjection(context.inverseView,matrices[cascade],rows);
            gpuPass.bindReplayShader(shadowPrograms.replay());
            // Fresh banks after the static pass. Fold the shadow projection into
            // desired constants first; compare only the shader's captured ranges.
            NorthlightShadowReplay::Pass replayPass(d);
            if(captureSampled)replayMeshKeys.clear();const size_t constantCallsBefore=result.replayConstantCalls;
            // 0.3.149 RenderProfile: a profile sample frame splits this loop into own logic / state
            // calls / draws (ReplaySplit, clock reads per span: a secondary number) and records the
            // drawn packets for the capture-waste count; with DiagReplayProbe the sun near map
            // records them for the probe and is timed as a whole. Every other frame (always with
            // RenderProfile=0) runs the Off instantiation, whose hooks are empty: the 0.3.148 loop.
            const bool profiled=profileSampled(),probeRecord=replayProbeActive()&&slot==0&&captureMode==CaptureFresh&&!shadowDiagnosticCapture;
            // 0.3.209 (task 29): the near cascade leaves the replays wholly inside the actor map's inset to the actor map, which draws
            // exactly those (actorReplays): no replay is drawn into both. The list is reserved first, so recording a skip never throws.
            bool nearSkip=cascade==0&&actorFrameOn[source];
            if(nearSkip){actorReplays.clear();try{actorReplays.reserve(replays.size());}catch(...){nearSkip=false;}}
            if(cascade==0)nearActorSkip[source]=nearSkip;
            const NorthlightActorShadowMap::InsetProjection actorInset(nearSkip?actorFrameMatrix[source]:nullptr,!legacyActorInset);
            unsigned nearSkipped=0;
            const bool actorParity=nearSkip&&NorthlightFrameParityClock::fixedShadowBounds();
            const bool sideDiag=nearSkip&&NorthlightDiagnostics::enabled(); /* diagnostics only: the side tracker, the hold counters */
            if(nearSkip)actorHold[source].begin(sideDiag||actorParity,unsigned(frames)+1);else if(cascade==0)actorHold[source].invalidate();
            if(sideDiag)actorSide[source].begin();
            bool groupedReplays=!legacyReplay&&!probeRecord;
            if(groupedReplays){
                directionalReplayCandidates.clear();
                try{directionalReplayCandidates.reserve(replays.size());}
                catch(const std::bad_alloc&){groupedReplays=false;++replayFallbacks;}
            }
            const auto replayBefore=cascadeReplayWork;
            auto replayLoop=[&](auto& split)->bool{
              using NorthlightRenderThreadProbe::Own;
              const auto issue=[&](uint32_t index,const NorthlightReplayDrawState::Range* range,ShadowIndexPair* pair=nullptr,NorthlightShaderConstants::Range uploadFloats={}){
                const auto& p=replays[index];
                split.mark(Own);
                return submitReplay(p.get(),replayPass,rows,matrices[cascade],split,result.replayConstantBytes,result.replayConstantCalls,
                                    [&](HRESULT h,const char* s){return check(h,s);},range,pair,uploadFloats);
              };
              const auto track=[&](uint32_t index){
                const auto& p=replays[index];
                split.drawn(p.get());
                if(captureSampled)try{replayMeshKeys.push_back({p->index,p->stream[0],p->decl,p->offset[0],p->start,p->count,p->base,p->constantGroup});}catch(...){}
              };
              split.start();
              for(size_t index=0;index<replays.size();++index){const auto& p=replays[index];
                // Only full geometric proof plus cached-volume containment can
                // replace a live rigid draw. Dynamic/alpha/unknown draws survive.
                if((!environmentReuse||cacheCurrent)&&key.valid&&(p->staticProofMask&(1u<<slot))&&staticCasters.stats().readyModels&&
                   (cull?(cull[index]&1)!=0:StaticShadow::containsBounds(cachedMatrix,p->staticProofLow,p->staticProofHigh))){
                    // Diagnostic only (no behaviour change): the proved model must be in
                    // this slot's committed cache content. Must stay 0 (same-frame commit).
                    if(key.staticContent.valid&&!key.staticContent.contains(p->staticProofModel,p->staticProofRevision))++staticDedupUncommitted;
                    ++staticDedupSkipped;continue;
                }
                // The actor map takes a caster whose bounds lie fully inside the inset, and keeps a skinned one while they lie within Stay (hysteresis,
                // actor_side_hold.h). Its identity there: the palette root, or the centre of its valid bounds (a rigid attachment has no root).
                bool inActor=false;
                const float* position=p->shadowSkinned?p->position():nullptr; /* once per replay: the hold and the diagnostics share it */
                if(nearSkip){const float* matrix=actorFrameMatrix[source];const float extent=actorInset.extent(p->pointBounds.valid,p->pointBounds.low,p->pointBounds.high,NorthlightActorShadowMap::Stay);
                    if(extent<=NorthlightActorShadowMap::Stay){
                        if(!p->shadowSkinned)inActor=extent<=NorthlightActorShadowMap::Inset;
                        else{const auto model=NorthlightActorSideHold::holdModel(*p);float centre[3];const float* at=position; /* a rigid attachment's model differs from a rooted caster's */
                            if(!p->captureRootValid){
                                if(!at){for(unsigned k=0;k<3;++k)centre[k]=.5f*(p->pointBounds.low[k]+p->pointBounds.high[k]);at=centre;}} /* no position at all: the centre of the valid bounds (finite extent) */
                            inActor=actorHold[source].decide(model,at,extent);
                            if(inActor&&position)actorHold[source].stamp(model,position,matrix,extent);}}
                    else if(p->shadowSkinned&&!p->pointBounds.valid&&p->shared){ /* no bounds this frame: stays in the actor map only by the hold's carry rule */
                        if(position)inActor=actorHold[source].carry(NorthlightActorSideHold::holdModel(*p),position,matrix);}}
                if(inActor){actorReplays.push_back(uint32_t(index));++nearSkipped;
                    if(sideDiag&&p->shadowSkinned)(p->shared?actorSide[source].note(NorthlightActorSide::key(p->shared.get(),p->decl,p->count,position!=nullptr,position),true,p->selfShadow,p->captureRootValid):actorSide[source].unkeyed());continue;}
                if(sideDiag&&p->shadowSkinned){auto& side=actorSide[source];if(p->shared)side.note(NorthlightActorSide::key(p->shared.get(),p->decl,p->count,position!=nullptr,position),false,p->selfShadow,p->captureRootValid,!p->pointBounds.valid);else side.unkeyed();
                    /* no valid bounds, yet the palette root lies inside the inset: it belongs to the actor map and fell to the near cascade for lack of bounds;
                       without a shared snapshot (Unsupported) it can never have bounds: unsupportedNear */
                    if(!p->pointBounds.valid&&p->captureRootValid&&NorthlightActorShadowMap::insideInset(true,p->captureRoot,p->captureRoot,actorFrameMatrix[source])){if(p->shared)side.invalidNear();else side.unsupportedNear();}}
                // A rigid M2 doodad at its resting pose is in this slot's static cache with the same geometry (prepareStaticM2): drawn there, not twice.
                // Hard condition: this slot's cache is current for this frame (rendered or unchanged, no band or rect redraw pending) and its
                // recorded content holds the model at this revision with the replay's box inside the drawn bounds. Otherwise the replay is drawn.
                if(p->staticM2Mask&(1u<<slot)&&p->staticM2Model&&cacheCurrent&&staticCasters.stats().readyModels&&(cull?(cull[index]&1)!=0:StaticShadow::containsBounds(cachedMatrix,p->staticProofLow,p->staticProofHigh))&&
                   key.staticContent.containsBox(*p->staticM2Model,p->staticProofRevision,p->staticProofLow,p->staticProofHigh,StaticM2Dedup::BoundsPad)){
                    ++staticM2Skipped;++replaySlotM2Skipped[slot];continue;
                }
                ++replaySlotTested[slot];replaySlotBounded[slot]+=p->pointBounds.valid;
                if(p->pointBounds.valid&&(cull?(cull[index]&2)!=0:(replayAffine&&NorthlightShadowBounds::clipRejectAffine(V(p->pointBounds.low[0],p->pointBounds.low[1],p->pointBounds.low[2]),V(p->pointBounds.high[0],p->pointBounds.high[1],p->pointBounds.high[2]),matrices[cascade])))){++culledReplayDraws;++replaySlotCulled[slot];continue;}
                if(groupedReplays)directionalReplayCandidates.push_back(uint32_t(index));
                else {
                    if(!issue(uint32_t(index),nullptr))return false;
                    track(uint32_t(index));
                    ++cascadeReplayWork.packets;++cascadeReplayWork.draws;cascadeReplayWork.triangles+=p->count;
                    if(NorthlightShadowAlpha::fixed(p->alphaMode)){++cascadeReplayWork.fixed;++cascadeReplayWork.scopes;}
                }
              }
              if(groupedReplays&&!runDirectionalReplays(directionalReplayCandidates,rows,matrices[cascade],replayPass,cascadeReplayWork,issue,track))return false;
              split.mark(Own);
              return true;
            };
            ReplaySplit split;split.timed=profiled;if(profiled)split.used=&replayProfileUsed;
            if(probeRecord){replayProbeList.clear();split.record=&replayProbeList;}
            const int64_t loopStart=probeRecord?QpcClock::now():0;
            if(profiled||probeRecord){if(!replayLoop(split))return false;}
            else {NorthlightRenderThreadProbe::Off off;if(!replayLoop(off))return false;}
            replayDraws=cascadeReplayWork.draws-replayBefore.draws;
            replayTriangles=cascadeReplayWork.triangles-replayBefore.triangles;
            const double realLoopMs=probeRecord&&captureFrequency.QuadPart>0?double(QpcClock::now()-loopStart)*1000.0/double(captureFrequency.QuadPart):0.0;
            result.replayPosePrepared+=replayPass.posesPrepared();result.replayPoseReused+=replayPass.posesReused();
            if(captureSampled){
                /* meshRepeats: draws whose geometry range+declaration already appeared in this
                   pass (an upper bound for instancing: only stream 0 is keyed, so multi-stream
                   declarations may overcount); exactRepeats also share the pose bank. */
                std::sort(replayMeshKeys.begin(),replayMeshKeys.end());size_t meshRepeats=0,exactRepeats=0;
                for(size_t k=1;k<replayMeshKeys.size();++k)if(replayMeshKeys[k].sameMesh(replayMeshKeys[k-1])){++meshRepeats;exactRepeats+=replayMeshKeys[k].group==replayMeshKeys[k-1].group;}
                const auto& c=replayPass.bindingCounts();
                logf("WORLD replay bindings source=%d cascade=%d draws=%llu meshRepeats=%zu exactRepeats=%zu declarations=%u streams=%u indices=%u shaders=%u textures=%u samplers=%u cutoffs=%u constantCalls=%zu texturesSkipped=%u samplersSkipped=%u opaqueKept=%u opaqueSkip=%d",
                    source,cascade,(unsigned long long)replayDraws,meshRepeats,exactRepeats,c.declarations,c.streams,c.indices,c.shaders,c.textures,c.samplers,c.cutoffs,result.replayConstantCalls-constantCallsBefore,
                    c.legacyTextures-c.textures,c.legacySamplers-c.samplers,c.opaqueKept,int(replayPass.skipping()));
            }
            if(profiled){const auto& c=replayPass.bindingCounts();keepReplaySplit(slot,split,replayDraws,c.declarations+c.streams+c.indices+c.shaders+c.textures+c.samplers+c.cutoffs);}
            // DiagReplayProbe: this map's drawn replays once more into a private target (image
            // unchanged); its GPU time gets its own profile mark in probing windows. Never on a
            // diagnostic capture frame (probeRecord), so it may precede the live dump below.
            if(probeRecord&&!split.recordFailed){const bool marks=profile&&replayProbeMode!=NorthlightRenderThreadProbe::ProbeOff;
                if(marks)profile->mark("SunNearLoop");replayProbe(rows,matrices[cascade],realLoopMs);if(marks)profile->mark("ReplayProbe");}
            if(cascade==0&&actorFrameOn[source]){
                if(profile)profile->mark(source==0?"SunNearReplays":"MoonNearReplays");
                bucket(NorthlightEffectsBuckets::Bucket(NorthlightEffectsBuckets::SunNear+source*2));
                if(!drawDirectionalActors(gpuPass,source,context.inverseView,actorFrameMatrix[source],nearSkip,sideDiag,
                                          nearSkipped,legacyReplay,diagnosticFiles,profile,result,actorReplayWork))return false;
                if(actorParity){
                    const auto matrix=NorthlightShaderTags::fnv1a(actorFrameMatrix[source],sizeof actorFrameMatrix[source]);
                    const auto packets=NorthlightShaderTags::fnv1a(actorReplays.data(),actorReplays.size()*sizeof(std::uint32_t));
                    logf("SHADOW_ACTOR_PARITY rendererFrame=%u source=%d matrix=%016llx packets=%016llx count=%u held=%u carried=%u",
                         rendererFrame,source,static_cast<unsigned long long>(matrix),static_cast<unsigned long long>(packets),
                         nearSkipped,actorHold[source].bandHeld,actorHold[source].carried);
                }
            }
            } /* actorShadows */
            // Union: min(dynamic scratch, cached static at its integer texel offset + exact depth delta).
            if(diagnosticFiles){
                char name[80];std::snprintf(name,sizeof name,"s%d-c%d-live",source,cascade);
                NorthlightWorldDiagnostics::dump(d,dynamicSurface,gpuDiagnosticDirectory,name,diagnosticFiles);
            }
            if(splitNear){
                if(!unionShadow(directionalMaps.current(slot).surface,dynamicShadow,directionalMaps.nearEnvironment(source).texture,0,0,0,1024))return false;
            }else if(!unionShadow(directionalMaps.current(slot).surface,dynamicShadow,directionalMaps.cache(slot).texture,float(long(ShadowCacheMargin)+offX),float(long(ShadowCacheMargin)-offY),dz,ShadowCacheSize))return false;
            if(cascade==1)++fogShadowRevision;
            if(slot==3)moonFloorShadowCurrent=key.valid&&!sliceBand&&!slices.active;
            if(!environmentReuse){
            const V selected=slotDirections[slot];const bool wasFading=transition.state.fading();const uint32_t committedAt=visualTime();
            if(!transition.state.commit({selected.x,selected.y,selected.z},committedAt,historyReady&&key.valid)){
                transition.state.request({selected.x,selected.y,selected.z},committedAt,true);transition.state.commit({selected.x,selected.y,selected.z},committedAt,false);
            }
            if(!wasFading&&transition.state.fading())++shadowTransitionStarts;
            transition.working=cascadeFrames[source][cascade];transition.workingValid=key.valid;
            }
            if(profile)profile->mark(source==0?(cascade==0?"SunNear":"SunFar"):(cascade==0?"MoonNear":"MoonFar"));
            bucket(NorthlightEffectsBuckets::Bucket(NorthlightEffectsBuckets::SunNear+source*2+cascade));
            dynamicMs=phaseNow()-dynamicStart;
            /* Only a frame that ran its capture commits: a skipped frame drew no replays, and a frame
               without any model draw (undecided) must not be reused for N frames either. */
            if(!environmentReuse){if(splitNear||captureMode==CaptureFresh||!actorShadows)reuse.commit(interval,shadowPasses,matrices[cascade]);else captureDemand=true;}
            const double phaseMs=phaseNow();const DWORD phaseTick=GetTickCount();
            if(captureSampled||(NorthlightDiagnostics::enabled()&&phaseMs>4.0&&(!shadowPhaseLogAt||phaseTick-shadowPhaseLogAt>=1000))){
                shadowPhaseLogAt=phaseTick;
                logf("WORLD shadow phase gpuFrame=%llu worldFrame=%u source=%d cascade=%d cache=%s cpuMs=%.3f selectMs=%.3f localMs=%.3f staticMs=%.3f dynamicMs=%.3f localDraws=%u staticDraws=%llu staticInstances=%llu replayDraws=%llu replayTriangles=%llu anchorRetained=%u offsetX=%ld offsetY=%ld depthDelta=%.9f selectPlanMs=%.3f planMs=%.3f",
                    (unsigned long long)(profile?profile->sampledFrame():0),frames,source,cascade,reason?reason:sliceBand?"static-slice":"reuse",phaseMs,selectMs,localMs,staticMs,dynamicMs,localDraws,
                    (unsigned long long)staticDraws,(unsigned long long)staticInstances,(unsigned long long)replayDraws,(unsigned long long)replayTriangles,unsigned(!placement.reason),offX,offY,dz,selectPlanMs,staticCasters.stats().planMs-planBefore);
            }
        }
        }
        transitionFrame.complete=true;
        if(NorthlightStaticPrebuild::Scheduler::Enabled&&directionalShadows)staticPrebuild.frame(cascadePivot,sourceDirections,sourceActive,!anyShadowCacheRender&&!continuousCelestialShadows&&staticScene&&staticCasters.stats().readyModels,GetTickCount(),
            [&](int,int cascade,V direction,float* m){const float radius=cascade==0?NorthlightShadowCascades::NearRadius:farRadius;NorthlightWorldMath::shadowMatrixFrom(NorthlightWorldMath::shadowFrame(cascade?farPivot:cascadePivot,direction,radius),radius*float(ShadowCacheSize)/1024.f,m);},
            [&](const float* m){return staticCasters.planReady(m);},
            [&](const float* m){try {staticCasters.signature(m);return true;}catch(...){return false;}}, /* a failed early build never resets the cache */
            [&](const float* m){staticCasters.discardPlan(m);});
        replayCullSettle();if(jobs)replayJobs_.wait(terrainDone); /* 0.3.200 (jobs): no cascade drew terrain: joined here */
        if(captureSampled){const auto& selection=terrainCandidates.stats();
            logf("WORLD terrain selection scanned=%llu candidates=%zu membershipChecks=%llu passes=%u reusedMembershipChecks=%llu fallbackPasses=%u scratchBytes=%zu prepareMs=%.3f",
                (unsigned long long)selection.scanned,terrainCandidates.candidates(),(unsigned long long)selection.membershipChecks,selection.passes,
                (unsigned long long)selection.reusedMembershipChecks,selection.fallbackPasses,terrainCandidates.bytes(),terrainPrepareMs);
            logf("WORLD directional terrain rendererFrame=%u batches=%u draws=%u materialBinds=%u legacy=%u",
                rendererFrame,dynamicTerrain.batches,dynamicTerrain.draws,dynamicTerrain.materialBinds,unsigned(legacyDirectionalTerrain));
            logf("WORLD directional replay rendererFrame=%u cascadePackets=%u cascadeDraws=%u cascadeTriangles=%llu cascadeFixed=%u cascadeScopes=%u actorPackets=%u actorDraws=%u actorTriangles=%llu actorFixed=%u actorScopes=%u fallbacks=%u legacy=%u legacyActorInset=%u",
                rendererFrame,cascadeReplayWork.packets,cascadeReplayWork.draws,(unsigned long long)cascadeReplayWork.triangles,cascadeReplayWork.fixed,cascadeReplayWork.scopes,
                actorReplayWork.packets,actorReplayWork.draws,(unsigned long long)actorReplayWork.triangles,actorReplayWork.fixed,actorReplayWork.scopes,replayFallbacks,unsigned(legacyReplay),unsigned(legacyActorInset));
            logf("WORLD directional palettes rendererFrame=%u cascadeDraws=%u cascadePackets=%u actorDraws=%u actorPackets=%u",
                rendererFrame,cascadeReplayWork.paletteDraws,cascadeReplayWork.palettePackets,actorReplayWork.paletteDraws,actorReplayWork.palettePackets);
            logf("WORLD directional index pairs rendererFrame=%u cascadeEligible=%u cascadePairs=%u actorEligible=%u actorPairs=%u cascadeBatchEligible=%u cascadeBatches=%u cascadeBatchPackets=%u actorBatchEligible=%u actorBatches=%u actorBatchPackets=%u cacheHits=%u created=%u uploadedBytes=%zu residentBytes=%zu enabled=%u",
                rendererFrame,cascadeReplayWork.pairEligible,cascadeReplayWork.pairDraws,actorReplayWork.pairEligible,actorReplayWork.pairDraws,
                cascadeReplayWork.batchEligible,cascadeReplayWork.batchDraws,cascadeReplayWork.batchPackets,
                actorReplayWork.batchEligible,actorReplayWork.batchDraws,actorReplayWork.batchPackets,
                shadowReplayIndices.hits,shadowReplayIndices.builds,shadowReplayIndices.uploaded(),shadowReplayIndices.bytes(),unsigned(directionalIndexPairsEnabled()));}
        shadowFrameReady=directionalShadows;
        if(directionalShadows)result.directionalShadowTime=NorthlightShadowRefreshBudget::Clock::now()-directionalStart;
        if(NorthlightFrameParityClock::fixedShadowBounds()&&directionalReplayBounds&&replayBoundsResolved){
            std::uint64_t signature=14695981039346656037ull;
            const auto word=[&](std::uint32_t value){signature=(signature^value)*1099511628211ull;};
            for(const auto& packet:replays){const auto& bounds=packet->pointBounds;word(bounds.valid);
                if(bounds.valid){for(float edge:bounds.low){std::uint32_t bits;std::memcpy(&bits,&edge,sizeof bits);word(bits);}
                    for(float edge:bounds.high){std::uint32_t bits;std::memcpy(&bits,&edge,sizeof bits);word(bits);}}}
            logf("SHADOW_BOUNDS_PARITY rendererFrame=%u packets=%zu valid=%u operations=%zu vertices=%zu cascades=%u actors=%u content=%016llx",
                 rendererFrame,replays.size(),pointBoundsValid,pointBoundOperations,pointBoundVertices,
                 cascadeReplayWork.packets,actorReplayWork.packets,static_cast<unsigned long long>(signature));
        }
        if(projectionAudit){
            const unsigned mask=directionalShadows?(unsigned(sourceActive[0])|(unsigned(sourceActive[1])<<1)):0;
            projectionAudit->directionalAnchor(mask,&pivot.x,&cascadePivot.x,&farPivot.x,farRadius,shadowPasses);
            for(unsigned slot=0;slot<4;++slot){
                const unsigned source=slot/2,cascade=slot%2;
                if(!(mask&(1u<<source)))continue;
                const auto& reuse=cascade?farShadow[source]:nearShadow[source];
                const auto& key=shadowCacheKey[slot];
                const auto& placement=slotPlacement[slot];
                float planned[16];
                const float radius=cascade?farRadius:NorthlightShadowCascades::NearRadius;
                NorthlightWorldMath::shadowMatrixFrom(cascadeFrames[source][cascade],radius,planned);
                projectionAudit->directionalProjection(slot,cascadeActions[slot],reuse.valid?reuse.pass:0,planned,
                    sourceMatrices[source][cascade],key.valid?staticSlotMatrix[slot]:nullptr,
                    key.serial,key.staticSignature,placement.offX,placement.offY,placement.dz);
            }
        }
        return true;
    }
