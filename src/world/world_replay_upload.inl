private:
    NorthlightReplayBulk::Layout replayBulkLayout;
    NorthlightReplayBulk::DrawList replayBulkDraws;
    NorthlightWorldReplayBuffers replayBuffers;

public:
    void releaseReplayGPU(){
        replayGpuCache.clear();
        replayBuffers.release(fence());
        replayBulkDraws.clear();
    }
    bool uploadReplay(bool allowCache=true){
        NorthlightCapturePhases::Scope<2> phase(captureSampled?&replayUploadPhases:nullptr);
        if(captureSampled)++replayUploadCalls;
        if(!allowCache){replayGpuCache.clear();for(auto& p:replays)if(p->gpuCached){for(auto& stream:p->stream)drop(stream);drop(p->index);}}
        {auto expire=streamingPhases.measure(NorthlightStreaming::PhaseProfile::ReplayExpire);replayGpuCache.beginFrame();}uint64_t cacheGrowthBytes=0;
        auto bindReplay=[&](Replay& p){
            auto growthAdmission=[&](size_t bytes){
                // Pending batches are not allocated yet. Reserve every admitted
                // mesh plus a stride of batch padding, including failed creates.
                uint64_t capacity=0;
                if(!NorthlightGeometryMemory::add(cacheGrowthBytes,bytes,capacity)||
                   !NorthlightGeometryMemory::add(capacity,p.mesh().streams[0].stride,capacity)||
                   !admitsGrowth("replay-cache",capacity))return false;
                cacheGrowthBytes=capacity;return true;
            };
            NorthlightReplayGPU::bindResident(replayGpuCache,d,p,allowCache,growthAdmission);
        };
        replayTimeDeferredIndices.clear();
        bool needsBulk=false;
        {auto bind=streamingPhases.measure(NorthlightStreaming::PhaseProfile::ReplayBind);
        for(size_t i=0;i<replays.size();++i){bindReplay(*replays[i]);
            if(!replays[i]->gpuCached){needsBulk=true;if(replayGpuCache.timeDeferredLast())replayTimeDeferredIndices.push_back(i);}}
        NorthlightReplayGPU::commitAdmissions(replayGpuCache,replays,bindReplay);}
        phase.next(1); // Layout, bulk-buffer growth/copy and output bindings.
        UINT totals[4]={};std::uint64_t totalIndices=0;size_t sharedBulkDraws=0,sharedBulkBytes=0,multiStreamDraws=0,layoutScanned=0;
        replayBulkDraws.clear();
        /* 0.3.211 (task 31): a single-stream bulk replay sits at a stride multiple (rel) so every one binds the ring slot start
           and selects its vertices by BaseVertexIndex/start: same-stride replays share one SetStreamSource. Layout and the
           address-space prefix fit both go through bulkPlace, so they stay byte-identical. Multi-stream: 16-padded, as before. */
        auto singleStream=[](const auto& p){const auto& m=p.mesh();return !m.streams[0].bytes.empty()&&m.streams[0].stride&&m.streams[1].bytes.empty()&&m.streams[2].bytes.empty()&&m.streams[3].bytes.empty();};
        auto bulkPlace=[&](const auto& p,bool single,unsigned s,std::uint64_t total,std::uint64_t& at){ /* single: singleStream(p), computed once per replay */
            const auto& source=p.mesh().streams[s];
            if(s==0&&single){at=(total+source.stride-1)/source.stride*source.stride;return at+source.bytes.size();}
            at=(total+15)&~std::uint64_t(15);return at+((source.bytes.size()+15)&~std::size_t(15));};
        auto layout=[&](size_t count){
            for(auto& t:totals)t=0;totalIndices=0;sharedBulkDraws=sharedBulkBytes=multiStreamDraws=0;replayBulkLayout.begin();replayBulkDraws.clear();
            layoutScanned+=count;
            for(size_t i=0;i<count;++i){auto& p=replays[i];if(p->gpuCached)continue;
                replayBulkDraws.add(i);
                const size_t owner=replayBulkLayout.add(p->shared.get(),i);
                if(owner!=i){const auto& first=*replays[owner];
                    for(unsigned s=0;s<4;++s){p->stride[s]=first.stride[s];p->offset[s]=first.offset[s];}
                    p->start=first.start;p->base=first.base;p->bulkSingle=first.bulkSingle;++sharedBulkDraws;sharedBulkBytes+=p->mesh().byteSize();continue;
                }
                const bool single=p->bulkSingle=singleStream(*p);
                for(unsigned s=0;s<4;++s){auto& source=p->mesh().streams[s];p->stride[s]=source.stride;
                    std::uint64_t at=0;const std::uint64_t next=bulkPlace(*p,single,s,totals[s],at);
                    if(next>64u*1024u*1024u){layoutScanned-=count-i-1;return false;}
                    p->offset[s]=UINT(at);totals[s]=UINT(next);}
                p->start=p->indexed?UINT(totalIndices):0;p->base=0;totalIndices+=p->mesh().indices.size();
                if(single){const UINT first=p->offset[0]/p->stride[0];if(p->indexed)p->base=INT(first);else p->start=first;} // offset[0] is still relative here
                else multiStreamDraws+=!p->mesh().streams[1].bytes.empty()||!p->mesh().streams[2].bytes.empty()||!p->mesh().streams[3].bytes.empty();}
            return totalIndices<=16u*1024u*1024u;
        };
        // A creation-time deferral must never force bulk growth (or exceed the
        // bulk limits): then create those meshes exactly as 0.3.137 would.
        if(needsBulk){const bool fits=layout(replays.size());bool grow=!fits||totalIndices*4>replayBuffers.indexCapacity();
         for(unsigned s=0;s<4;++s)grow=grow||totals[s]>replayBuffers.vertexCapacity(s);
         if(grow&&!replayTimeDeferredIndices.empty()){
             replayGpuCache.ignoreTimeBudget(true);
             for(size_t i:replayTimeDeferredIndices){bindReplay(*replays[i]);replayBudgetOverrides+=replays[i]->gpuCached;}
             replayGpuCache.ignoreTimeBudget(false);
         }
         if(!fits||(grow&&!replayTimeDeferredIndices.empty())){if(!layout(replays.size()))return false;}}
        {const auto& cache=replayGpuCache.stats();streamingPhases.record(NorthlightStreaming::PhaseProfile::ReplayCreate,cache.createMs);
         replayCreatedPeak=std::max(replayCreatedPeak,cache.created);replayTimeDeferred+=cache.timeDeferred;}
        {const auto growth=replayBuffers.growthFor(totals,UINT(totalIndices*4));
         if(growth.needed&&!admitsGrowth("replay-growth",growth.capacity)){
             // Optional residency must not displace casters that the original
             // bulk path could fit. Reclaim it before applying the old fallback.
             if(allowCache&&replayGpuCache.bytes()){
                 if(captureSampled)++replayUploadFallbacks;
                 ++replayFallbacks;
                 phase.stop(); // The recursive retry owns its own interval.
                 return uploadReplay(false);
             }
             // Under address-space pressure draw the prefix of casters that fits
             // the existing buffers this frame; nothing is allocated.
             size_t fit=0;UINT running[4]={};std::uint64_t runningIndices=0;
             for(;fit<replays.size();++fit){auto& p=replays[fit];if(p->gpuCached||!replayBulkLayout.unique(fit))continue;bool ok=true;std::uint64_t nexts[4]={};const bool single=singleStream(*p);
                 for(unsigned s=0;s<4&&ok;++s){std::uint64_t at=0;nexts[s]=bulkPlace(*p,single,s,running[s],at);ok=nexts[s]<=replayBuffers.vertexCapacity(s);}
                 if(!ok||(runningIndices+p->mesh().indices.size())*4>replayBuffers.indexCapacity())break;
                 for(unsigned s=0;s<4;++s)running[s]=UINT(nexts[s]);runningIndices+=p->mesh().indices.size();}
             // The bounds job borrows packet constants. Join before truncating
             // the packet list, while its results still match the whole list.
             replayBoundsJoin();
             if(prepareUnsettled())for(size_t i=fit;i<replays.size();++i)recycleReplay(replays[i].release()); /* 0.3.177: quarantined */
             replays.resize(fit);if(!layout(fit))return false;
         }}
        const UINT indexBytes=UINT(totalIndices*4);
        if(!replayBuffers.upload(d,fence(),streamingPhases,replayGrowths,totals,indexBytes,replayBulkLayout,replayBulkDraws,replays,
              [&](HRESULT result,const char* action){return check(result,action);}))return false;
        phase.stop(); // Report I/O is not part of cache/bulk timing.
        if(captureSampled){size_t bulk=indexBytes;for(auto total:totals)bulk+=total;
            const auto residency=NorthlightReplayGPU::batchStats(replayGpuCache);
            deferLogf("GPU_SCENE consumer=shadows worldFrame=%u residentHits=%u crossConsumerHits=%u reusedDrawBytes=%zu ownedBytes=%zu ownedLimitMiB=%zu spareLoans=%u spareLoanedBytes=%zu sceneChargedBytes=%zu sceneLimitBytes=%zu",frames,replayGpuCache.counters().hits,replayGpuCache.counters().crossOwner,replayGpuCache.reused(),replayGpuCache.bytes(),replayGpuCache.limit()>>20,replayGpuCache.counters().loans,replayGpuCache.counters().loanedBytes,gpuScene->native.chargedBytes(),std::min(gpuScene->native.totalLimit(),gpuScene->materials.limit()+gpuScene->shadows.limit()));
            deferLogf("MODEL GPU cache hits=%u reusedBytes=%zu newBytes=%zu residentBytes=%zu bulkUploadBytes=%zu batches=%zu batchBytes=%zu liveBytes=%zu separateBytes=%zu batchedUploads=%u separateUploads=%u compactions=%u compactedBytes=%zu batchFailures=%u",replayGpuCache.hits(),replayGpuCache.reused(),replayGpuCache.uploaded(),replayGpuCache.bytes(),bulk,
                residency.batches,residency.batchBytes,residency.liveBytes,residency.separateBytes,residency.batchedUploads,residency.separateUploads,residency.compactions,residency.compactedBytes,residency.batchFailures);
            deferLogf("MODEL bulk sharing duplicateDraws=%zu avoidedBytes=%zu actualBytes=%zu multiStreamDraws=%zu packets=%zu bulkPackets=%zu layoutScanned=%zu indexedTraversal=%u",sharedBulkDraws,sharedBulkBytes,bulk,multiStreamDraws,replays.size(),replayBulkDraws.size(),layoutScanned,unsigned(replayBulkDraws.indexed()));
            const auto population=replayGpuCache.population();const auto& policy=replayGpuCache.stats();
            deferLogf("MODEL GPU policy entries=%zu resident=%zu probation=%zu countPressure=%u bytePressure=%u roomRejected=%u promotionCountOnly=%u promotionCountRejected=%u evictions=%u warmup=%u uploadDeferred=%u admissionRejected=%u entryLimit=%zu scanPasses=%u scanVisits=%u scanMemoHits=%u attemptDeferred=%u uploadByteDeferred=%u expiredEntries=%u expiredBytes=%zu evictionChecks=%u lruMoves=%u",population.entries,population.resident,population.probation,policy.countPressure,policy.bytePressure,policy.roomRejected,policy.promotionCountOnly,policy.promotionCountRejected,policy.evictions,policy.warmup,policy.uploadDeferred,policy.admissionRejected,replayGpuCache.entryLimit(),policy.scanPasses,policy.scanVisits,policy.scanMemoHits,policy.attemptDeferred,policy.uploadByteDeferred,policy.expiredEntries,policy.expiredBytes,policy.evictionChecks,policy.lruMoves);
            const auto& cleared=replayGpuCache.clearStats();
            deferLogf("MODEL GPU clears lifetimeCalls=%llu lifetimeEntries=%llu lifetimeBytes=%llu",(unsigned long long)cleared.calls,(unsigned long long)cleared.entries,(unsigned long long)cleared.bytes);
            const auto& captureWork=shadowPrograms.captureWork();
            deferLogf("SHADOW shader policy lifetimeCandidates=%llu compactCreated=%llu fullFallback=%llu fullWords=%llu compactWords=%llu",(unsigned long long)captureWork.candidates,(unsigned long long)captureWork.compactCreated,(unsigned long long)captureWork.fullFallback,(unsigned long long)captureWork.fullWords,(unsigned long long)captureWork.compactWords);
        }
        return true;
    }
