// Render-thread staging and publication of an immutable worker mesh.
// A value returns the caller immediately. An empty result continues staging.
    std::optional<bool> beginMeshStaging(){
        if(!meshRetry.ready(GetTickCount())||!pressureReplayReady())return uploadedMap==active->map&&vertices&&indices;
        if(!active->meshPlan)return streamingFailure(D3DERR_INVALIDCALL,"validate plan","missing upload plan");
        const auto& plan=*active->meshPlan;
        auto payload=plan.payload.share();
        if(!payload){requestPayloadRebuild();return uploadedMap==active->map&&vertices&&indices;} /* 0.3.214 (task 42) */
        uint64_t largestTexture=0;for(const auto& material:payload->materials)largestTexture=std::max(largestTexture,shadowTextureFormat.bytes(material.bgraBytes()));
        const char* invalid=NorthlightWorldMeshPages::validateSealed(plan);
        if(invalid||!payload->pages||payload->pages->pages.empty())return streamingFailure(D3DERR_INVALIDCALL,"validate pages",invalid?invalid:"missing pages");
        // Admit all full-effect targets while the bounded candidate still
        // has its contiguous witness. Managed probes and cutout textures
        // then share allocator bins before DEFAULT page mappings fragment VA.
        if(payload->pages->temporary&&!geometryResources(width,height))return streamingFailure(E_OUTOFMEMORY,"pressure scene targets");
        // 0.3.216 (issue #66): look every new page up by content key among the store's buffers (active and idle); a hit needs no
        // upload. Admission below covers what this staging can newly allocate: the bytes of the MISSED pages (a hit or a reused
        // idle buffer adds no address space) plus the textures, not the whole mesh. All misses (no hit) is the old full copy.
        std::vector<NorthlightMeshPageKey::Key> pageKeys;pageKeys.reserve(payload->pages->pages.size());
        std::vector<uint8_t> elidedPages;bool anyElided=false;elidedPages.reserve(payload->pages->pages.size());
        for(const auto& page:payload->pages->pages){pageKeys.push_back(page.key);elidedPages.push_back(page.elided);anyElided|=page.elided;}
        const auto compatiblePage=[&](uint32_t id){return id<meshBuffers.size()&&meshBuffers[id].vb&&meshBuffers[id].ib&&(payload->pages->defaultPool()||(meshBuffers[id].vbPool==D3DPOOL_MANAGED&&meshBuffers[id].ibPool==D3DPOOL_MANAGED));};
        if(!meshStore.beginStaging(pageKeys,anyElided?elidedPages:std::vector<uint8_t>(),compatiblePage)){
            if(!meshStore.elidedMisses())return streamingFailure(D3DERR_INVALIDCALL,"page staging","staging already open");
            // An elided page has no CPU data and its buffer is gone (trimmed, replaced, device reset): this plan cannot be
            // staged. Nothing was claimed. Publish no keys (the rebuilt plan elides nothing, so this cannot repeat) and drop the
            // payload: the next frame asks the worker for a fresh generation through the payload-rebuild path.
            if(elidedFallbacks++<12)logf("WORLD elided page missed at staging: misses=%zu of %zu pages, rebuilding the generation without elision (fallback %u)",meshStore.elidedMisses(),pageKeys.size(),elidedFallbacks);
            {std::lock_guard<std::mutex> lock(mutex);residentPages.reset();residentMap.clear();}
            active->meshPlan->payload.discard();
            return uploadedMap==active->map&&vertices&&indices;
        }
        syncMeshBuffers();
        const auto& staged=meshStore.staging();
        uint64_t missVertexBytes=0,missIndexBytes=0;
        if(!meshStore.stagingGrowth([&](uint32_t id,bool vertex){return id<meshBuffers.size()&&(vertex?meshBuffers[id].vb&&meshBuffers[id].vbPool==(payload->pages->defaultPool()?D3DPOOL_DEFAULT:D3DPOOL_MANAGED):meshBuffers[id].ib&&meshBuffers[id].ibPool==(payload->pages->defaultPool()?D3DPOOL_DEFAULT:D3DPOOL_MANAGED));},missVertexBytes,missIndexBytes))
            return streamingFailure(D3DERR_INVALIDCALL,"page staging","invalid growth accounting");
        auto budget=payload->pages->temporary?NorthlightGeometryMemory::coldUploadBudget(largestTexture):NorthlightGeometryMemory::uploadBudget(payload->pages->defaultPool()?0:missVertexBytes,payload->pages->defaultPool()?0:missIndexBytes,payload->pages->defaultPool()?0:shadowTextureFormat.bytes(active->meshPlan->textureBytes),payload->pages->defaultPool()?0:largestTexture);
        if(payload->pages->temporary)budget=NorthlightGeometryMemory::replayBudget(budget,pressureReplayAllocation());
        if(!payload->pages->temporary)budget.largest=2*std::max<uint64_t>(missVertexBytes||missIndexBytes?NorthlightWorldMeshPages::PageVertexByteLimit:0,largestTexture)+NorthlightGeometryMemory::ChunkMargin;
        NorthlightGeometryMemory::Sample memory;bool exact=true;
        {auto phase=streamingPhases.measure(NorthlightStreaming::PhaseProfile::Admission);memory=geometryAdmission(budget,&exact);}
        if(NorthlightDiagnostics::enabled())logGeometryMemory("before-upload",memory,0,exact,&budget);
        if(!NorthlightGeometryMemory::admits(memory,budget))return streamingFailure(E_OUTOFMEMORY,"upload-admission","address-space reserve or contiguous block");
        pendingMesh=std::make_unique<PendingMesh>();pendingMesh->bvh=active->bvh;
        pendingMesh->plan=active->meshPlan;pendingMesh->prepared=active->prepared;pendingMesh->map=active->map;pendingMesh->payload=payload;
        auto& next=*pendingMesh;next.textures.reserve(payload->materials.size());
        // The upload cursor runs over the miss list only; its page index maps to a payload page and a claimed buffer in perform().
        std::vector<NorthlightWorldMeshPages::PageBytes> sizes;sizes.reserve(staged.upload.size());
        for(uint32_t pageIndex:staged.upload){const auto& page=payload->pages->pages[pageIndex];sizes.push_back({page.key.vertexBytes,page.key.indexBytes,payload->pages->defaultPool()?sizeof(NorthlightGI::WorldVertex):0});}
        if(!next.upload.begin(std::move(sizes)))return streamingFailure(D3DERR_INVALIDCALL,"page sizes");
        return std::nullopt;
    }
    struct PageBudget {
        NorthlightStreaming::Budget& parent;double limitMs;
        bool available()const{return parent.available()&&parent.elapsedMs()<limitMs;}
        size_t chunk(size_t n)const{return available()?parent.chunk(n):0;}
        void consume(size_t n){parent.consume(n);}
    };
    std::optional<bool> stageMeshPages(PendingMesh& next,PageBudget& pageBudget){
        const auto& payload=*next.payload;
        auto perform=[&](NorthlightWorldMeshPages::Step step,size_t cursor,size_t offset,size_t bytes){
            using S=NorthlightWorldMeshPages::Step;
            const auto& staging=meshStore.staging();
            if(!staging.open||cursor>=staging.upload.size())return false;
            const uint32_t id=staging.uploadBuffer[cursor];
            if(!meshStore.markWriting(id)||id>=meshBuffers.size())return false; // never an active (drawn) buffer; the old key dies before the first write
            auto& page=meshBuffers[id];const auto& cpu=payload.pages->pages[staging.upload[cursor]];
            if(cpu.elided)return false; // an elided page is always a store hit and never in the upload list
            const auto& entry=meshStore.entries()[id];
            if(step==S::CreateVertices||step==S::CreateIndices){
                const bool vb=step==S::CreateVertices;
                const D3DPOOL pool=payload.pages->defaultPool()?D3DPOOL_DEFAULT:D3DPOOL_MANAGED;
                const size_t gpuBytes=vb&&pool==D3DPOOL_DEFAULT?bytes/sizeof(NorthlightGI::WorldVertex)*sizeof(StaticShadow::Vertex):bytes;
                const bool reuse=vb?page.vb&&page.vbPool==pool&&entry.vbCapacity>=gpuBytes:page.ib&&page.ibPool==pool&&entry.ibCapacity>=gpuBytes;
                if(reuse)return true;
                auto phase=streamingPhases.measure(NorthlightStreaming::PhaseProfile::Buffers);
                return acquireMeshPage(id,vb,UINT(gpuBytes),pool);
            }
            if(step==S::PreloadVertices||step==S::PreloadIndices){
                auto phase=streamingPhases.measure(NorthlightStreaming::PhaseProfile::Preload);
                gpuScene->meshPages.preload(id,step==S::PreloadVertices);
                return true;
            }
            auto phase=streamingPhases.measure(NorthlightStreaming::PhaseProfile::Copies);
            return gpuScene->meshPages.copy(id,step==S::CopyVertices,*payload.pages,cpu,offset,bytes,
                [&](const char* operation,DWORD flags){traceUpload(operation,flags);});
        };
        if(next.upload.advanceAvailable(perform,pageBudget)==NorthlightWorldMeshPages::Result::Failed)
            return streamingFailure(E_FAIL,"mesh page upload");
        return std::nullopt;
    }
    std::optional<bool> stageMeshMaterials(PendingMesh& next,PageBudget& pageBudget,NorthlightStreaming::Budget& streamBudget,
                                           size_t uploadLimit){
        const auto& payload=*next.payload;
        size_t budget=streamBudget.remainingBytes();
        auto canStage=[&]{return pageBudget.available()&&next.upload.status()==NorthlightWorldMeshPages::Result::Ready;};
        {auto phase=streamingPhases.measure(NorthlightStreaming::PhaseProfile::Materials);
        size_t uploads=0;
        while(canStage()&&budget&&next.material<payload.materials.size()){
            const auto& m=payload.materials[next.material];
            if(m.textureOwner!=UINT32_MAX){
                if(m.textureOwner>=next.textures.size()||!next.textures[m.textureOwner])return streamingFailure(E_FAIL,"staged material owner");
                auto* texture=next.textures[m.textureOwner];texture->AddRef();next.textures.push_back(texture);
                ++next.material;++next.textureReuses;continue;
            }
            const bool white=m.whiteMask();
            if(white&&next.white){next.white->AddRef();next.textures.push_back(next.white);++next.material;continue;}
            if(!next.materialUpload.texture()){
                if(auto* reused=reusableMaterial(m,next.material)){
                    if(resetReplayScoped)NorthlightWorldMeshPublication::MaterialUpload::preload(reused);
                    reused->AddRef();next.textures.push_back(reused);++next.material;++next.textureReuses;
                    if(white){next.white=reused;reused->AddRef();}
                    continue;
                }
                if(uploads>=uploadLimit)break;
                if(payload.pages->temporary&&!admitsGrowth("pressure-material",shadowTextureFormat.bytes(m.bgraBytes()),nullptr,true))return streamingFailure(E_OUTOFMEMORY,"pressure-material-admission");
                const auto opened=next.materialUpload.begin(d,m,shadowTextureFormat.value,payload.pages->defaultPool());
                if(FAILED(opened.hr))return streamingFailure(opened.hr,opened.stage);
            }
            if(!canStage())break;
            const auto copied=next.materialUpload.copy(m,shadowTextureFormat,budget,[&](size_t offset,size_t bytes,void* target){
                return payload.pages->copyMaterial(m,offset,bytes,target);
            });
            if(FAILED(copied.hr))return streamingFailure(copied.hr,copied.stage);
            if(!copied.bytes)break;
            budget-=copied.bytes;streamBudget.consume(copied.bytes);
            if(next.materialUpload.rowsWritten()==m.height){
                const auto finished=next.materialUpload.finish(d);
                if(FAILED(finished.hr))return streamingFailure(finished.hr,finished.stage);
                next.textures.push_back(next.materialUpload.texture());auto* t=next.materialUpload.take();
                ++next.material;++uploads;++next.textureUploads;
                if(white){next.white=t;t->AddRef();}
            }
        }
        }
        return std::nullopt;
    }
    bool commitStagedMesh(PendingMesh& next){
        auto& plan=*next.plan;
        const auto& payload=*next.payload;
        auto phase=streamingPhases.measure(NorthlightStreaming::PhaseProfile::Commit);
        traceUpload("commit-enter");
        // Allocate small commit metadata before releasing the previous GPU set.
        std::vector<float> cutoffs;cutoffs.reserve(payload.materials.size());
        // Constant white passes every validated cutoff, including 1. Point and
        // directional casters can use their existing opaque shader/batching path.
        for(const auto& material:payload.materials)
            cutoffs.push_back(material.alphaOnly&&material.whiteMask()?0.f:material.alphaCutoff);
        std::vector<Batch> committedBatches=payload.pages->batches,committedPointBatches=payload.pages->pointSubBatches;
        auto committedPointRanges=payload.pages->pointSubRanges;
        auto committedPointTree=payload.pages->pointTree;
        std::string committedMap=next.map;
        const bool prepared=next.prepared&&next.prepared->plan==next.plan.get()&&next.prepared->owners&&next.prepared->fixed;preparedOwnerCommits+=prepared;
        auto committedFixed=prepared?next.prepared->fixed:std::make_shared<const FixedChunks>(plan.fixedTerrainChunks);
        auto committedOwners=prepared?next.prepared->owners:StaticShadow::makeOwners(ownerPlacements(plan));
        // Retain only a bounded subset of immutable texels, by moving
        // existing upload storage (no second full BGRA allocation).
        auto committedPixels=std::make_shared<MaterialPixels>(payload.materials.size());
        size_t retainedPixels=0;
        traceUpload("commit-prepared");
        // Keep the previous complete geometry drawable until publication fits.
        if(uploadedTextureBytes<=NorthlightStreaming::DeferredRelease<IDirect3DTexture9>::MaxBytes&&
           !retiredMaterials.enqueue(materials,uploadedTextureBytes))return false;
        size_t elidedAtCommit=0;for(const auto& page:payload.pages->pages)elidedAtCommit+=page.elided;
        for(size_t i=0;i<next.payload->materials.size();++i){
            auto& material=next.payload->materials[i];
            if(payload.pages->temporary||material.textureOwner!=UINT32_MAX||material.retainedPixelBytes()>MaterialReuseBytes-retainedPixels)continue;
            retainedPixels+=size_t(material.retainedPixelBytes());(*committedPixels)[i]=std::move(material);
        }
        auto previousPixels=std::move(uploadedMaterialPixels);
        const size_t previousPixelBytes=materialReuseBytes;
        const bool coldStart=meshStore.table().empty();clearMesh();const auto stagedPages=meshStore.commit(); /* 0.3.216: atomic switch of the active page table; replaced buffers go idle */
        for(auto id:meshStore.table())if(id<meshBuffers.size())meshBuffers[id].vbFresh=meshBuffers[id].ibFresh=false;
        {const uint32_t first=meshStore.bufferOf(0);vertices=first<meshBuffers.size()?meshBuffers[first].vb:nullptr;indices=first<meshBuffers.size()?meshBuffers[first].ib:nullptr;}
        materials=std::move(next.textures);uploadedTextureBytes=size_t(shadowTextureFormat.bytes(plan.textureBytes));vertexCount=plan.vertexCount;
        uploadedMaterialPixels=std::move(committedPixels);materialReuseBytes=retainedPixels;
        if(previousPixels&&!NorthlightStreaming::cpuRetirement().retire(previousPixels,previousPixelBytes+previousPixels->capacity()*sizeof(NorthlightWorldMesh::MaterialUpload)))previousPixels.reset();
        uploadedLocalShadowRecords=plan.localShadowRecords;
        batches=std::move(committedBatches);pointSubBatches=std::move(committedPointBatches);pointSubRanges=std::move(committedPointRanges);pointTree=std::move(committedPointTree);uploadedAlphaCutoffs=std::move(cutoffs);
        // Replaced shared sets may be last owners (~7000 + ~3000 nodes).
        {auto& reaper=NorthlightStreaming::cpuRetirement();
         std::swap(fixedTerrain,committedFixed);std::swap(uploadedStaticOwners,committedOwners);fixedTerrainBits.assign(fixedTerrain.get());
         if(committedFixed)retirementBacklog.retire(reaper,committedFixed,committedFixed->size()*48);
         if(committedOwners)retirementBacklog.retire(reaper,committedOwners,committedOwners->placements*(sizeof(StaticShadow::Placement)+64));}
        uploaded=next.bvh;uploadedMeshPlan=next.plan;uploadedMap=std::move(committedMap);
        publishResidentPages(uploadedMap);
        ++meshGeneration;rebuildTerrainLists();
        // Resident staging is freed after publication for both pools.
        // File-backed pressure generations retain their small Reset source.
        const uint64_t payloadBytes=payload.pages->temporary?0:plan.payload.discard();const uint64_t releasedBytes=payloadBytes>retainedPixels?payloadBytes-retainedPixels:0;
        if(NorthlightDiagnostics::enabled())logf("WORLD staged mesh committed: vertices=%u triangles=%zu materials=%zu textureUploads=%u textureReuses=%u retainedMaterialMiB=%.2f stagingFrames=%u stagingMs=%lu releasedPayloadMiB=%.2f pages=%zu reusedPages=%zu uploadedPages=%zu reusedMiB=%.2f uploadedMiB=%.2f elidedPages=%zu deviceLocal=%u fileStaging=%u directionalCacheIndexed=%u directionalCacheBatches=%zu totalBatches=%zu",vertexCount,size_t(plan.triangleCount),materials.size(),next.textureUploads,next.textureReuses,double(materialReuseBytes)/1048576,next.frames,(unsigned long)(GetTickCount()-next.started),double(releasedBytes)/1048576,
            stagedPages.pages,stagedPages.reusedPages,stagedPages.uploadedPages,double(stagedPages.reusedBytes)/1048576,double(stagedPages.uploadedBytes)/1048576,elidedAtCommit,unsigned(payload.pages->defaultPool()),unsigned(bool(payload.pages->temporary)),unsigned(directionalCacheMesh.published()),directionalCacheMesh.retained(),batches.size());
        retirePendingCpu();pendingMesh.reset();meshRetry.clear();payloadRebuildAsked.reset();payloadRebuildTick=0;resetReplaySource.reset();resetReplayPlan.reset();resetReplayScoped=false;
        trimMeshPoolAfterCommit(coldStart?0:stagedPages.uploadedCapacity); /* a cold start held no buffer idle */
        traceUpload("commit-complete");
        return true;
    }

    bool upload(NorthlightStreaming::Budget& streamBudget,bool replayCheckpoint=false){
        if(!shadowTextureFormat.probe(d))return streamingFailure(E_OUTOFMEMORY,"shadow texture format");
        retiredMaterials.drain(streamBudget);
        static_assert(sizeof(NorthlightGI::WorldVertex)==32,"FGS vertex layout");
        releaseOrphanedPending("upload");pressureReplayAllocation();
        if(menuDomain){
            if(uploadedMap!=active->map){pendingMesh.reset();clearMesh();releaseMeshPool();uploadedMap=active->map;++meshGeneration;}
            uploaded=active->bvh;uploadedMeshPlan=active->meshPlan;
        }else if(uploaded.lock()!=active->bvh&&!pendingMesh&&streamBudget.available()){
            if(const auto outcome=beginMeshStaging())return *outcome;
        }
        if(pendingMesh){
            frameStaging=true;
            auto& next=*pendingMesh;
            ++next.frames;
            const bool fixedCheckpoint=replayCheckpoint&&NorthlightFrameParityClock::fixedGeometryCheckpoint();
            const double checkpointPageMs=fixedCheckpoint?
                std::numeric_limits<double>::infinity():28.0;
            PageBudget pageBudget{streamBudget,replayCheckpoint?checkpointPageMs:meshStore.table().empty()?1.5:.5};
            if(const auto outcome=stageMeshPages(next,pageBudget))return *outcome;
            // The fixed checkpoint can create at most one texture per remaining
            // material, with the byte cap shared by pages and texture copies.
            const size_t textureLimit=fixedCheckpoint?next.payload->materials.size()-next.material:replayCheckpoint?32u:16u;
            if(const auto outcome=stageMeshMaterials(next,pageBudget,streamBudget,textureLimit))return *outcome;
            if((fixedCheckpoint||streamBudget.available())&&next.upload.status()==NorthlightWorldMeshPages::Result::Ready&&
                next.material==next.payload->materials.size()&&!commitStagedMesh(next))
                return vertices&&indices&&uploadedMap==active->map;
        }
        if((!menuDomain&&(!vertices||!indices))||uploadedMap!=active->map)return false;
        // Mesh staging can precede the scene targets; probe upload waits for them.
        if(!geometryTargetsReady)return true;
        return uploadProbeAtlas();
    }
    bool probeAtlasReady(const std::string& map)const{
        return geometryTargetsReady&&(geometryGroups&2u)&&active&&active->map==map&&
               uploadedProbeMap==map&&uploadedSerial&&uploadedSerial==active->serial;
    }
    void stageScreenProbes(const std::string& map){
        if(geometryTargetsReady&&(geometryGroups&2u)&&active&&active->map==map)uploadProbeAtlas();
    }
    bool uploadProbeAtlas(){
        // Geometry can be ready before a new map's first GI solve. Shadows
        // may render immediately; GridInfo.w prevents reading the old atlas.
        if(active->serial==0)uploadedSerial=0;
        if(giWorkEnabled()&&(geometryGroups&2u)&&active->serial&&(uploadedSerial!=active->serial||uploadedProbeMap!=active->map)){
            const bool first=!uploadedSerial||uploadedProbeMap!=active->map;
            uploadedSerial=0; // A partially written atlas cannot certify any snapshot.
            auto phase=streamingPhases.measure(NorthlightStreaming::PhaseProfile::Probes);frameProbeUpload=true; /* 0.3.176 (D3) */
            if(active->atlas.size()!=NorthlightGI::probeLayout().atlasSize())return false;
            traceUpload("probe-enter");
            const float activationNow=visualAnimationSeconds(GetTickCount());
            NorthlightWorldProbeAtlas::UploadWork work;
            if(!probeAtlas.upload(active->map,active->atlas,active->coarseAtlas.get(),activationNow,work,
                [&](HRESULT hr,const char* name){return check(hr,name);}))return false;
            ++probeBlendPublishes;probeBlendSlots+=work.blendedSlots;
            uploadedProbeMap=active->map;uploadedSerial=active->serial;
            if(NorthlightFrameParityClock::enabled()&&NorthlightDiagnostics::enabled())
                logf("GI_ATLAS_PRESENT present=%u serial=%llu request=%llu actor=%llu generation=%llu processed=%u dynamicSolved=%u coarse=%u partial=%u",
                    parityFrame,(unsigned long long)uploadedSerial,(unsigned long long)active->requestId,
                    (unsigned long long)active->requestActorSerial,(unsigned long long)active->lightingGeneration,
                    active->processedProbes,active->dynamicSolved,unsigned(bool(active->coarseAtlas)),unsigned(active->partial));
#ifdef NORTHLIGHT_REPLAY_STAND
            if(NorthlightDiagnostics::enabled())logf("GI_PROBE_UPLOAD entries=%zu rgb=%u previous=%u blendTexture=%u",
                    active->atlas.size()*NorthlightGI::ProbeCascadeCount,unsigned(work.current),unsigned(work.previous),unsigned(probeAtlas.previous()!=nullptr));
#endif
            if(first&&NorthlightDiagnostics::enabled())deferLogf("WORLD probe atlas ready map=%s frame=%u tick=%lu staticMesh=%u",uploadedProbeMap.c_str(),unsigned(frames),(unsigned long)GetTickCount(),unsigned(uploaded.lock()==active->bvh));
        }return true;
    }
