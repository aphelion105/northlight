// Included inside WorldRenderer. Schedule the cube passes and bind their inputs.
    static constexpr UINT PointResolution=NorthlightWorldPointTargets::Size;
    using PointSlot=NorthlightWorldPointTargets::Slot;
    using PointTerrainMemo=NorthlightPointShadow::TerrainMemo;
    NorthlightWorldPointTargets pointTargets;
    unsigned pointIndexedPairDraws=0,pointIndexedBatchDraws=0,pointIndexedBatchPackets=0;
    NorthlightShadowPalettePass shadowPalette;
    NorthlightWorldPointLighting pointLighting;
    NorthlightPointShadow::SelectionTracker pointSelection;std::string pointSelectionMap;
    unsigned pointIndex=0;bool pointReady=false;
    PointSlot& pointSlot(){return pointTargets.slots()[pointIndex];}
    const PointSlot& pointSlot()const{return pointTargets.slots()[pointIndex];}
    bool pointCubeIsWhite(const PointSlot& slot)const{
        if(!active||!slot.ready||!slot.cube||!slot.cacheValid||slot.map!=active->map||
           slot.cacheSource!=slot.selected.sourceId||slot.staticOnlyFaces!=63)return false;
        // A successful static-only refresh cleared empty faces to white on
        // every face. Existing bits describe stored content, including cycles.
        for(unsigned face=0;face<6;++face)if(!slot.faces[face]||slot.cacheFace[face]||slot.cacheSurface[face])return false;
        return true;
    }
    NorthlightPointShadow::OpaqueFaceCache pointOpaqueFaces;
    NorthlightPointShadow::MixedFaceCache pointMixedFaces;
    static bool pointMixedReuseEnabled(){
        static const bool enabled=[]{
#ifdef NORTHLIGHT_REPLAY_STAND
            const char* value=std::getenv("NORTHLIGHT_RETRACE_POINT_MIXED_SUBSET");
#else
            const char* value=std::getenv("NORTHLIGHT_POINT_MIXED_REUSE");
#endif
            return value&&std::strcmp(value,"1")==0;
        }();
        return enabled;
    }
    static bool pointOpaqueReuseEnabled(){
        static const bool enabled=[]{
#ifdef NORTHLIGHT_REPLAY_STAND
            const char* value=std::getenv("NORTHLIGHT_RETRACE_POINT_OPAQUE_REUSE");
#else
            const char* value=std::getenv("NORTHLIGHT_POINT_OPAQUE_REUSE");
#endif
            return value&&value[0]=='1'&&value[1]=='\0';
        }();
        return enabled;
    }
#ifdef NORTHLIGHT_REPLAY_STAND
    static bool pointShadowAuditEnabled(){
        static const bool enabled=[]{
            char value[2]{};
            return GetEnvironmentVariableA("NORTHLIGHT_RETRACE_POINT_SHADOW_AUDIT",value,sizeof value)==1&&value[0]=='1';
        }();
        return enabled;
    }
#endif
    unsigned pointFacesRebuilt=0,pointFacesKept=0,pointFacesReused=0,pointFailures=0,pointDraws=0,pointCulled=0,pointBoundsValid=0;
    unsigned pointMaterialBoundsUsed=0;
    unsigned pointUpdates=0,pointReuses=0,pointReportedLimit=0,pointBatchesMerged=0;
    using PointCandidates=std::array<std::vector<size_t>,6>;
    PointCandidates pointStaticCandidates,pointTerrainCandidates,pointLiveCandidates,pointReplayCandidates;
    std::vector<unsigned char> pointBatchMasks;
    std::vector<unsigned char> pointEmptyTerrainMasks;uint64_t pointEmptyTerrainGeneration=UINT64_MAX;
    std::vector<unsigned char> pointNearbyBatches;bool pointNearbyReady=false;
    // The GPU addresses and palette window are immutable until capture retires.
    // Keep them together for the per-face batching loop.
    using PointPaletteGeometry=std::tuple<uintptr_t,UINT,UINT,INT,uintptr_t,UINT,UINT,unsigned,unsigned>;
#ifdef NORTHLIGHT_REPLAY_STAND
    enum class PointIndexAdmission : unsigned char {Unknown, Rejected, Accepted};
#endif
    struct PointPalettePacket {
        size_t pose;unsigned span;PointPaletteGeometry geometry;
#ifdef NORTHLIGHT_REPLAY_STAND
        PointIndexAdmission indexAdmission=PointIndexAdmission::Unknown;
#endif
    };
    using PointReplaySortKey=std::array<uintptr_t,22>;
    std::vector<size_t> pointReplayOrder;
    std::vector<PointPalettePacket> pointReplayPalette;
    std::vector<PointReplaySortKey> pointReplaySortKeys;
    bool pointReplayOrderReady=false;
    UINT pointPrimitiveLimit=0;bool pointMinBlend=false,pointPairedTargets=false;
    struct PointWork {unsigned selected=0,statics=0,terrain=0,live=0,replay=0,replayMerged=0,crossGroupMerged=0,duplicateInstances=0,unions=0,rebuilt=0,pairedFaces=0,restored=0,unboundedRigid=0,unboundedSkinned=0,whiteLightingSkipped=0,paletteDraws=0,palettePackets=0,paletteRegisters=0,paletteRebased=0,paletteSavedRegisters=0,adaptiveHeld=0;uint64_t triangles=0,bridged=0;} pointWork;
    unsigned pointAdaptiveMotionHeld=0,pointAdaptiveMotionPeakAge=0;
    NorthlightPointShadow::ReplayCasters pointReplayCasters;
    struct PointCold {unsigned cubes=0,statics=0,shared=0,shaders=0,terrainMemoHits=0,terrainMemoMisses=0,orderPackets=0,orderEligible=0,contentPasses=0,alphaScopes=0,fixedDraws=0;
        unsigned replayFaces=0,geometryCalls=0,constantCalls=0,weightedConstantDraws=0;
#ifdef NORTHLIGHT_REPLAY_STAND
        unsigned indexAdmissionTests=0,indexAdmissionHits=0,rigidPoseMerges=0;
#endif
        unsigned opaqueTested=0,opaqueReused=0,opaqueStored=0;
        NorthlightPointShadow::OpaqueFaceCache::Work opaqueSaved;
        unsigned mixedFaces=0,mixedBakedDraws=0,mixedFailures=0;
        NorthlightPointShadow::OpaqueFaceCache::Work mixedSaved;
        uint64_t constantBytes=0;
        unsigned facesWithoutCache=0,facesResized=0,facesMoved=0,facesMeshInvalidated=0;
        unsigned nativeShellChecks=0,placedShellChecks=0,placedCacheable=0,placedShellHits=0,mergeShellBuilds=0,mergeShellHits=0;
        unsigned placedWorldBuilds=0,placedWorldHits=0;
        size_t contentRecords=0,staticEmptyCandidates=0;
        int64_t ticks=0,order=0,prepare=0,boundsWait=0,candidates=0,submit=0,nativeShell=0,placedShell=0,mergeShell=0,placedWorld=0;} pointCold;
    template<class Call> HRESULT pointAllocate(unsigned& attempts,Call call){
        ++attempts;const bool timed=NorthlightQuality::renderProfile(quality);const int64_t start=timed?QpcClock::now():0;
        const HRESULT hr=call();if(timed)pointCold.ticks+=QpcClock::now()-start;return hr;
    }
    size_t pointBoundVertices=0,pointBoundOperations=0;
    struct PointLiveBatch {UINT start,count;V low,high;};std::vector<PointLiveBatch> pointLiveBatches;

    size_t clearPointKeeps(){
        clearPlacedLampMemos();
        pointOpaqueFaces.clear();
        size_t mixedBytes=0;
        pointMixedFaces.visitTextures([&](auto*){mixedBytes+=NorthlightPointShadow::CubeFaceBytes;});
        pointMixedFaces.clear();
        return mixedBytes+pointTargets.keeps().trim()+shadowReplayIndices.clear();
    }
    void keepPointSlot(PointSlot& p,unsigned capacity){
        pointTargets.keeps().retain(p,capacity,active?&active->map:nullptr);
    }
    void restorePointSlot(const NorthlightLocalLights::Light& light){
        pointWork.restored+=pointTargets.keeps().restore(pointSlot(),light.sourceId,active->map);
    }
    void releasePointGPU(bool preserveShaders=false){
        pointOpaqueFaces.clear();
        pointMixedFaces.clear();
        ++nativeCubeRevision;
        if(nativeDirectRenderer)nativeDirectRenderer->reset();nativeDirectBankMemo.reset();nativeDirectScene.clear();nativeDirectFrame=~0u;nativeDirectBindingCount=0;
        pointTargets.resetSlots();
        pointIndex=0;
        pointTargets.keeps().reset();
        shadowReplayIndices.clear();
        pointTargets.releaseShared();
        shadowPalette.reset(preserveShaders);
        if(!preserveShaders)pointTargets.releaseCasterShaders();
        pointLighting.reset(preserveShaders);
        pointReady=false;pointUpdates=pointReuses=pointReportedLimit=pointBatchesMerged=0;localLightTracker.reset();localLightQpc=0;
        pointSelection.reset();pointSelectionMap.clear();
        for(auto* lists:{&pointStaticCandidates,&pointTerrainCandidates,&pointLiveCandidates,&pointReplayCandidates})
            for(auto& list:*lists)std::vector<size_t>().swap(list);
        std::vector<unsigned char>().swap(pointBatchMasks);
        std::vector<unsigned char>().swap(pointEmptyTerrainMasks);
        std::vector<unsigned char>().swap(pointNearbyBatches);pointNearbyReady=false;
        pointReplayCasters.release();pointMeshBounds.release();
        pointLampSources.clear();
        clearPlacedLampMemos();
        for(auto& memo:nativeShellMemo)memo=NativeShellMemo{};
        std::vector<size_t>().swap(pointReplayOrder);std::vector<PointPalettePacket>().swap(pointReplayPalette);pointReplayOrderReady=false;
        std::vector<PointReplaySortKey>().swap(pointReplaySortKeys);
    }
    bool pointCheck(HRESULT hr,const char* operation){
        if(SUCCEEDED(hr))return true;
        pointSlot().invalidateReplayFaces();
        ++nativeCubeRevision;
        pointSlot().schedule.invalidate();pointSlot().cacheValid=false;pointSlot().staticOnlyFaces=pointSlot().geometryOnlyFaces=0;
        pointSlot().ready=false;pointSlot().retryAt=GetTickCount()+1000;
        if(pointFailures++<12)logf("POINT pass skipped: %s hr=%08lx (world effects retained)",operation,(unsigned long)hr);
        return false;
    }
    void prewarmPointShaders(){
        // Give DXVK's compiler the mesh-staging interval; no targets or state
        // are bound. Normal creation/error handling remains in pointResources.
        if(!pointTargets.casterVertex())pointTargets.ensureCasterVertex(d);
        else if(!pointTargets.casterPixel())pointTargets.ensureCasterPixel(d);
        else if(!pointLighting.hardShader())pointLighting.createHard(d);
    }
    void shadowDeviceCaps(){
        if(pointPrimitiveLimit)return;
        D3DCAPS9 caps{};pointPrimitiveLimit=SUCCEEDED(d->GetDeviceCaps(&caps))&&caps.MaxPrimitiveCount?caps.MaxPrimitiveCount:1;
        pointPairedTargets=caps.NumSimultaneousRTs>=2;
        IDirect3D9* api=nullptr;D3DDEVICE_CREATION_PARAMETERS creation{};D3DDISPLAYMODE display{};
        if((caps.PrimitiveMiscCaps&D3DPMISCCAPS_BLENDOP)&&SUCCEEDED(d->GetDirect3D(&api))&&api&&
           SUCCEEDED(d->GetCreationParameters(&creation))&&SUCCEEDED(api->GetAdapterDisplayMode(creation.AdapterOrdinal,&display)))
            pointMinBlend=SUCCEEDED(api->CheckDeviceFormat(creation.AdapterOrdinal,creation.DeviceType,display.Format,D3DUSAGE_RENDERTARGET|D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING,D3DRTYPE_CUBETEXTURE,D3DFMT_R32F));
        if(api)api->Release();
    }
    // Device creation only, before the client can begin a scene. Shader creation
    // alone does not exercise DXVK's static/union pipelines. These three private
    // draws start that first-use work without publishing a cube or waiting for it.
    HRESULT prewarmPointPipelines(bool& kicked,HRESULT& restoreStatus){
        kicked=false;restoreStatus=S_OK;auto& slot=pointTargets.slots()[0];
        if(slot.ready||slot.cacheValid)return D3DERR_INVALIDCALL;
        shadowDeviceCaps();
        HRESULT hr=S_OK;const auto ok=[&](HRESULT value){hr=value;return SUCCEEDED(value);};
        if(!pointTargets.casterVertex()&&!ok(pointTargets.ensureCasterVertex(d)))return hr;
        if(!pointTargets.casterPixel()&&!ok(pointTargets.ensureCasterPixel(d)))return hr;
        if(!ok(shadowPrograms.createUnion(d))||!ok(shadowPrograms.createWorldLayout(d)))return hr;
        if(!pointTargets.scratch()&&!ok(pointTargets.createScratch(d)))return hr;
        if(!pointTargets.scratchSurface()&&!ok(pointTargets.acquireScratchSurface()))return hr;
        if(!pointTargets.depth()&&!ok(pointTargets.createDepth(d)))return hr;
        if(!ok(pointEmptyResource())||!ok(pointCubeResource(slot)))return hr;
        return NorthlightPointPipelineWarmup::submit(d,pointTargets,slot.faces[0],
            {shadowPrograms.worldLayout(),pointTargets.casterVertex(),pointTargets.casterPixel(),shadowPrograms.unionShader()},pointMinBlend,kicked,restoreStatus);
    }
    HRESULT pointEmptyResource(){
        return pointTargets.ensureEmpty(d,[&](const auto& create){return pointAllocate(pointCold.shared,create);});
    }
    HRESULT pointCubeResource(PointSlot& p){
        return pointTargets.ensureCube(d,p,[&](const auto& create){return pointAllocate(pointCold.cubes,create);});
    }
    void prewarmPointResources(const NorthlightLocalLightSelection::Selection& lights,const std::string& sourceMap,NorthlightStreaming::Budget& budget){
        // Prior completed lighting only; prediction must not start fades or publish a cube.
        std::array<NorthlightPointShadow::SelectionTracker::Slot,NorthlightPointShadow::MaxLights> picks{};
        const bool enabled=active&&sourceMap==active->map&&!pointSelection.initialized&&!memoryPressure&&effects.shadows&&quality.pointShadows&&quality.localLights&&quality.localLightLimit;
        if(enabled){auto preview=pointSelection;if(pointSelectionMap!=active->map)preview.reset();
            picks=preview.update(lights.sources.data(),lights.count,context.camera,NorthlightQuality::localShadowLimit(quality),visualTime(),&localScissorView,float(quality.pointShadowDistance));}
        unsigned wanted=0;
        for(unsigned i=0;i<pointTargets.slots().size();++i){auto& p=pointTargets.slots()[i];const auto& pick=picks[i];wanted+=pick.index>=0;
            if(p.prewarmed&&(pick.index<0||p.map!=active->map||p.selected.sourceId!=lights.sources[pick.index].sourceId))p.reset();}
        if(!wanted||!budget.available())return;
        if(!pointTargets.casterVertex()||!pointTargets.casterPixel()||!pointLighting.hardShader()){budget.consume(1);prewarmPointShaders();return;}
        constexpr size_t faceBytes=NorthlightPointShadow::CubeFaceBytes;
        if(!pointTargets.scratch()){if(budget.chunk(faceBytes)!=faceBytes)return;budget.consume(faceBytes);
            pointAllocate(pointCold.shared,[&]{return pointTargets.createScratch(d);});
            if(pointTargets.scratch())pointTargets.acquireScratchSurface();return;}
        if(!pointTargets.scratchSurface()){pointTargets.acquireScratchSurface();return;}
        if(!pointTargets.depth()){if(budget.chunk(faceBytes)!=faceBytes)return;budget.consume(faceBytes);
            pointAllocate(pointCold.shared,[&]{return pointTargets.createDepth(d);});return;}
        for(unsigned i=0;i<pointTargets.slots().size();++i)if(picks[i].index>=0){auto& p=pointTargets.slots()[i];if(p.cube||p.ready||p.cacheValid)continue;
            if(budget.chunk(6*faceBytes)!=6*faceBytes)return;budget.consume(6*faceBytes);
            try{p.map=active->map;}catch(const std::bad_alloc&){return;}
            p.selected=lights.sources[picks[i].index];p.prewarmed=true;
            if(FAILED(pointCubeResource(p)))p.reset(); // Optional failure must not arm the live retry delay.
            return;
        }
    }
    bool pointResources(){
        if(pointSlot().retryAt&&LONG(GetTickCount()-pointSlot().retryAt)<0)return false;
        shadowDeviceCaps();
        if(!pointTargets.scratch()&&!pointCheck(pointAllocate(pointCold.shared,[&]{return pointTargets.createScratch(d);}),"cube scratch"))return false;
        if(!pointTargets.scratch())return pointCheck(E_FAIL,"empty cube scratch");
        if(!pointTargets.scratchSurface()&&!pointCheck(pointTargets.acquireScratchSurface(),"cube scratch surface"))return false;
        if(!pointTargets.depth()&&!pointCheck(pointAllocate(pointCold.shared,[&]{return pointTargets.createDepth(d);}),"cube depth"))return false;
        if(!pointTargets.casterVertex()&&!pointCheck(pointAllocate(pointCold.shaders,[&]{return pointTargets.ensureCasterVertex(d);}),"cube vertex shader"))return false;
        if(!pointTargets.casterPixel()&&!pointCheck(pointAllocate(pointCold.shaders,[&]{return pointTargets.ensureCasterPixel(d);}),"cube pixel shader"))return false;
        if(!pointLighting.hardShader()&&!pointCheck(pointAllocate(pointCold.shaders,[&]{return pointLighting.createHard(d);}),"local lighting shader"))return false;
        if(!pointTargets.depth()||!pointTargets.scratchSurface()||!pointTargets.casterVertex()||!pointTargets.casterPixel()||!pointLighting.hardShader())return pointCheck(E_FAIL,"empty optional resource");
        if(pointSlot().cube){
            bool faces=true;for(auto* face:pointSlot().faces)faces=faces&&face;if(faces){pointSlot().retryAt=0;return true;}
        }
        // Static faces may have survived an offscreen interval without a cube.
        // Complete partial allocations on retry; pointCheck already invalidates their contents.
        if(!pointCheck(pointCubeResource(pointSlot()),"cube resource"))return false;
        pointSlot().retryAt=0;return true;
    }
    bool pointCacheResources(unsigned face){
        return pointTargets.ensureStaticFace(d,pointSlot(),face,
            [&](const auto& create){return pointAllocate(pointCold.statics,create);},
            [&](HRESULT hr,const char* operation){return pointCheck(hr,operation);});
    }
#include "world_point_mesh.inl"
    Batch pointBatchRun(const std::vector<size_t>& candidates,size_t& at,unsigned face=6){
        size_t last=candidates[at++];Batch run=pointMeshBatch(last);uint64_t selected=run.count,bridged=0;
        const auto opaque=[&](unsigned material){return material<materials.size()&&material<uploadedAlphaCutoffs.size()&&uploadedAlphaCutoffs[material]<=0;};
        const auto append=[&](Batch& sum,const Batch& next){
            return NorthlightWorldMeshPages::appendDrawRange(sum,next,pointPrimitiveLimit);
        };
        // Preserve index order and coverage. A gap may bridge only batches
        // proven outside this light/face; excluded or unknown batches stay barriers.
        while(at<candidates.size()){
            const size_t index=candidates[at];const auto& next=pointMeshBatch(index);
            if(index<=last)break;
            // Cached world materials are UNORM: cutoff zero cannot reject any
            // alpha. The draw below canonicalizes these to no alpha sampling.
            if((next.material!=run.material||next.pointOnly!=run.pointOnly)&&!(opaque(run.material)&&opaque(next.material)))break;
            Batch combined=run;uint64_t extra=0;size_t gap=last+1;
            for(;gap<index;++gap){
                const auto& skipped=pointMeshBatch(gap);
                extra+=skipped.count;
                if(bridged+extra>selected+next.count||face>=6||pointBatchMasks.size()!=pointMeshBatchCount()||(pointBatchMasks[gap]&(1u<<face))||!append(combined,skipped))break;
            }
            // ponytail: at most twice the selected triangle work; compact indices
            // are the next step only if sparse, large gaps dominate a real capture.
            if(gap!=index||!append(combined,next))break;
            run=combined;last=index;selected+=next.count;bridged+=extra;++at;++pointBatchesMerged;
        }
        pointWork.bridged+=bridged;
        return run;
    }
    // Content digests of the six faces for `light`; null when the records are unknown.
    // Recomputed only when the records, the mesh generation or the light's cube change.
    const NorthlightLocalShadowSignature::Digest* pointFaceDigests(const NorthlightLocalLights::Light& light){
        const auto* records=uploadedLocalShadowRecords.get();if(!records)return nullptr;
        auto& memo=pointSlot().contentMemo;
        if(!memo.valid||memo.records!=records||memo.generation!=meshGeneration||memo.source!=light.sourceId||memo.range!=light.attenuationEnd||std::memcmp(memo.position,light.position,sizeof memo.position)){
            float matrices[6][16];memo.valid=false;
            for(unsigned face=0;face<6;++face)if(!NorthlightPointShadow::faceMatrix(light.position,.1f,light.attenuationEnd,face,matrices[face],PointResolution))return nullptr;
            ++pointCold.contentPasses;pointCold.contentRecords+=records->size();
            NorthlightPointShadow::faceContent(*records,light,matrices,memo.faces);
            // Shell ownership can depend on parts from several faces. A new
            // mesh generation must recheck the exclusion as a whole.
            if(light.sourceId){
                const bool indexed=pointLampSources.prepare(batches,meshGeneration);
                const auto tagged=pointLampSources.find(light.sourceId);
                const bool hasShell=indexed?tagged.first!=tagged.second:std::any_of(batches.begin(),batches.end(),
                    [&](const Batch& batch){return !batch.terrain&&batch.lampSourceId==light.sourceId;});
                if(hasShell)for(auto& digest:memo.faces)digest.add({{light.sourceId,meshGeneration},0});
            }
            memo.records=records;memo.generation=meshGeneration;memo.source=light.sourceId;memo.range=light.attenuationEnd;
            std::memcpy(memo.position,light.position,sizeof memo.position);memo.valid=true;
        }
        return memo.faces;
    }
    // Static cache faces to redraw for `light`: all six for a new/changed light or an invalid
    // cache, else each face whose content changed (unknown records: the generation rule).
    unsigned pointStaticRebuild(const NorthlightLocalLights::Light& light,bool lightChanged){
        if(!pointSlot().cacheValid||lightChanged||pointSlot().cacheSource!=light.sourceId)return 63;
        // The committed records cannot change inside one mesh generation.
        // Defer their content walk until a publication actually changes it.
        bool sameGeneration=true;for(auto serial:pointSlot().faceSerial)sameGeneration&=serial==meshGeneration;
        if(sameGeneration)return 0;
        return NorthlightPointShadow::staleFaces(pointSlot().faceContent,pointSlot().faceKnown,pointSlot().faceSerial,pointFaceDigests(light),meshGeneration);
    }
    void pointRememberStaticFace(unsigned face,bool lightChanged){
        // Moving lamps redraw all six static faces anyway. Hashing the
        // whole scene for each intermediate position has no consumer.
        // Unknown content remains reusable within this generation; its
        // first generation change conservatively redraws it once.
        const auto* digests=lightChanged?nullptr:pointFaceDigests(pointSlot().selected);
        pointSlot().faceContent[face]=digests?digests[face]:NorthlightLocalShadowSignature::Digest{};
        pointSlot().faceKnown[face]=digests!=nullptr;pointSlot().faceSerial[face]=meshGeneration;
    }
    void pointLogSchedule(bool reused){
        if(NorthlightDiagnostics::enabled()&&(frames==0||frames%120==0)){
            logf("POINT schedule updates=%u reuses=%u reused=%u crowd=%u periodMs=%u replayPackets=%zu draws=%u culled=%u",
                pointUpdates,pointReuses,unsigned(reused),unsigned(pointReplayCount>=256),NorthlightPointShadow::RefreshSchedule::period(pointReplayCount,quality.pointShadowRefreshMs),replays.size(),pointDraws,pointCulled);
            pointUpdates=pointReuses=0;
            if(NorthlightQuality::renderProfile(quality))logf("POINT static faces rebuilt=%u keptByContent=%u mergedBatches=%u worldFrame=%llu",pointFacesRebuilt,pointFacesKept,pointBatchesMerged,(unsigned long long)frames);
            pointFacesRebuilt=pointFacesKept=pointBatchesMerged=0;
        }
    }
#include "world_shadow_bounds.inl"
    // Capture-skip prediction: would renderPointShadow() refresh the cube this frame?
    // Same selection and schedule, no state change; unpredictable counts as due.
    bool pointRefreshPredicted()const{
        if(!effects.shadows||lastRenderDebug!=0||!quality.pointShadows||!quality.localLights||!quality.localLightLimit||!active)return false;
        if(equipmentLightsOn()&&particleLights.hasLights())return true;
        // Conservative: a new lamp or changing selection also needs current-frame casters.
        return NorthlightPointShadow::selectMany(active->localLights.data(),active->localLights.size(),context.camera,NorthlightQuality::localShadowLimit(quality),{},float(quality.pointShadowDistance),true).count!=0||
            NorthlightPointShadow::selectMany(equipmentLights.output.data(),equipmentLights.outputCount,context.camera,NorthlightQuality::localShadowLimit(quality),{},float(quality.pointShadowDistance),true).count!=0;
    }
#include "world_point_lamp_shells.inl"
#include "world_point_replays.inl"

    bool bakePointSubset(IDirect3DSurface9* target,IDirect3DSurface9* staticSurface,const float* matrix,
                         unsigned rendererFrame,unsigned face,bool& stateRestored){
        SavedState saved(d,&stateBlocks);
        if(!saved.ok)return false;
        const auto oldCold=pointCold;
        const auto oldWork=pointWork;
        const auto oldDraws=pointDraws,oldMerged=pointBatchesMerged;
        const auto oldPairs=pointIndexedPairDraws,oldBatches=pointIndexedBatchDraws,oldPackets=pointIndexedBatchPackets;
        const auto check=[](HRESULT hr,const char*){return SUCCEEDED(hr);};
        NorthlightShadowReplay::Pass replayPass(d);
        NorthlightPointShadow::FacePass bake(d,check,PointResolution,pointTargets.depth(),pointTargets.casterPixel());
        bool allowBlend=pointMinBlend;
        const bool ok=bake.beginFace(target,pointSlot().selected.attenuationEnd)&&bake.copyCachedBase(staticSurface)&&
            bake.beginDynamic(nullptr,allowBlend,[]{return true;})&&
            submitPointReplayFace(replayPass,pointMixedFaces.opaque(),matrix,rendererFrame,face,check);
        const unsigned submitted=pointDraws-oldDraws;
        pointCold=oldCold;pointWork=oldWork;pointDraws=oldDraws;pointBatchesMerged=oldMerged;
        pointIndexedPairDraws=oldPairs;pointIndexedBatchDraws=oldBatches;pointIndexedBatchPackets=oldPackets;
        pointCold.mixedBakedDraws+=submitted;
        stateRestored=saved.restore();
        return ok&&stateRestored;
    }

    // fresh=false: model capture was skipped. Reuse a compatible complete cube
    // or defer; publishing a static-only cube would erase character shadows.
    // withReplays=false (0.3.158 ActorShadows=0): static cache, terrain and union only; the
    // cube is complete without replays (fresh=true) and the schedule sees none.
    bool renderPointShadow(const NorthlightLocalLights::Light& light,bool fresh=true,bool withReplays=true,
                           const NorthlightShadowRefreshBudget* budget=nullptr,unsigned rendererFrame=0,PointWorldTriangles* world=nullptr){
        const bool previouslyHeld=std::exchange(pointSlot().budgetHeld,false);
        pointSlot().prewarmed=false;pointSlot().ready=false;pointDraws=pointCulled=pointFacesReused=0;
        // Publication can lead the bounded upload. Its committed same-map
        // pages, materials and meshGeneration remain drawable until the swap.
        if(!active||!vertices||!indices||uploadedMap!=active->map)return false;
        if(pointSlot().map!=active->map){
            pointSlot().schedule.invalidate();pointSlot().cacheValid=false;
            pointSlot().invalidateReplayFaces();
        }
        pointSlot().selected=light;pointSlot().map=active->map;
        // LocalLighting is zero outside this same conservative sphere scissor.
        // Keep the static cache, but require fresh dynamic faces on re-entry.
        long screenArea=0;
        if(NorthlightLocalLightScissor::Enabled&&localScissorView.valid){
            const std::array<NorthlightLocalLightSelection::Constant,1> sphere={{{light.position[0],light.position[1],light.position[2],light.attenuationEnd}}};
            screenArea=NorthlightLocalLightScissor::batchRect(localScissorView,sphere,1).area();
            if(!screenArea){
                pointSlot().schedule.invalidate();pointSlot().faceCycle.reset();return false;
            }
        }
        if(!pointResources())return false;
        // Static-cache keys exclude terrain. Complete cube faces additionally
        // retain its exact committed mesh/live revisions when no replay enters.
        const bool lightChanged=!NorthlightPointShadow::sameCube(pointSlot().schedule.light,light);
        const unsigned rebuildMask=pointStaticRebuild(pointSlot().selected,lightChanged);const bool rebuild=rebuildMask!=0;
        const DWORD updateAt=GetTickCount();if(fresh)pointReplayCount=withReplays?replays.size():0;
        if(!pointSlot().schedule.due(updateAt,pointSlot().selected,meshGeneration,pointReplayCount,rebuild,quality.pointShadowRefreshMs)){
            pointSlot().ready=true;++pointReuses;return true;
        }
        if(!fresh){
            captureDemand=true;++captureDeferrals;
            if(!pointSlot().schedule.usable||lightChanged)return false;
            pointSlot().ready=true;++pointReuses;return true;
        }
        // All six faces already contain this exact committed geometry, and
        // there are no current actor replays to add. Do not classify thousands
        // of terrain batches just to rediscover the same six reuse decisions.
        bool geometryUnchanged=!rebuild&&!lightChanged&&pointSlot().schedule.complete&&
            pointSlot().geometryOnlyFaces==63&&(!withReplays||replays.empty());
        for(unsigned face=0;face<6&&geometryUnchanged;++face)
            geometryUnchanged=pointSlot().geometrySerial[face]==meshGeneration&&pointSlot().terrainSerial[face]==liveTerrainRevision;
        if(geometryUnchanged){
            pointSlot().schedule.commit(updateAt,pointSlot().selected,meshGeneration);
            pointFacesReused=6;pointSlot().ready=true;++pointReuses;return true;
        }
        const auto& previous=pointSlot().schedule;
        const bool smallMotion=lightChanged&&pointSlot().cacheValid&&pointSlot().cacheSource==light.sourceId&&
            NorthlightPointShadow::smallNativeCubeMotion(previous.light,light);
        const bool compatibleCube=(!rebuild&&!lightChanged)||smallMotion;
        const auto age=updateAt-previous.updatedAt;
        const auto period=NorthlightPointShadow::RefreshSchedule::period(pointReplayCount,quality.pointShadowRefreshMs);
        if(budget&&withReplays&&!replays.empty()&&compatibleCube&&previous.complete&&
           pointSlot().geometryOnlyFaces!=63&&pointSlot().completeTerrainRevision==liveTerrainRevision&&
           previous.generation==meshGeneration&&quality.pointShadowFacesPerFrame==6){
            const uint64_t area=uint64_t(localScissorView.halfW)*localScissorView.halfH;
            const bool adaptiveMotion=quality.pointShadowAdaptiveRefresh&&smallMotion&&pointAdaptiveMotionEnabled()&&
                NorthlightPointShadow::holdSmallMovingCube(previouslyHeld,age,rendererFrame,pointIndex,screenArea,area,period,
                    pointReplayCount>=256&&budget->exhausted());
            const bool adaptive=adaptiveMotion||(quality.pointShadowAdaptiveRefresh&&!lightChanged&&
                NorthlightPointShadow::holdSmallCube(previouslyHeld,age,rendererFrame,pointIndex,screenArea,area));
            if(adaptive||(smallMotion&&!previouslyHeld&&age<period)||budget->mayHold(previouslyHeld,age,period)){
                pointSlot().budgetHeld=true;
                pointSlot().ready=true;
                pointWork.adaptiveHeld+=adaptive;
                if(adaptiveMotion){++pointAdaptiveMotionHeld;pointAdaptiveMotionPeakAge=std::max(pointAdaptiveMotionPeakAge,unsigned(age));}
                ++pointReuses;
                return true;
            }
        }
        // Only an actual dynamic cube refresh consumes replay bounds. Start
        // before candidate preparation; the first reader below joins the job.
        if(withReplays)replayBoundsRequest();
        // PointShadowFacesPerFrame<6: a refresh of the same light whose six faces are all complete
        // draws only this frame's group; a new, moved or resized light draws all six at once.
        const bool cycling=quality.pointShadowFacesPerFrame<6&&pointSlot().schedule.usable&&!lightChanged&&pointSlot().cacheValid&&pointSlot().cacheSource==pointSlot().selected.sourceId;
        unsigned faces=63;
        if(cycling){if(!pointSlot().faceCycle.active())pointSlot().faceCycle.start(quality.pointShadowFacesPerFrame,pointSlot().faceWeights,updateAt,meshGeneration);
            faces=pointSlot().faceCycle.mask();pointSlot().schedule.stale();} /* a face failure below invalidates the whole cube */
        else{
            // From here a failure may leave a partially overwritten cube. It cannot
            // be reused until every face has been completed successfully.
            pointSlot().faceCycle.reset();pointSlot().schedule.invalidate();}
        float faceMatrices[6][16];
        for(unsigned face=0;face<6;++face)
            if((faces&(1u<<face))&&!NorthlightPointShadow::faceMatrix(pointSlot().selected.position,.1f,pointSlot().selected.attenuationEnd,face,faceMatrices[face],PointResolution))return pointCheck(E_FAIL,"cube projection");
        const NorthlightPointShadow::FaceClip clip(faceMatrices,pointSlot().selected,faces);
        const bool pointTimed=NorthlightQuality::renderProfile(quality);
        const int64_t prepareStart=pointTimed?QpcClock::now():0;
        PointLampSplits lampSplits;
        try {
            for(auto* lists:{&pointStaticCandidates,&pointTerrainCandidates,&pointLiveCandidates,&pointReplayCandidates})for(auto& list:*lists)list.clear();
            preparePointTerrain(clip,faces,rebuildMask&faces);
            std::vector<size_t> owned;
            if(light.sourceId&&((rebuildMask&faces)||withReplays)){
                const bool indexed=pointLampSources.prepare(batches,meshGeneration);
                const auto candidates=pointLampSources.find(light.sourceId);
                owned=NorthlightWmoLampShadow::parts(light,batches,indexed?&candidates:nullptr);
            }
            if(rebuildMask&faces){
                const unsigned staticFaces=rebuildMask&faces;
                const bool listed=terrainBatchListGeneration==meshGeneration&&terrainBatchListSize==batches.size();
                pointMeshBounds.prepare(meshGeneration,batches,pointSubBatches);
                const size_t outside=NorthlightPointMesh::classifyStatic(batches,pointSubBatches,pointSubRanges,listed?&pointStaticList:nullptr,
                    pointNearbyReady?pointNearbyBatches.data():nullptr,staticFaces,pointBatchMasks,
                    [&](size_t i,const Batch& batch,unsigned mask){return pointMeshBounds.mask(i,batch,clip,mask);},
                    [&](size_t i,unsigned mask){
                        if(!std::binary_search(owned.begin(),owned.end(),i))pointAppendCandidates(pointStaticCandidates,i,mask,staticFaces);
                    });
                // An excluded shell must also block coalescing across index gaps.
                for(size_t i:owned)pointBatchMasks[i]|=staticFaces;
                pointCold.staticEmptyCandidates+=outside;
                pointCulled+=unsigned(outside)*unsigned(__builtin_popcount(staticFaces));
            }
            for(size_t i=0;i<pointLiveBatches.size();++i){const auto& batch=pointLiveBatches[i];
                pointAppendCandidates(pointLiveCandidates,i,pointMask(batch.low,batch.high,clip,faces),faces);}
            const int64_t boundsStart=pointTimed?QpcClock::now():0;
            replayBoundsJoin();
            const int64_t candidatesStart=pointTimed?QpcClock::now():0;
            if(pointTimed)pointCold.boundsWait+=candidatesStart-boundsStart;
            if(withReplays){
                pointOrderReplays();
                const auto lamp=pointLampReplay(light,owned);
                if(world&&lamp.placed.lampTriangles)world->prepare(replays.size());
                const bool ordered=pointReplayOrderReady&&pointReplayOrder.size()==replays.size();
                pointReplayCasters.prepare(replays.size(),[&](size_t n){
                    const size_t i=ordered?pointReplayOrder[n]:n;
                    const auto& replay=*replays[i];
                    const auto& bounds=replay.pointBounds;
                    return NorthlightPointShadow::ReplayCasters::Source{i,bounds.low,bounds.high,bounds.valid,replay.shadowSkinned};
                });
                const auto counts=pointReplayCasters.classify(clip,faces,[&](size_t i,unsigned mask){
                    if(pointKeepLampReplay(i,light,lamp,lampSplits,world))pointAppendCandidates(pointReplayCandidates,i,mask,faces);
                });
                pointCulled+=counts.culled;
                pointWork.unboundedSkinned+=counts.unboundedSkinned;
                pointWork.unboundedRigid+=counts.unboundedRigid;
            }
            if(pointTimed)pointCold.candidates+=QpcClock::now()-candidatesStart;
        }catch(...){return pointCheck(E_OUTOFMEMORY,"cube candidate allocation");}
        const int64_t submitStart=pointTimed?QpcClock::now():0;
        if(pointTimed)pointCold.prepare+=submitStart-prepareStart;
        NorthlightShadowReplay::Pass replayPass(d);
        const auto check=[&](HRESULT hr,const char* operation){return pointCheck(hr,operation);};
        NorthlightPointShadow::FacePass facePass(d,check,
            PointResolution,pointTargets.depth(),pointTargets.casterPixel());
        using MeshPass=decltype(facePass)::MeshPass;
        const bool opaqueEnabled=pointOpaqueReuseEnabled()&&!memoryPressure&&lampSplits.empty();
        const bool mixedEnabled=pointMixedReuseEnabled()&&!memoryPressure&&lampSplits.empty();
        for(unsigned face=0;face<6;++face){
            if(!(faces&(1u<<face)))continue; /* a later frame of this face cycle */
            const unsigned bit=1u<<face;
            const bool staticOnly=pointTerrainCandidates[face].empty()&&
                (!liveTerrainIndexCount||pointLiveCandidates[face].empty())&&pointReplayCandidates[face].empty();
            const bool geometryOnly=pointReplayCandidates[face].empty();
            auto& opaqueToken=pointSlot().opaqueTokens[face];
            auto& mixedToken=pointSlot().mixedTokens[face];
            auto* const staticFace=pointSlot().cacheFace[face];
            const bool proveOpaque=opaqueEnabled&&!geometryOnly&&!(rebuildMask&bit)&&
                (!staticFace||(pointMinBlend&&pointSlot().cacheSurface[face]))&&pointTerrainCandidates[face].empty()&&
                (!liveTerrainIndexCount||pointLiveCandidates[face].empty());
            const auto replayAt=[&](size_t index)->const Replay&{return *replays[index];};
            const auto shaderRows=[&](const Replay& replay)->const NorthlightPointShadow::RigidPoseRows*{
                const auto shader=captureShaders.find(replay.originalShader);
                return shader!=captureShaders.end()&&shader->second.replacement==replay.shader?
                    &shader->second.pointRigidRows:nullptr;
            };
            float opaqueRows[16];
            const NorthlightPointShadow::OpaqueFaceCache::Key opaqueKey{
                light.sourceId,meshGeneration,worldShaderGen,light.attenuationEnd,staticFace,faceMatrices[face],opaqueRows};
            if(proveOpaque){
                ++pointCold.opaqueTested;
                NorthlightWorldMath::replayProjection(context.inverseView,faceMatrices[face],opaqueRows);
                if(const auto* saved=pointOpaqueFaces.match(opaqueToken,opaqueKey,pointReplayCandidates[face],replayAt,shaderRows)){
                    ++pointFacesReused;
                    ++pointCold.opaqueReused;
                    pointCold.opaqueSaved.draws+=saved->draws;
                    pointCold.opaqueSaved.packets+=saved->packets;
                    pointCold.opaqueSaved.triangles+=saved->triangles;
                    continue;
                }
            }
            opaqueToken=0;
            const auto& candidates=pointReplayCandidates[face];
            // The ordinary static-base route already blends with MIN. Direct
            // faces use D24 overwrite order when two fragments tie.
            const bool split=mixedEnabled&&withReplays&&pointMinBlend&&!(rebuildMask&bit)&&
                (mixedToken||pointMixedFaces.canStore())&&
                staticFace&&pointSlot().cacheSurface[face]&&pointMixedFaces.split(candidates,replayAt,shaderRows);
            NorthlightPointShadow::MixedFaceCache::Match mixed;
            if(split){
                NorthlightWorldMath::replayProjection(context.inverseView,faceMatrices[face],opaqueRows);
                mixed=pointMixedFaces.match(mixedToken,opaqueKey,replayAt,shaderRows);
            }
            if(!mixed)mixedToken=0;
            if(!(rebuildMask&bit)){
                if(pointSlot().faceKnown[face]&&pointSlot().faceSerial[face]!=meshGeneration){pointSlot().faceSerial[face]=meshGeneration;++pointFacesKept;}
                if(staticOnly&&(pointSlot().staticOnlyFaces&bit)){
                    pointSlot().geometryOnlyFaces|=bit;pointSlot().geometrySerial[face]=meshGeneration;pointSlot().terrainSerial[face]=liveTerrainRevision;
                    pointSlot().faceWeights[face]=0;++pointFacesReused;continue;
                }
                if(geometryOnly&&(pointSlot().geometryOnlyFaces&bit)&&
                   pointSlot().geometrySerial[face]==meshGeneration&&pointSlot().terrainSerial[face]==liveTerrainRevision){
                    ++pointFacesReused;continue;
                }
            }
            pointSlot().geometryOnlyFaces&=~bit;
            if(!facePass.beginFace(pointSlot().faces[face],pointSlot().selected.attenuationEnd))return false;
            const float* matrix=faceMatrices[face];
            const auto drawMesh=[&](const auto& candidates,MeshPass pass,unsigned& draws){
                return facePass.drawMeshRanges(candidates.size(),pass,materials,uploadedAlphaCutoffs,
                    [&](size_t& at){return pointBatchRun(candidates,at,face);},
                    [&](const Batch& batch,UINT& page){return bindMeshPage(batch,page);},
                    [&](UINT triangles){++pointDraws;++draws;pointWork.triangles+=triangles;});
            };
            const auto bindWorldGeometry=[&]{
                replayPass.invalidateBindings();
                const bool bound=facePass.bindWorldGeometry(shadowPrograms.worldLayout(),pointTargets.casterVertex(),matrix,vertices,sizeof(NorthlightGI::WorldVertex),indices);
                if(bound)replayPass.recordFloatPrefix(matrix,4);
                return bound;
            };
            if(rebuildMask&(1u<<face)){
                if(pointStaticCandidates[face].empty()){drop(pointSlot().cacheSurface[face]);drop(pointSlot().cacheFace[face]);}
                else {
                    if(!pointCacheResources(face)||
                       !facePass.beginStatic(pointSlot().cacheSurface[face],pointPairedTargets&&(staticOnly||pointMinBlend),pointPairedTargets)||
                       !bindWorldGeometry()||
                       !drawMesh(pointStaticCandidates[face],MeshPass::Static,pointWork.statics)||
                       !facePass.finishStatic())return false;
                    if(facePass.seeded())++pointWork.pairedFaces;
                }
                pointRememberStaticFace(face,lightChanged);
                ++pointFacesRebuilt;++pointWork.rebuilt;
            }
            pointSlot().staticOnlyFaces&=~bit;
            const unsigned faceDraws=pointDraws,faceMerged=pointBatchesMerged;
            const auto faceTriangles=pointWork.triangles;
            const bool refusedCopy=mixed&&!facePass.copyCachedBase(mixed.image->surface);
            if(refusedCopy){
                mixed={};mixedToken=0;
                pointMixedFaces.refuseStore();
                ++pointCold.mixedFailures;
            }
            if(!mixed)facePass.chooseDestination(pointSlot().cacheFace[face]!=nullptr,pointSlot().cacheSurface[face],staticOnly,pointMinBlend);
            // Dynamic casters of this frame into the selected face, plus cached-mesh
            // terrain, including parts absent from the camera-culled native draws.
            if(!staticOnly){
                const bool drawLiveTerrain=liveTerrainIndexCount&&!pointLiveCandidates[face].empty();
                // The first replay binds its own geometry, projection and material.
                const bool drawWorldGeometry=drawLiveTerrain||!pointTerrainCandidates[face].empty();
                if(!facePass.beginDynamic(pointTargets.scratchSurface(),pointMinBlend,[&]{return !drawWorldGeometry||bindWorldGeometry();}))return false;
                if(!drawMesh(pointTerrainCandidates[face],MeshPass::Terrain,pointWork.terrain))return false;
                if(drawLiveTerrain){
                    if(!facePass.drawLiveTerrain({shadowPrograms.liveLayout(),liveTerrainGPU.vertices(),liveTerrainIndices.buffer(),
                            sizeof(StaticShadow::Vertex),liveTerrainGPU.vertexCapacity(),liveTerrainIndices.base()},
                        shadowPrograms.worldLayout(),pointLiveCandidates[face],
                        [&](size_t index)->const auto& {return pointLiveBatches[index];},
                        [&]{liveTerrainIndices.touch(fence());},
                        [&](UINT triangles){++pointDraws;++pointWork.live;pointWork.triangles+=triangles;}))return false;
                }
                const auto& replayPackets=mixed?pointMixedFaces.remaining():candidates;
                if(withReplays&&!submitPointReplayFace(replayPass,replayPackets,matrix,rendererFrame,face,check,
                        lampSplits.empty()?nullptr:&lampSplits))return false;
            } /* dynamic candidates */
            // Preserve the original logical workload weights: coalescing must
            // not change the configured partial-face refresh groups/order.
            pointSlot().faceWeights[face]=pointDraws-faceDraws+pointBatchesMerged-faceMerged+(mixed?mixed.work->packets:0);
            auto* const resolvedBase=mixed?mixed.image->texture:pointSlot().cacheFace[face];
            if(!facePass.resolve(staticOnly,resolvedBase,pointTargets.scratch(),shadowPrograms.unionShader()))return false;
            if(facePass.usesUnion()){
                replayPass.invalidateBindings();
                ++pointWork.unions;
            }
            if(staticOnly)pointSlot().staticOnlyFaces|=bit;
            if(geometryOnly){pointSlot().geometryOnlyFaces|=bit;pointSlot().geometrySerial[face]=meshGeneration;pointSlot().terrainSerial[face]=liveTerrainRevision;}
            facePass.finishFace();
            // A failed static copy falls back to a shader union and turns off
            // MIN blending. Certify only direct or successfully copied faces.
            if(proveOpaque&&(!staticFace||pointMinBlend)){
                const unsigned draws=pointDraws-faceDraws;
                opaqueToken=pointOpaqueFaces.record(opaqueKey,pointReplayCandidates[face],replayAt,shaderRows,
                    {draws,draws+pointBatchesMerged-faceMerged,pointWork.triangles-faceTriangles});
                pointCold.opaqueStored+=opaqueToken!=0;
            }
            if(mixed){
                ++pointCold.mixedFaces;
                pointCold.mixedSaved.packets+=mixed.work->packets;
                pointCold.mixedSaved.triangles+=mixed.work->triangles;
            }else if(split&&pointMixedFaces.canStore()&&pointMinBlend){
                bool stateRestored=true;
                mixedToken=pointMixedFaces.store(d,PointResolution,opaqueKey,replayAt,shaderRows,
                    [&](IDirect3DSurface9* target){
                        return bakePointSubset(target,pointSlot().cacheSurface[face],matrix,rendererFrame,face,stateRestored);
                    });
                if(!mixedToken)++pointCold.mixedFailures;
                if(!stateRestored)return pointCheck(E_FAIL,"restore mixed cube cache state");
            }
        }
        const auto& bindings=replayPass.bindingCounts();
        pointCold.geometryCalls+=bindings.declarations+bindings.streams+bindings.indices+bindings.shaders;
        if(pointTimed)pointCold.submit+=QpcClock::now()-submitStart;
        if(pointTimed&&rebuild){
            const unsigned written=__builtin_popcount(rebuildMask&faces);
            if(!pointSlot().cacheValid||pointSlot().cacheSource!=light.sourceId)pointCold.facesWithoutCache+=written;
            else if(pointSlot().schedule.light.attenuationEnd!=light.attenuationEnd)pointCold.facesResized+=written;
            else if(lightChanged)pointCold.facesMoved+=written;
            else pointCold.facesMeshInvalidated+=written;
        }
        if(rebuild){pointSlot().cacheValid=true;pointSlot().cacheSource=pointSlot().selected.sourceId;} /* cycling: already valid, its remaining faces wait for their frame */
        if(cycling&&!pointSlot().faceCycle.advance()){pointSlot().ready=true;++pointUpdates;return true;} /* stale until the last group */
        if(cycling)pointSlot().schedule.commit(pointSlot().faceCycle.startedAt,pointSlot().selected,pointSlot().faceCycle.generation);
        else pointSlot().schedule.commit(updateAt,pointSlot().selected,meshGeneration);
        pointSlot().completeTerrainRevision=liveTerrainRevision;
        pointSlot().ready=true;++pointUpdates;
        if(frames==0||(frames%600==0&&NorthlightDiagnostics::enabled()))logf("POINT source=%llu kind=%u range=%.2f faces=%u size=256 draws=%u reusedFaces=%u culled=%u replayBounds=%u boundVertices=%zu boundOperations=%zu",(unsigned long long)pointSlot().selected.sourceId,pointSlot().selected.kind,pointSlot().selected.attenuationEnd,unsigned(__builtin_popcount(faces)),pointDraws,pointFacesReused,pointCulled,pointBoundsValid,pointBoundVertices,pointBoundOperations);
        return true;
    }
    static bool pointAdaptiveMotionEnabled(){
#ifdef NORTHLIGHT_REPLAY_STAND
        static const bool enabled=[] {
            const char* value=std::getenv("NORTHLIGHT_RETRACE_POINT_ADAPTIVE_MOTION");
            return !(value&&value[0]=='0'&&value[1]=='\0');
        }();
        return enabled;
#else
        return true;
#endif
    }
    void renderPointShadows(const NorthlightLocalLightSelection::Selection& lights,bool fresh,bool withReplays,unsigned rendererFrame=0,
                            NorthlightShadowRefreshBudget::Clock::duration directionalTime={}){
        ++nativeCubeRevision;
        pointReady=false;pointWork={};pointCold={};pointAdaptiveMotionHeld=pointAdaptiveMotionPeakAge=0;
        pointIndexedPairDraws=pointIndexedBatchDraws=pointIndexedBatchPackets=0;
        const auto hitsBefore=shadowReplayIndices.hits,buildsBefore=shadowReplayIndices.builds;
        const auto uploadsBefore=shadowReplayIndices.uploaded();
        if(!active)return;
        NorthlightShadowRefreshBudget refreshBudget(quality.shadowRefreshCpuMs,directionalTime);
        unsigned held=0,movingHeld=0,coldCubes=0;
        const bool coldTimed=NorthlightQuality::renderProfile(quality);const int64_t coldStarted=coldTimed?QpcClock::now():0;
        if(pointSelectionMap!=active->map){pointSelection.reset();pointTargets.keeps().trim();pointSelectionMap=active->map;}
        const auto picks=pointSelection.update(lights.sources.data(),lights.count,context.camera,NorthlightQuality::localShadowLimit(quality),visualTime(),&localScissorView,float(quality.pointShadowDistance));
        unsigned selected=0,totalDraws=0,totalCulled=0;const unsigned updatesBefore=pointUpdates,reusesBefore=pointReuses;
        unsigned wanted=0;for(const auto& pick:picks)wanted+=pick.index>=0;
        const unsigned limit=std::min(NorthlightQuality::localShadowLimit(quality),NorthlightPointShadow::MaxLights);
        // Every keep uses a spare configured slot, never an extra light budget;
        // eight static-only keeps cap idle targets at 12 MiB, zero under pressure.
        const unsigned keepLimit=memoryPressure?0:std::min(unsigned(pointTargets.keeps().size()),limit-std::min(limit,wanted));
        pointTargets.keeps().prepare(keepLimit,[&](uint64_t source){
            return std::any_of(picks.begin(),picks.end(),[&](const auto& pick){
                return pick.index>=0&&lights.sources[pick.index].sourceId==source;
            });
        });
        // Release outgoing resources before allocating replacements, even when
        // the free slot occurs earlier in the array than the retired slot.
        for(pointIndex=0;pointIndex<pointTargets.slots().size();++pointIndex){pointSlot().ready=false;const auto& pick=picks[pointIndex];
            // Allocation and retained static faces always acquire a map first.
            if(pick.index<0&&!pointSlot().cube&&pointSlot().map.empty())continue;
            if(pick.index<0||pointSlot().map!=active->map||pointSlot().selected.sourceId!=lights.sources[pick.index].sourceId){
                PointSlot recycled;
                if(pick.index>=0&&!memoryPressure&&pointSlot().map==active->map)
                    recycled.swapCube(pointSlot());
                keepPointSlot(pointSlot(),keepLimit);
                // Reuse storage only when a replacement will immediately draw.
                // Retirement still resets every content key and schedule.
                recycled.swapCube(pointSlot());
            }}
        PointWorldTriangles world;
        for(pointIndex=0;pointIndex<pointTargets.slots().size();++pointIndex){const auto& pick=picks[pointIndex];if(pick.index<0)continue;
            ++selected;
            restorePointSlot(lights.sources[pick.index]);pointSlot().selectionGain=pick.gain;
            const bool cold=staggerColdCaptures&&!pointSlot().schedule.usable;
            // Spread first renders across frames. Restored cubes and the refresh
            // of already usable cubes follow the ordinary scheduling rules.
            if(cold&&coldCubes>=2){pointSelection.defer(pointIndex);continue;}
            renderPointShadow(lights.sources[pick.index],fresh,withReplays,&refreshBudget,rendererFrame,memoryPressure?nullptr:&world);pointReady|=pointSlot().ready;
            if(cold&&pointSlot().ready)++coldCubes;
            held+=pointSlot().budgetHeld;
            movingHeld+=pointSlot().budgetHeld&&!NorthlightPointShadow::sameCube(pointSlot().schedule.light,pointSlot().selected);
            totalDraws+=pointDraws;totalCulled+=pointCulled;
            if(!pointSlot().ready)pointSelection.defer(pointIndex);
        }
        pointIndex=0;
        pointDraws=totalDraws;pointCulled=totalCulled;pointLogSchedule(pointUpdates==updatesBefore&&pointReuses!=reusesBefore);
        pointWork.selected=selected;
        if(profileSampled())logf("SHADOW_REFRESH rendererFrame=%u targetMs=%u directionalCpuMs=%.3f heldCubes=%u selected=%u heldMovingCubes=%u adaptiveHeldCubes=%u adaptiveMotion=%u adaptiveMotionHeld=%u adaptiveMotionPeakAgeMs=%u",rendererFrame,
            quality.shadowRefreshCpuMs,std::chrono::duration<double,std::milli>(directionalTime).count(),held,selected,movingHeld,pointWork.adaptiveHeld,unsigned(pointAdaptiveMotionEnabled()),pointAdaptiveMotionHeld,pointAdaptiveMotionPeakAge);
        if(profileSampled())logf("POINT_INDEX_PAIRS rendererFrame=%u draws=%u cacheHits=%u created=%u uploadedBytes=%zu residentBytes=%zu",
            rendererFrame,pointIndexedPairDraws,shadowReplayIndices.hits-hitsBefore,shadowReplayIndices.builds-buildsBefore,
            shadowReplayIndices.uploaded()-uploadsBefore,shadowReplayIndices.bytes());
        if(profileSampled())logf("POINT_INDEX_BATCHES rendererFrame=%u enabled=%u draws=%u packets=%u",
            rendererFrame,unsigned(pointIndexBatchesEnabled()),pointIndexedBatchDraws,pointIndexedBatchPackets);
#ifdef NORTHLIGHT_REPLAY_STAND
        constexpr bool logMixed=true;
        constexpr bool logOpaque=true;
#else
        const bool logMixed=pointMixedReuseEnabled();
        const bool logOpaque=pointOpaqueReuseEnabled();
#endif
        if(logMixed&&profileSampled())logf("POINT_MIXED_BAKE rendererFrame=%u enabled=%u faces=%u packets=%u triangles=%llu bakeDraws=%u failures=%u",
            rendererFrame,unsigned(pointMixedReuseEnabled()),pointCold.mixedFaces,pointCold.mixedSaved.packets,
            (unsigned long long)pointCold.mixedSaved.triangles,pointCold.mixedBakedDraws,pointCold.mixedFailures);
        if(logOpaque&&profileSampled())logf("POINT_OPAQUE_REUSE rendererFrame=%u enabled=%u tested=%u stored=%u reused=%u savedDraws=%u savedPackets=%u savedTriangles=%llu",
            rendererFrame,unsigned(pointOpaqueReuseEnabled()),pointCold.opaqueTested,pointCold.opaqueStored,pointCold.opaqueReused,
            pointCold.opaqueSaved.draws,pointCold.opaqueSaved.packets,(unsigned long long)pointCold.opaqueSaved.triangles);
#ifdef NORTHLIGHT_REPLAY_STAND
        if(profileSampled())logf("POINT_EFFECTIVE_POSE rendererFrame=%u enabled=%u merged=%u",
            rendererFrame,unsigned(pointEffectivePose()),pointCold.rigidPoseMerges);
        if(profileSampled())logf("POINT_INDEX_ADMISSION rendererFrame=%u reuse=%u tests=%u hits=%u",
            rendererFrame,unsigned(pointIndexAdmissionReuse()),pointCold.indexAdmissionTests,pointCold.indexAdmissionHits);
#endif
        if(profileSampled())logf("POINT_REPLAY_STATE rendererFrame=%u reuse=%u faces=%u geometryCalls=%u constantCalls=%u constantBytes=%llu weightedConstantDraws=%u",
            rendererFrame,unsigned(pointReplayStateReuse()),pointCold.replayFaces,pointCold.geometryCalls,pointCold.constantCalls,(unsigned long long)pointCold.constantBytes,pointCold.weightedConstantDraws);
        if(pointReportedLimit!=NorthlightQuality::localShadowLimit(quality)||(frames%600==0&&NorthlightDiagnostics::enabled())){
            unsigned ready=0,resident=0,kept=0,staticFaces=0;for(const auto& p:pointTargets.slots()){ready+=p.ready;resident+=p.cube!=nullptr;for(auto* t:p.cacheFace)staticFaces+=t!=nullptr;}for(const auto& k:pointTargets.keeps()){kept+=k.slot.cacheValid;for(auto* t:k.slot.cacheFace)staticFaces+=t!=nullptr;}
            logf("POINT pool limit=%u selected=%u ready=%u cubeSlots=%u estimatedMiB=%.1f staticKeeps=%u staticFaces=%u",NorthlightQuality::localShadowLimit(quality),selected,ready,resident,resident*1.5+staticFaces*.25+(pointTargets.scratch()?.25:0)+(pointTargets.depth()?.25:0),kept,staticFaces);
            pointReportedLimit=NorthlightQuality::localShadowLimit(quality);
        }
        if(coldTimed&&captureFrequency.QuadPart>0){const double scale=1000.0/double(captureFrequency.QuadPart),cpuMs=double(QpcClock::now()-coldStarted)*scale;
            if(cpuMs>=8||profileSampled()){
                logf("POINT_PHASE rendererFrame=%u prepareMs=%.3f boundsWaitMs=%.3f candidatesMs=%.3f submitMs=%.3f prepareIncludesBoundsAndCandidates=1 terrainMemoHits=%u terrainMemoMisses=%u terrainListSize=%zu totalBatches=%zu contentPasses=%u contentRecords=%zu spatialIndex=%u facesWithoutCache=%u facesResized=%u facesMoved=%u facesMeshInvalidated=%u staticEmptyCandidates=%zu preparedMeshBounds=%zu",rendererFrame,double(pointCold.prepare)*scale,double(pointCold.boundsWait)*scale,double(pointCold.candidates)*scale,double(pointCold.submit)*scale,pointCold.terrainMemoHits,pointCold.terrainMemoMisses,terrainBatchList.size(),batches.size(),pointCold.contentPasses,pointCold.contentRecords,unsigned(bool(pointTree)),pointCold.facesWithoutCache,pointCold.facesResized,pointCold.facesMoved,pointCold.facesMeshInvalidated,pointCold.staticEmptyCandidates,pointMeshBounds.size());
                logf("POINT_SHELL rendererFrame=%u nativeMs=%.3f placedMs=%.3f mergeMs=%.3f nativeChecks=%u placedChecks=%u placedCacheable=%u placedHits=%u mergeBuilds=%u mergeHits=%u worldMs=%.3f worldBuilds=%u worldHits=%u insideCandidates=1",
                    rendererFrame,double(pointCold.nativeShell)*scale,double(pointCold.placedShell)*scale,double(pointCold.mergeShell)*scale,
                    pointCold.nativeShellChecks,pointCold.placedShellChecks,pointCold.placedCacheable,pointCold.placedShellHits,pointCold.mergeShellBuilds,pointCold.mergeShellHits,
                    double(pointCold.placedWorld)*scale,pointCold.placedWorldBuilds,pointCold.placedWorldHits);
                logf("POINT_ORDER rendererFrame=%u cpuMs=%.3f packets=%u eligible=%u oncePerFrame=1 alphaBarriers=1 fixedDraws=%u alphaScopes=%u",rendererFrame,double(pointCold.order)*scale,pointCold.orderPackets,pointCold.orderEligible,pointCold.fixedDraws,pointCold.alphaScopes);
                // A reused cube may leave the bounds job running. Its mutable
                // counters are readable only after that job has been joined.
                const auto* stats=replayBoundsWorker.pending()?nullptr:&replayBoundsCache.envelopeStats();
                if(stats){
                    // Read only joined job state. These phase totals need no
                    // extra clocks and identify the cap behind cold fan-out.
                    unsigned unfinished[5]={};
                    for(const auto& p:replays)if(!p->pointBounds.valid)++unfinished[unsigned(p->boundsWork.kind)];
                    logf("POINT_BOUNDS rendererFrame=%u worldFrame=%llu readyNs=%llu heavyNs=%llu buildNs=%llu readyLimitNs=%llu heavyLimitNs=%llu buildLimitNs=%llu unknown=%u cold=%u cheap=%u heavy=%u unsupported=%u pendingVertices=%zu pendingIndices=%zu skinBuildVertices=%zu typedSkinBuildVertices=%zu terminalBuildVertices=%zu terminalBuildNs=%llu",
                        rendererFrame,(unsigned long long)frames,
                        (unsigned long long)replayBoundsCache.phaseNanoseconds(0),(unsigned long long)replayBoundsCache.phaseNanoseconds(1),(unsigned long long)replayBoundsCache.phaseNanoseconds(2),
                        (unsigned long long)replayBoundsCache.phaseLimit(0),(unsigned long long)replayBoundsCache.phaseLimit(1),(unsigned long long)replayBoundsCache.phaseLimit(2),
                        unfinished[0],unfinished[1],unfinished[2],unfinished[3],unfinished[4],replayBoundsCache.pendingBuildVertices(),replayBoundsCache.pendingBuildIndices(),stats->skinBuildVertices,stats->typedSkinBuildVertices,stats->terminalBuildVertices,(unsigned long long)stats->terminalBuildNanoseconds);
                }
                logf("POINT_COLD rendererFrame=%u worldFrame=%llu cpuMs=%.3f allocationMs=%.3f cubeAttempts=%u staticTextureAttempts=%u sharedAttempts=%u shaderAttempts=%u selected=%u staticDraws=%u terrainDraws=%u liveDraws=%u replayDraws=%u unionDraws=%u staticRebuiltFaces=%u staticRestores=%u casterTriangles=%llu boundsTotal=%zu boundsValid=%u unboundedRigidFacePackets=%u unboundedSkinnedFacePackets=%u boundsStatsReady=%u boundsBuildVertices=%zu boundsBuildIndices=%zu boundsCompleted=%zu boundsTimeDeferred=%zu boundsVertexDeferred=%zu boundsOperationDeferred=%zu boundsPendingDeferred=%zu materialBoundsUsed=%u",rendererFrame,(unsigned long long)frames,cpuMs,double(pointCold.ticks)*scale,pointCold.cubes,pointCold.statics,pointCold.shared,pointCold.shaders,pointWork.selected,pointWork.statics,pointWork.terrain,pointWork.live,pointWork.replay,pointWork.unions,pointWork.rebuilt,pointWork.restored,(unsigned long long)pointWork.triangles,replays.size(),pointBoundsValid,pointWork.unboundedRigid,pointWork.unboundedSkinned,unsigned(stats!=nullptr),stats?stats->buildVertices:0,stats?stats->buildIndices:0,stats?stats->completed:0,stats?stats->timeDeferred:0,stats?stats->vertexDeferred:0,stats?stats->operationDeferred:0,stats?stats->pendingDeferred:0,pointMaterialBoundsUsed);
            }
        }
    }
    // sharedState belongs only to an uninterrupted batch with identical inputs
    // and settings. Standalone and diagnostic calls retain complete setup.
    bool renderPointLighting(IDirect3DTexture9* depth,IDirect3DTexture9* waterMask,UINT w,UINT h,float nearZ,float farZ,float minZ,float maxZ,float strength,float sunlitCut,float shadowStrength,float sunEnergy,NorthlightLightingCapture::Capture* capture=nullptr,bool* sharedState=nullptr,NorthlightLightingCapture::Capture* inputs=nullptr,float reflectionGain=0){
        if(!pointSlot().ready)return false;
        if(pointCubeIsWhite(pointSlot())){++pointWork.whiteLightingSkipped;return true;}
        NorthlightWorldPointLighting::Inputs pass{depth,waterMask,worldNormals.texture(),screenLighting.baseline().texture,
            materialDetail,pointSlot().cube,w/2,h/2};
        auto& constants=pass.constants;
        constants[0][0]=1.f/w;constants[0][1]=1.f/h;constants[0][2]=nearZ;constants[0][3]=farZ;
        constants[1][0]=projection[0];constants[1][1]=projection[1];constants[1][2]=projection[2];constants[1][3]=minZ;
        constants[2][0]=1.f/(maxZ-minZ);constants[2][1]=waterMask?1.f:0.f;constants[2][2]=pointSlot().selectionGain;constants[2][3]=.025f;
        std::memcpy(constants[3],context.inverseView,64);std::memcpy(constants[7],pointSlot().selected.position,12);constants[7][3]=1.f/std::max(pointSlot().selected.attenuationEnd-std::min(pointSlot().selected.attenuationStart,pointSlot().selected.attenuationEnd*.9f),.05f);
        std::memcpy(constants[8],pointSlot().selected.diffuse,12);constants[8][3]=pointSlot().selected.attenuationEnd;
        constants[9][0]=.1f;constants[9][1]=pointSlot().selected.attenuationEnd;constants[9][2]=1.f/PointResolution;constants[9][3]=1;
        constants[10][0]=strength;constants[10][1]=sunlitCut;constants[10][2]=shadowStrength;constants[10][3]=sunEnergy;
        constants[11][0]=sourceDirections[0].x;constants[11][1]=sourceDirections[0].y;constants[11][2]=sourceDirections[0].z;
        const auto* origin=pointSlot().cubeOrigin();
        for(unsigned i=0;i<3;++i)pass.cubeOffset[i]=origin[i]-constants[7][i];
        pass.cubeOffset[3]=reflectionGain;
        pass.bump[0]=materialDetail?materialBumpGain:0.f;pass.bump[1]=materialBumpDistance;
        return pointLighting.draw(d,pass,quality.softShadows,sharedState,localLighting.scissor(),localScissorView,
            [&](HRESULT hr,const char* operation){return pointCheck(hr,operation);},
            []{logf("POINT PCSS unavailable; ordinary filtering retained");},
            [&](bool soft,HRESULT result){
                if(inputs)inputs->pointCorrection(pointIndex,pointSlot(),soft,result);
                if(capture)capture->pointPass(pointIndex,soft,nullptr,result);
            });
    }
