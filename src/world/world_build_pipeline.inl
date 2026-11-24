// CPU-only geometry builder and GI solver. Publication is guarded by the shared worker state.
    void work() {
      // Below-normal priority: the solve competes with the single game thread
      // for the same performance cores under Rosetta; it is latency-tolerant.
      SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
      try {
        // GIThreads>1: helpers at the same below-normal priority; probe values are scheduling-independent.
        std::unique_ptr<NorthlightGI::SolvePool> solvePool;
        NorthlightQuality::Settings solverQuality;solverQuality.giThreads=quality.giThreads;
        const unsigned solverThreads=NorthlightQuality::giSolverThreads(solverQuality,NorthlightStream::cores());
        if(solverThreads>1)solvePool=std::make_unique<NorthlightGI::SolvePool>(solverThreads-1,[]{SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);});
        if(quality.giThreads>1)logf("QUALITY GI solver threads requested=%u effective=%u cores=%u priority=below-normal",quality.giThreads,solverThreads,NorthlightStream::cores());
        // 0.3.153: geometry regions are built on their own below-normal thread
        // while this worker keeps solving camera moves against the published
        // previous generation. The builder alone owns the local geometry cache,
        // memory admission, light cache and palette region; it never touches GI
        // state. Guarded by mutex: the finished build, the worker's request for
        // one after it dropped its region, and builder exit.
        struct Built {std::shared_ptr<NorthlightGI::BVH> bvh;std::shared_ptr<NorthlightWorldMesh::WorldMeshUploadPlan> plan;std::shared_ptr<const PreparedCommit> prepared;
            std::shared_ptr<const NorthlightRegionalFog::Field> fog;std::vector<NorthlightGI::PointLight> lights;std::vector<NorthlightLocalLights::Light> rawLights;
            std::shared_ptr<const NorthlightLightAttachments::Records> lightAttachments;
            std::string map;V center;uint64_t id=0;DWORD ms=0;unsigned superseded=0;};
        std::unique_ptr<Built> builtGeometry;bool geometryWanted=false,builderExit=false;
        std::condition_variable buildWake;std::thread builder;
        // Joins before anything the builder uses is destroyed, also when this worker faults.
        struct BuilderJoin {std::mutex& mutex;std::condition_variable& wake;bool& exit;std::thread& thread;
            ~BuilderJoin(){{std::lock_guard<std::mutex> lock(mutex);exit=true;}wake.notify_all();if(thread.joinable())thread.join();}} builderJoin{mutex,buildWake,builderExit,builder};
        builder=std::thread([&]{
          SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
          try {
          NorthlightGeometryMemory::Generations<NorthlightGI::BVH> generations;
          // Reuse sources, placement acceleration and shadow preparation under
          // the former 32 MiB local-cache allowance, including live GI leases.
          struct Preparation {NorthlightGI::LocalSceneCache geometry{NorthlightGI::LocalSceneCache::Limits{16u*1024u*1024u}};NorthlightWorldMesh::PreparationCache mesh;Preparation(){mesh.setLimit(8u*1024u*1024u);}};
          auto prepared=std::make_shared<Preparation>();
          std::atomic_store(&gpuScene->workerPreparation,std::shared_ptr<void>(prepared));
          struct Unpublish {std::shared_ptr<void>& slot;~Unpublish(){std::atomic_store(&slot,std::shared_ptr<void>{});}} unpublish{gpuScene->workerPreparation};
          NorthlightGI::LocalSceneCache pressureGeometry{NorthlightGI::LocalSceneCache::Limits{0}};
          auto& localGeometry=prepared->geometry;auto& meshPreparation=prepared->mesh;
          auto& sceneSources=*sharedSceneSources;
          const NorthlightGI::SceneSourceProvider sceneSource=sceneSources.provider();
          const NorthlightGI::SceneSourceRevision sceneRevision=[&](const std::string& path){return sceneSources.findIdentity(path);};
          std::string preparationMap;
          auto resetPreparation=[&]{meshPreparation.reset();sceneSources.reset();localGeometry.reset();};
          // Independent worker-owned address witness. Every allocation still
          // queries CURRENT aggregate free memory and a CURRENT free region;
          // a stale/split witness falls back to the complete address-space walk.
          NorthlightMemoryAdmission::Probe workerAdmissionProbe;
          bool workerMemoryExact=true;double workerMemoryMs=0;
          NorthlightGeometryMemory::Sample lastWorkerMemory;DWORD lastWorkerMemoryAt=0;
          uint64_t minimumBuildAvailable=UINT64_MAX;bool profileBuildActive=false;
          auto workerMemory=[&](NorthlightGeometryMemory::Budget budget=NorthlightGeometryMemory::buildBudget()){
              const auto begin=std::chrono::steady_clock::now();
              const auto result=geometryAdmissionFor(workerAdmissionProbe,budget,&workerMemoryExact);
              if(profileBuildActive){lastWorkerMemory=result;lastWorkerMemoryAt=GetTickCount();if(result.valid)minimumBuildAvailable=std::min(minimumBuildAvailable,result.available);}
              workerMemoryMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
              return result;
          };
          DWORD memoryReportAt=0,memoryFootprintReportAt=0;
          DWORD fogAttemptAt=0;std::string fogAttemptMap;
#if NORTHLIGHT_DEV_TUNING
          std::shared_ptr<const NorthlightZoneTuning::Overrides> fogTuning;
#endif
          NorthlightTerrainReach::State reachState;NorthlightGI::SceneSizeHint terrainSizeHint; /* terrain scene counts of the last build; keyed by reach, reset per map */NorthlightTerrainReach::Episode memoryStall;
          NorthlightGeometryMemory::EntryLift entryLift,chunkLift,coldLift;
          std::string entryLiftMap;V entryLiftCenter;
          // Episode logs (begin on the first memory refusal, end when the build is delivered or dropped).
          // `required` is the refused check's aggregate floor: published (at most the plain entry floor)
          // as the memory guard's demand, so the mod's own caches shrink while the builder waits.
          auto stallBegin=[&](const char* stage,NorthlightGeometryMemory::Sample sample,uint64_t required,uint64_t requiredLargest=0){
              NorthlightMemMap::deferredRequest.store(true,std::memory_order_relaxed); /* task 42: the Device queues a MEMMAP memory-deferred point (throttled there) */
              geometryMemoryDemand.store(std::min(required,NorthlightGeometryMemory::buildBudget().available),std::memory_order_relaxed);
              if(memoryStall.refused(GetTickCount())){
                  char largest[32];NorthlightGeometryMemory::formatLargestMiB(largest,sizeof largest,sample.largest,workerMemoryExact);
                  NorthlightGeometryMemory::Budget checked;checked.available=required;checked.largest=requiredLargest;checked.valid=true;
                  const auto why=NorthlightGeometryMemory::refusal(sample,checked);
                  logf("WORLD geometry memory stall begin stage=%s availableMiB=%llu largestMiB=%s generations=%zu refused=%s need=%llu",
                      stage,(unsigned long long)(sample.available>>20),largest,generations.live(),why.limit,(unsigned long long)(why.need>>20));}
          };
          auto stallEnd=[&](const char* outcome){
              geometryMemoryDemand.store(0,std::memory_order_relaxed);
              uint32_t ms=0;unsigned tries=0;
              if(memoryStall.end(GetTickCount(),ms,tries))logf("WORLD geometry memory stall end ms=%u attempts=%u outcome=%s",ms,tries,outcome);
          };
          auto abandonGeometry=[&](){
              // A measured peak belongs to the abandoned spatial region. It
              // must not refuse a different region before that build starts.
              entryLift.clear();chunkLift.clear();coldLift.clear();terrainSizeHint={};stallEnd("superseded");
          };
          NorthlightLocalLights::Cache lightCache(root+"world-cache/lights");
          NorthlightLightAttachments::Cache lightAttachmentCache(root+"world-cache/lights");
          // Last delivered region. Any build that does not deliver forces the next
          // request to build again, as the 0.3.151 worker did without a BVH.
          std::string builtMap;V builtCenter;bool builtValid=false,wanted=false,retry=false;uint64_t seen=0;unsigned superseded=0;
          std::string cacheCheckedMap;bool cacheMapHasTiles=true,uncachedPublished=false;unsigned uncachedSkips=0;uint64_t uncachedSkippedId=0;
          for(;;){
            Request r;std::shared_ptr<const Snapshot> restore;
            {std::unique_lock<std::mutex> lock(mutex);buildWake.wait(lock,[&]{return stopping||builderExit||retry||geometryWanted||request.id!=seen;});
             if(stopping||builderExit)return;r=request;seen=r.id;retry=false;wanted=wanted||geometryWanted;geometryWanted=false;
             if(payloadRebuildSource){
                 if(r.enabled&&!r.menu&&payloadRebuildSource->bvh&&NorthlightWorldStreaming::applicable(payloadRebuildSource->map,payloadRebuildSource->center,r.map,r.camera)){
                     restore=payloadRebuildSource;r.geometryCenter=restore->center;wanted=true;
                 }else payloadRebuildSource.reset();
             }}
            if(!r.enabled||r.menu)continue;
            profileBuildActive=false;
            if(workerMemoryTrim.exchange(false,std::memory_order_relaxed))resetPreparation(); /* the entry lift stays: it is a floor on measured free memory, so what a trim freed already counts */
            if(preparationMap!=r.map){resetPreparation();preparationMap=r.map;}
            // Publish small authored zone data before expensive geometry work.
            // No cache reads are performed by the sky or world draw callbacks.
            int paletteTX=int(std::floor((NorthlightRegionalFog::WorldZero-r.camera.y)/NorthlightRegionalFog::TileSize));
            int paletteTY=int(std::floor((NorthlightRegionalFog::WorldZero-r.camera.x)/NorthlightRegionalFog::TileSize));
            std::shared_ptr<const PaletteRegion> paletteRegion;
            {std::lock_guard<std::mutex> lock(mutex);paletteRegion=publishedPaletteRegion;}
            if(!paletteRegion||paletteRegion->map!=r.map||paletteRegion->tx!=paletteTX||paletteRegion->ty!=paletteTY){
                auto next=std::make_shared<PaletteRegion>();next->map=r.map;next->tx=paletteTX;next->ty=paletteTY;
                next->region=NorthlightRegionalFog::loadRegion(root+"world-cache/fog",r.map,r.camera.x,r.camera.y);
                paletteRegion=next;
                {std::lock_guard<std::mutex> lock(mutex);publishedPaletteRegion=next;}
            }
            std::shared_ptr<const FogRegion> fogRegion;
            {std::lock_guard<std::mutex> lock(mutex);fogRegion=publishedFogRegion;}
#if NORTHLIGHT_DEV_TUNING
            const auto requestedFogTuning=devSnapshot();
            const bool fogTuningChanged=requestedFogTuning!=fogTuning;
#else
            const bool fogTuningChanged=false;
#endif
            // BEGIN FOG FIELD BUILD: independent of GI/shadow entry admission.
            const bool fogNeeded=r.fogEnabled&&!paletteRegion->region.tiles.empty()&&
                (!fogRegion||fogRegion->map!=r.map||fogTuningChanged||!NorthlightWorldStreaming::within(fogRegion->center,r.camera,NorthlightRegionalFog::RefreshDistance));
            const DWORD fogNow=GetTickCount();
            if(fogNeeded&&(fogAttemptMap!=r.map||!fogAttemptAt||fogNow-fogAttemptAt>=1000)){
                fogAttemptMap=r.map;fogAttemptAt=fogNow;
                const auto fogStart=std::chrono::steady_clock::now();
                bool fogDeferred=false;const auto fogBudget=NorthlightGeometryMemory::inBuildBudget();
                auto fogMemory=workerMemory(fogBudget);
                NorthlightGI::AllocationAdmission fogAdmission=[&](uint64_t bytes){
                    if(bytes<NorthlightGeometryMemory::MiB)return true;
                    const auto budget=NorthlightGeometryMemory::inBuildBudget(bytes);fogMemory=workerMemory(budget);
                    if(NorthlightGeometryMemory::admits(fogMemory,budget))return true;fogDeferred=true;return false;
                };
                std::string fogError;
                try{
                    auto field=std::make_shared<NorthlightRegionalFog::Field>();
                    const NorthlightRegionalFog::ZoneScale* scale=nullptr;
#if NORTHLIGHT_DEV_TUNING
                    const NorthlightRegionalFog::ZoneScale zoneScale=[&](uint32_t zone,float& day,float& nightExtra){NorthlightZoneTuning::fogScale(*requestedFogTuning,r.map,zone,day,nightExtra);};scale=&zoneScale;
#endif
                    if(NorthlightGeometryMemory::admits(fogMemory,fogBudget)&&NorthlightRegionalFog::buildCachedField(root+"world-cache",r.map,r.camera,paletteRegion->region,*field,fogError,fogAdmission,sceneSource,scale)){
                        auto next=std::make_shared<FogRegion>();next->map=r.map;next->center=r.camera;next->field=std::move(field);next->requestId=r.id;
                        bool accepted=false;
                        {std::lock_guard<std::mutex> lock(mutex);if(request.map==r.map&&NorthlightWorldStreaming::within(next->center,request.camera,NorthlightRegionalFog::RefreshDistance)){publishedFogRegion=next;fogRegion=std::move(next);accepted=true;}}
#if NORTHLIGHT_DEV_TUNING
                        if(accepted)fogTuning=requestedFogTuning;
#endif
                        if(!accepted)fogAttemptAt=0;
                        if(accepted&&NorthlightDiagnostics::enabled())logf("FOG FIELD ready map=\"%s\" request=%llu ground=%u air=%u fog=%u cpuKiB=%zu buildMs=%.3f bvhRequired=0 meshRequired=0",r.map.c_str(),(unsigned long long)r.id,fogRegion->field->groundCells,fogRegion->field->airCells,fogRegion->field->fogCells,sizeof(NorthlightRegionalFog::Field)/1024,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-fogStart).count());
                    }else if(NorthlightDiagnostics::enabled())logf("FOG FIELD deferred map=\"%s\" request=%llu memory=%u availableMiB=%llu requiredMiB=%llu reason=%s",r.map.c_str(),(unsigned long long)r.id,unsigned(fogDeferred||!NorthlightGeometryMemory::admits(fogMemory,fogBudget)),(unsigned long long)(fogMemory.available>>20),(unsigned long long)(fogBudget.available>>20),fogError.c_str());
                }catch(const std::bad_alloc&){logf("FOG FIELD deferred map=\"%s\" reason=allocation; previous field retained",r.map.c_str());}
            }
            // END FOG FIELD BUILD
            if(!wanted&&builtValid&&!NorthlightWorldStreaming::needsGeometry(builtMap,builtCenter,r.map,r.geometryCenter))continue;
            builtValid=false;
            auto result=std::make_shared<Snapshot>();result->map=r.map;result->center=r.geometryCenter;
            DWORD started=GetTickCount();result->requestId=r.id;result->requestedAt=r.queuedAt;result->queueMs=started-r.queuedAt;
            minimumBuildAvailable=UINT64_MAX;uint64_t maximumObservedCapacity=0;double memoryProfileMs=0;
            const bool profileThisBuild=NorthlightDiagnostics::enabled()&&(!memoryFootprintReportAt||started-memoryFootprintReportAt>=10000);
            profileBuildActive=profileThisBuild;
            if(profileThisBuild)memoryFootprintReportAt=started;
            bool chunkedLocal=bool(restore),coldRecovery=bool(restore),spoolTerrain=false;
            auto profileMemory=[&](const char* stage,const NorthlightWorldMesh::WorldMeshUploadPlan* plan=nullptr,const NorthlightGI::WorldScene* scratch=nullptr,const NorthlightWorldMeshPages::Plan* pages=nullptr){
                if(!profileThisBuild)return;
                const auto profileStart=std::chrono::steady_clock::now();
                using namespace NorthlightMemoryFootprint;
                Counter memory;
                generations.forEach([&](const NorthlightGI::BVH& bvh){bvh.accountMemory(memory);});
                localGeometry.accountMemory(memory);sceneSources.accountMemory(memory);meshPreparation.accountMemory(memory);
                std::shared_ptr<Snapshot> publication;std::shared_ptr<NorthlightWorldMesh::WorldMeshUploadPlan> pendingPlan;
                {std::lock_guard<std::mutex> lock(mutex);publication=published;if(builtGeometry)pendingPlan=builtGeometry->plan;}
                if(publication){if(publication->bvh)publication->bvh->accountMemory(memory);if(publication->meshPlan)publication->meshPlan->accountMemory(memory);
                    NorthlightGI::accountProbeAtlas(memory,publication->atlas);if(publication->coarseAtlas)NorthlightGI::accountProbeAtlas(memory,*publication->coarseAtlas);memory.vector(Metadata,publication->localLights);
                    if(publication->localLightIndex)memory.vector(Metadata,*publication->localLightIndex);
                    if(publication->fogField)memory.allocation(Fog,publication->fogField.get(),sizeof(*publication->fogField));}
                if(pendingPlan)pendingPlan->accountMemory(memory);
                if(plan)plan->accountMemory(memory);if(scratch)scratch->accountMemory(memory);if(pages)pages->accountMemory(memory);
                if(paletteRegion){memory.vector(Fog,paletteRegion->region.tiles);for(const auto& tile:paletteRegion->region.tiles){memory.vector(Fog,tile.water);memory.vector(Fog,tile.indoors);}}
                if(fogRegion&&fogRegion->field)memory.allocation(Fog,fogRegion->field.get(),sizeof(*fogRegion->field));
                maximumObservedCapacity=std::max(maximumObservedCapacity,memory.total());
                logCpuFootprint("builder-reachable",stage,r.id,memory);
                const auto entryBudget=coldRecovery?coldLift.apply(NorthlightGeometryMemory::coldChunkBuildBudget(),GetTickCount()):chunkedLocal?chunkLift.apply(NorthlightGeometryMemory::chunkBuildBudget(),GetTickCount()):entryLift.apply(NorthlightGeometryMemory::buildBudget(64*NorthlightGeometryMemory::MiB),GetTickCount());
                NorthlightMemoryProfile::Admission admission;
                admission.available=lastWorkerMemory.available;admission.largest=lastWorkerMemory.largest;admission.valid=lastWorkerMemory.valid;admission.largestExact=workerMemoryExact;
                admission.entryRequired=entryBudget.available;admission.largestRequired=entryBudget.largest;
                admission.reserve=NorthlightGeometryMemory::ProcessReserve;admission.entryHeadroom=coldRecovery?NorthlightGeometryMemory::ColdEntryHeadroom:chunkedLocal?NorthlightGeometryMemory::EntryHeadroom-32*NorthlightGeometryMemory::MiB:NorthlightGeometryMemory::EntryHeadroom;admission.interiorHeadroom=(coldRecovery||spoolTerrain)?NorthlightGeometryMemory::ColdInBuildHeadroom:NorthlightGeometryMemory::InBuildHeadroom;
                admission.minimumAvailable=minimumBuildAvailable==UINT64_MAX?0:minimumBuildAvailable;admission.maximumKnownCapacity=maximumObservedCapacity;
                std::strncpy(admission.stage,stage,sizeof(admission.stage)-1);NorthlightMemoryProfile::store().admission(admission);
                logf("WORLD MEMORY admission stage=%s buildId=%llu processReserveMiB=%llu entryHeadroomMiB=%llu interiorHeadroomMiB=%llu entryRequiredMiB=%llu availableMiB=%llu minObservedAvailableMiB=%llu maxObservedCapacityMiB=%.3f sampleAgeMs=%lu sampleValid=%u sourceChargedInclusiveMiB=%.3f sourceDecodedInclusiveMiB=%.3f sourceUncachedLeaseMiB=%.3f preparationChargedInclusiveMiB=%.3f ledgersNotAdditive=1 peaksAreSampled=1",
                     stage,(unsigned long long)r.id,(unsigned long long)(NorthlightGeometryMemory::ProcessReserve>>20),(unsigned long long)(admission.entryHeadroom>>20),(unsigned long long)(admission.interiorHeadroom>>20),
                     (unsigned long long)(entryBudget.available>>20),(unsigned long long)(lastWorkerMemory.available>>20),(unsigned long long)(minimumBuildAvailable==UINT64_MAX?0:minimumBuildAvailable>>20),double(maximumObservedCapacity)/1048576,
                     (unsigned long)(GetTickCount()-lastWorkerMemoryAt),unsigned(lastWorkerMemory.valid),double(sceneSources.liveBytes())/1048576,double(sceneSources.decodedLiveBytes())/1048576,double(sceneSources.uncachedLeaseBytes())/1048576,double(meshPreparation.stats().bytes)/1048576);
                memoryProfileMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-profileStart).count();
            };
            auto publishError=[&](){
                stallEnd("error"); // the build ends here whatever the publication outcome
                std::lock_guard<std::mutex> lock(mutex);
                if(stopping||!request.enabled||request.id!=r.id){++superseded;return false;}
                // A failed replacement must not replace usable geometry with
                // an empty error snapshot. Its existing spatial expiry remains.
                if(published&&published->bvh&&NorthlightWorldStreaming::retained(published->map,published->center,request.map,request.camera)){
                    logf("WORLD replacement deferred; previous region retained: %s",result->message.c_str());return false;
                }
                logf("WORLD geometry unavailable: %s",result->message.c_str());
                published=result;return true;
            };
            // Geometry validity is spatial, independent of the latest GI
            // request ID. A camera retarget must not discard a usable build.
            auto currentGeometry=[&](){std::lock_guard<std::mutex> lock(mutex);return !stopping&&request.enabled&&NorthlightWorldStreaming::applicable(r.map,r.geometryCenter,request.map,request.camera);};
            auto deferBuild=[&](const char* stage,NorthlightGeometryMemory::Sample sample,unsigned delay,const NorthlightGeometryMemory::Budget* budget=nullptr){
                DWORD now=GetTickCount();if(!memoryReportAt||now-memoryReportAt>=1000){memoryReportAt=now;logGeometryMemory(stage,sample,generations.live(),workerMemoryExact,budget);}
                profileMemory(stage);
                // GI retargets inside this region cannot make a failed memory
                // admission succeed. Keep its backoff; next loop reads latest
                // state. Stop or a spatially invalid request still wakes now.
                bool abandoned=false;
                {std::unique_lock<std::mutex> lock(mutex);retry=true;
                 buildWake.wait_for(lock,std::chrono::milliseconds(delay),[&]{return stopping||builderExit||!request.enabled||!NorthlightWorldStreaming::applicable(r.map,r.geometryCenter,request.map,request.camera);});
                 // The request moved on and this region no longer applies: the stalled build is dropped.
                 abandoned=stopping||builderExit||!request.enabled||!NorthlightWorldStreaming::applicable(r.map,r.geometryCenter,request.map,request.camera);}
                if(abandoned)abandonGeometry();
            };
            // Issue #40: a map without world-cache tiles (dungeons, raids, battlegrounds) is checked
            // once per entry; its error is published once and no build runs until the map changes.
            if(r.map!=cacheCheckedMap){
                if(!cacheMapHasTiles&&NorthlightDiagnostics::enabled())logf("WORLD cache absent end map=\"%s\" skippedBuilds=%u",cacheCheckedMap.c_str(),uncachedSkips);
                cacheCheckedMap=r.map;cacheMapHasTiles=worldCacheHasTiles(r.map);uncachedPublished=false;uncachedSkips=0;entryLift.clear();chunkLift.clear();coldLift.clear();reachState={};terrainSizeHint={};
                if(!cacheMapHasTiles)logf("WORLD cache absent map=\"%s\"; geometry builds skipped until the map changes",r.map.c_str());
            }
            if(!cacheMapHasTiles){
                if(!uncachedPublished){result->message="No cached geometry for "+r.map;uncachedPublished=publishError();}
                else if(r.id!=uncachedSkippedId){uncachedSkippedId=r.id;++uncachedSkips;} /* one count per GI request, not per builder wake */
                continue;
            }
            // Keep the refused attempt's anchor: a stream of small requests
            // must not carry its peak estimate across arbitrarily far travel.
            if((entryLift.armed||chunkLift.armed||coldLift.armed)&&!NorthlightWorldStreaming::applicable(entryLiftMap,entryLiftCenter,r.map,r.camera)){
                entryLift.clear();chunkLift.clear();coldLift.clear();terrainSizeHint={};
            }
            auto built=std::make_unique<Built>();
#if NORTHLIGHT_DEV_TUNING
            const auto tuning=devSnapshot(); // ONE snapshot per build: the terrain reach and the ground fog scales both use it
#endif
#include "world_region_build.inl"
            // END REGION BUILD
            built->id=r.id;built->ms=GetTickCount()-started;built->superseded=superseded;
            geometryBuildEstimateMs.store((3*geometryBuildEstimateMs.load(std::memory_order_relaxed)+unsigned(std::min<DWORD>(built->ms,4000)))/4,std::memory_order_relaxed);
            // Replaces an unadopted older build; it satisfies every earlier worker request.
            // The replaced build (tens of MB) is freed after unlocking, never under the mutex.
            {std::lock_guard<std::mutex> lock(mutex);if(stopping||builderExit)return;
             if(!request.enabled){++superseded;continue;}
             std::swap(builtGeometry,built);geometryWanted=false;if(restore&&payloadRebuildSource==restore)payloadRebuildSource.reset();}
#if NORTHLIGHT_DEV_TUNING
            {std::lock_guard<std::mutex> lock(devMutex);devBuilt=tuning;} // what the last published build used
            devBuilds.fetch_add(1,std::memory_order_relaxed);
#endif
            wake.notify_one();built.reset();
            stallEnd(memoryStall.reduced?"reduced":"published");entryLift.clear();chunkLift.clear();coldLift.clear();
            builtMap=r.map;builtCenter=r.geometryCenter;builtValid=true;wanted=false;superseded=0;
          }
          }catch(const std::bad_alloc&){workerFaultCode.store(1);}
           catch(const std::exception&){workerFaultCode.store(2);}
           catch(...){workerFaultCode.store(3);}
        });
        std::shared_ptr<NorthlightGI::BVH> bvh;std::shared_ptr<NorthlightWorldMesh::WorldMeshUploadPlan> scenePlan;std::shared_ptr<const PreparedCommit> scenePrepared;V sceneCenter;std::shared_ptr<const NorthlightRegionalFog::Field> sceneFog;std::string sceneMap;uint64_t serial=0,sceneBuild=0;
        std::shared_ptr<Snapshot> previousLighting;
        NorthlightGI::ProbeCache probeCache;
        NorthlightGI::CoarseProbeLayer coarseProbes;
        NorthlightGI::ProbeVisibilityCache visibilityCache;
        NorthlightGI::ProbeRelightCache relightCache;std::string relightMap;
        NorthlightGI::DynamicProbeLayer dynamicProbes;
        std::vector<NorthlightGI::PointLight> sceneLights;
        std::vector<NorthlightLocalLights::Light> rawSceneLights;
        std::shared_ptr<const NorthlightEquipmentLights::StaticLightIndex> sceneLightIndex;
        std::shared_ptr<const NorthlightLightAttachments::Records> sceneLightAttachments;
        std::shared_ptr<NorthlightGI::BVH> actors;
        NorthlightActorGeometry::TexelKeys actorTexelKeys;
        NorthlightWorkerActorMemo<NorthlightActorGeometry::ActorJob> actorMemo,menuActorMemo;
        uint64_t solvedActorHash=0,completedActorHash=0,completedBaseId=0,menuGeometryHash=0;
        std::shared_ptr<NorthlightGI::BVH> probeCacheGeometry;
        NorthlightGI::Lighting probeCacheLight;NorthlightGI::PreparedLighting preparedLight;
        NorthlightGI::ProbePublicationCadence publicationCadence;
        std::vector<NorthlightGI::ProbeAtlasEntry> displayFallback;
        uint64_t lightingGeneration=0;unsigned superseded=0,retargeted=0;
        NorthlightGI::MovingSolveStats movingStats;
        DWORD giMemoryProfileAt=0;
        auto profileGiMemory=[&](const char* stage,uint64_t id){
            if(!NorthlightDiagnostics::enabled())return;
            const DWORD now=GetTickCount();if(giMemoryProfileAt&&now-giMemoryProfileAt<10000)return;giMemoryProfileAt=now;
            const auto started=std::chrono::steady_clock::now();
            using namespace NorthlightMemoryFootprint;Counter memory;
            if(bvh)bvh->accountMemory(memory);if(actors)actors->accountMemory(memory);if(probeCacheGeometry)probeCacheGeometry->accountMemory(memory);
            if(scenePlan)scenePlan->accountMemory(memory);
            coarseProbes.accountMemory(memory);probeCache.accountMemory(memory);relightCache.accountMemory(memory);dynamicProbes.accountMemory(memory);visibilityCache.accountMemory(memory);
            NorthlightGI::accountProbeAtlas(memory,displayFallback);memory.vector(Metadata,sceneLights);memory.vector(Metadata,rawSceneLights);memory.vector(Metadata,actorTexelKeys);
            if(sceneLightIndex)memory.vector(Metadata,*sceneLightIndex);
            if(sceneFog)memory.allocation(Fog,sceneFog.get(),sizeof(*sceneFog));
            if(previousLighting){NorthlightGI::accountProbeAtlas(memory,previousLighting->atlas);if(previousLighting->coarseAtlas)NorthlightGI::accountProbeAtlas(memory,*previousLighting->coarseAtlas);if(previousLighting->bvh)previousLighting->bvh->accountMemory(memory);if(previousLighting->meshPlan)previousLighting->meshPlan->accountMemory(memory);}
            logCpuFootprint("gi-worker-reachable",stage,id,memory);
            logf("WORLD MEMORY probes buildId=%llu cached=%zu capacity=%zu relightInclusiveMiB=%.3f pathInclusiveMiB=%.3f diagnosticMs=%.3f ledgersNotAdditive=1",(unsigned long long)id,probeCache.size(),probeCache.capacity(),double(relightCache.stats().bytes)/1048576,double(dynamicProbes.pathBytes())/1048576,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count());
        };
        // 0.3.153 per geometry swap: passes completed on the previous generation
        // while the builder ran, and time with no usable region (logged at publication).
        unsigned concurrentSolves=0;DWORD stallMs=0,stallStart=0;bool stalled=false;std::string stallMap;
        const bool fixedGi=NorthlightFrameParityClock::fixedGiPublication();
        if(fixedGi)logf("%s","GI_PARITY_POLICY completed-near-and-coarse");
        for(;;){
            Request r;std::unique_ptr<Built> adopted;
            if(!takeRequest(r,builtGeometry,adopted,geometryWanted))return;
            buildWake.notify_one(); /* the builder follows the same request stream */
            const auto finish=[&]{finishRequest(fixedGi);};
            struct WorkerIdle {decltype(finish)& done;~WorkerIdle(){done();}} idle{finish};
            if(!r.enabled)continue;
            profileGiMemory("request",r.id);
            // No solve runs here: a memory trim or a new map frees the relight slots (1-4 MiB); configure() allocates them again.
            if(giMemoryTrim.exchange(false,std::memory_order_relaxed)||(!relightMap.empty()&&relightMap!=r.map))relightCache.release();
            relightMap=r.map;
            auto result=std::make_shared<Snapshot>();result->map=r.map;result->center=r.camera;
            DWORD started=GetTickCount();result->requestId=r.id;result->requestedAt=r.queuedAt;result->queueMs=started-r.queuedAt;
            bool concurrent=false;
            if(r.menu){
                adopted.reset();
                if(!bvh||sceneMap!=r.map||!menuActorMemo.matches(r.actorJob)){
                    auto geometry=r.actorJob?r.actorJob->resolve():NorthlightActorGeometry::Result{};
                    if(sceneMap!=r.map||!bvh||geometry.hash!=menuGeometryHash){
                        auto next=std::make_shared<NorthlightGI::BVH>();std::string error;
                        if(geometry.scene&&!geometry.scene->triangles.empty()&&!next->build(std::move(*geometry.scene),error,quality.giFastBVH!=0))continue;
                        bvh=std::move(next);menuGeometryHash=geometry.hash;sceneMap=r.map;sceneCenter=r.camera;
                        scenePlan=std::make_shared<NorthlightWorldMesh::WorldMeshUploadPlan>();scenePrepared.reset();sceneFog.reset();rawSceneLights.clear();sceneLightIndex.reset();sceneLightAttachments.reset();
                        actors.reset();actorTexelKeys.clear();actorMemo={};solvedActorHash=0;dynamicProbes.reset(nullptr);
                    }
                    menuActorMemo.remember(r.actorJob);
                }
                sceneCenter=r.camera;
                sceneLights.clear();
                for(unsigned i=0;i<r.menuLights.count;++i){const auto& p=r.menuLights.lights[i];
                    // Use the same bounded denominator fit as the direct-light owner.
                    NorthlightEquipmentLights::Native native;std::copy_n(p.position,3,native.position);std::copy_n(p.color,3,native.color);std::copy_n(p.attenuation,3,native.attenuation);
                    const auto fitted=NorthlightEquipmentLights::light(native,p.id,1.f);
                    sceneLights.push_back({vec(fitted.position),vec(fitted.diffuse)*3.14159265f,fitted.attenuationStart,fitted.attenuationEnd});
                }
                if(!r.actorJob)r.giEnabled=false;
            }
            if(!r.menu)menuActorMemo={};
            if(adopted){
                bvh=std::move(adopted->bvh);scenePlan=std::move(adopted->plan);scenePrepared=std::move(adopted->prepared);sceneFog=std::move(adopted->fog);
                sceneLights=std::move(adopted->lights);rawSceneLights=std::move(adopted->rawLights);sceneLightAttachments=std::move(adopted->lightAttachments);sceneMap=std::move(adopted->map);sceneCenter=adopted->center;
                sceneLightIndex.reset();
                if(!rawSceneLights.empty())try{
                    sceneLightIndex=std::make_shared<const NorthlightEquipmentLights::StaticLightIndex>(NorthlightEquipmentLights::indexStaticLights(rawSceneLights));
                }catch(const std::bad_alloc&){/* The published lights can still use the linear search. */}
                sceneBuild=adopted->id;superseded+=adopted->superseded;if(stalled){if(stallMap==sceneMap)stallMs+=GetTickCount()-stallStart;stalled=false;} /* an episode begun on another map is not this region's stall */
            }else if(!r.menu&&(!bvh||NorthlightWorldStreaming::needsGeometry(sceneMap,sceneCenter,r.map,r.geometryCenter))){
                // The builder is replacing this region. Borrow the published
                // previous generation and keep its probe cache while the original
                // 64-unit GI region holds; the new generation clears it on adoption.
                bool serve;
                {std::lock_guard<std::mutex> lock(mutex);
                 serve=bvh&&sceneMap==r.map&&NorthlightWorldStreaming::within(sceneCenter,r.camera,64)&&published&&published->bvh==bvh;
                 if(!serve){
                     if(published&&published->bvh==bvh&&bvh){
                         previousLighting=std::make_shared<Snapshot>(*published);
                         previousLighting->bvh.reset();previousLighting->meshPlan.reset();
                     }
                     geometryWanted=true;
                 }}
                if(!serve){
                    // Release worker ownership, but leave the last published
                    // complete region available to the renderer until a
                    // replacement arrives or its unchanged coverage ends.
                    // Only the first drop has a region to release; adoption resets the probe state again.
                    if(bvh){
                        probeCache.clear();probeCacheGeometry.reset();visibilityCache.reset();
                        dynamicProbes.reset(actors?&actors->scene():nullptr);
                        scenePlan.reset();scenePrepared.reset();sceneFog.reset();bvh.reset();
                    }
                    if(stalled&&stallMap!=r.map)stalled=false; /* the episode began on another map */
                    if(!stalled){stalled=true;stallStart=started;stallMap=r.map;}
                    buildWake.notify_one();continue;
                }
                concurrent=true;
            }
            result->geometryMs=adopted?adopted->ms:GetTickCount()-started;
            // BEGIN GEOMETRY HANDOFF: runs before actor work and GI request-ID
            // cancellation. Tests execute this production block with retargets.
            {std::lock_guard<std::mutex> lock(mutex);
             if(stopping)return;
             if(!request.enabled)continue;
             if(!NorthlightWorldStreaming::applicable(sceneMap,sceneCenter,request.map,request.camera)){
                 ++superseded;pending=true;continue;
             }
             if(!published||published->bvh!=bvh||published->meshPlan!=scenePlan){
                 auto geometry=std::make_shared<Snapshot>();
                 if(published&&published->map==sceneMap)*geometry=*published;
                 else if(previousLighting&&previousLighting->map==sceneMap)*geometry=*previousLighting;
                 geometry->bvh=bvh;geometry->meshPlan=scenePlan;geometry->prepared=scenePrepared;geometry->retirementBytes=bvh->retainedBytes()+scenePlan->cpuBytes()+sizeof(Snapshot);
                 geometry->map=sceneMap;geometry->center=sceneCenter;geometry->fogField=sceneFog;geometry->localLights=rawSceneLights;geometry->lightAttachments=sceneLightAttachments;
                 geometry->localLightIndex=sceneLightIndex;
                 published=geometry;previousLighting.reset();
                 if(NorthlightDiagnostics::enabled())logf("WORLD geometry published buildId=%llu latestId=%llu buildMs=%lu cameraLag=%.2f retainedGI=%u refreshDistance=32 coverageDistance=96 concurrentSolves=%u stallMs=%lu",
                     (unsigned long long)sceneBuild,(unsigned long long)request.id,(unsigned long)result->geometryMs,
                     std::sqrt(NorthlightGI::dot(request.camera-sceneCenter,request.camera-sceneCenter)),unsigned(geometry->serial!=0),concurrentSolves,(unsigned long)stallMs);
                 concurrentSolves=0;stallMs=0;
             }
             // The geometry is already drawable up to the existing 96-unit
             // limit. GI still needs its original, stricter 64-unit region.
             // 0.3.169: a region built ahead (lead) that the eye has not reached
             // yet waits for the next request instead of re-running this check.
             // Liveness: waiting needs the lead point within the 32-unit refresh and
             // the eye beyond 64, so a lead of at least 64-32. Its decay or reversal
             // (stop, turn back) moves the lead point by more than the reason-128 step,
             // which issues that next request.
             static_assert(64-NorthlightWorldStreaming::GeometryRefreshDistance>NorthlightWorldStreaming::GeometryLeadMoveStep,"lead waiting liveness");
             if(!NorthlightWorldStreaming::within(sceneCenter,request.camera,64)){
                 pending=NorthlightWorldStreaming::needsGeometry(sceneMap,sceneCenter,request.map,request.geometryCenter);continue;
             }
             if(request.id!=r.id){++retargeted;r=request;pending=false;}
             result->map=sceneMap;result->center=sceneCenter;result->requestId=r.id;result->requestedAt=r.queuedAt;
            }
            // END GEOMETRY HANDOFF
            if(!r.giEnabled)continue; /* GI=0: geometry for shadows only; no actor, probe or atlas work */
            result->requestActorSerial=r.actorSerial;
            DWORD actorStarted=GetTickCount();
            const auto actorWorkStart=std::chrono::steady_clock::now();
            bool actorsChanged=false;
            if(!r.menu&&!actorMemo.matches(r.actorJob)){
              NorthlightActorGeometry::Result resolvedActors;
              if(r.actorJob)resolvedActors=r.actorJob->resolve(actors?&actors->scene():nullptr,&actorTexelKeys);
              result->actorTexturesDecoded=resolvedActors.texturesDecoded;result->actorTextureEncodedBytes=resolvedActors.textureEncodedBytes;
              result->actorDecodeMs=resolvedActors.decodeMs;result->actorPoseMs=resolvedActors.geometryMs;result->actorHashMs=resolvedActors.hashMs;
              actorsChanged=resolvedActors.hash!=solvedActorHash;
              if(actorsChanged){
                const auto bvhStart=std::chrono::steady_clock::now();
                actors.reset();std::string actorError;
                if(resolvedActors.scene&&!resolvedActors.scene->triangles.empty()){
                    auto next=std::make_shared<NorthlightGI::BVH>();auto scene=std::move(*resolvedActors.scene);
                    if(next->build(std::move(scene),actorError,quality.giFastBVH!=0))actors=next; /* GIFastBVH=0: 0.3.137 tracer */
                    else logf("GI actor BVH rejected: %s",actorError.c_str());
                }
                actorTexelKeys=std::move(resolvedActors.texelKeys);
                solvedActorHash=resolvedActors.hash;
                // Pose/packet change only: keep corrections whose nearby actor
                // bounds are unchanged (0.3.35); the static generation is intact.
                dynamicProbes.observe(actors?&actors->scene():nullptr);
                result->actorBvhMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-bvhStart).count();
              }
              actorMemo.remember(r.actorJob);
            }
            result->actorMs=GetTickCount()-actorStarted;
            result->actorWorkMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-actorWorkStart).count();
            // An unchanged actor packet must not republish/upload an identical
            // 4096-entry probe atlas every capture interval. Only skip after a
            // complete solve for this actor generation has actually published.
            if(r.reason==64&&!actorsChanged&&completedActorHash==solvedActorHash&&completedBaseId==r.baseId&&r.baseId!=0&&probeCacheGeometry==bvh&&coarseProbes.completeFor(r.probeCenter))continue;
            r.light.points=sceneLights;r.light.movingGeometry=nullptr;
            // A probe belongs to a fixed world point and a geometry/lighting
            // generation. Camera motion alone must not recompute that point.
            const bool visibilityGeometryChanged=probeCacheGeometry!=bvh;
            if(visibilityGeometryChanged||!NorthlightGI::sameStaticLighting(probeCacheLight,r.light)){
                if(visibilityGeometryChanged){relightCache.reset();visibilityCache.reset();}
                probeCache.clear();probeCacheGeometry=bvh;probeCacheLight=r.light;
                preparedLight=NorthlightGI::prepareLighting(probeCacheLight);
                relightCache.configure(preparedLight,quality.giRays);
                publicationCadence.reset();++lightingGeneration;
                // Keep already displayed same-map GI until replacement probes
                // are ready. This is display fallback only, never solver input.
                displayFallback.clear();
                {std::lock_guard<std::mutex> lock(mutex);
                 if(published&&published->map==r.map)displayFallback=published->atlas;
                 else if(previousLighting&&previousLighting->map==r.map)displayFallback=previousLighting->atlas;}
                // Retain the complete displayed sample while streaming a new
                // same-map region. Removing only visibility turns its retained
                // radiance black until the worker reaches that point again.
                // This atlas never enters the new geometry's solve caches.
                dynamicProbes.reset(actors?&actors->scene():nullptr);
            }
            const auto solveStatic=[&](NorthlightGI::ProbeGridKey key,V p){
                auto value=relightCache.solve(*bvh,key,p,preparedLight,quality.giRays,NorthlightGI::probeSeed(key));
                if(value.valid)value.localVisibility=visibilityCache.get(*bvh,key,p);
                return value;
            };
            // Geometry was already handed off before actor work. A second
            // request-ID gate here discarded useful work on every camera move.
            result->fogField=sceneFog;result->bvh=bvh;result->meshPlan=scenePlan;result->prepared=scenePrepared;result->retirementBytes=bvh->retainedBytes()+scenePlan->cpuBytes()+sizeof(Snapshot);result->localLights=rawSceneLights;result->lightAttachments=sceneLightAttachments;result->origin=NorthlightGI::probeWindowOrigin(r.probeCenter);
            result->localLightIndex=sceneLightIndex;
            result->lightingGeneration=lightingGeneration;
            if(preparedLight.valid()){
                const auto& light=preparedLight.values();
                result->probeRadiance={light.sunDirection,light.sunIrradiance,light.skyRadiance,true};
            }
            result->coarseAtlas=coarseProbes.atlas(r.map);
            DWORD solveStart=GetTickCount();bool obsolete=false,replaced=false;
            const auto solveWorkStart=std::chrono::steady_clock::now();
            // Called with mutex held. A camera-only request may consume this
            // generation's completed world probes, but a new map, distant BVH
            // region or changed lighting must never accept this publication.
            uint64_t checkedRequestId=r.id;bool checkedCompatibility=true;
            auto compatiblePublication=[&](){
                if(!request.enabled||!request.giEnabled||probeCacheGeometry!=bvh)return false;
                if(checkedRequestId==request.id)return checkedCompatibility;
                checkedRequestId=request.id;
                auto latestLight=request.light;latestLight.points=sceneLights;latestLight.movingGeometry=nullptr;
                checkedCompatibility=NorthlightGI::staticFallbackCompatible(r.map,sceneMap,request.map,
                    r.camera,sceneCenter,request.camera,probeCacheLight,latestLight);
                return checkedCompatibility;
            };
            // World-keyed transport remains valid across a camera retarget.
            // A different actor packet still cancels its moving correction.
            auto compatibleTransport=[&](){return compatiblePublication()&&request.actorJob==r.actorJob;};
            auto publishProgress=[&](){
                if(fixedGi||!publicationCadence.due(GetTickCount(),result->processedProbes))return;
                {std::lock_guard<std::mutex> lock(mutex);if(stopping||!compatiblePublication())return;}
                // Every export owns its storage. Neither subsequent solves nor
                // cancellation can modify an atlas already seen by rendering.
                auto progress=std::make_shared<Snapshot>(*result);
                progress->atlas=probeCache.atlas();progress->dynamicReused=progress->dynamicSolved=0;
                if(!dynamicProbes.applyCached(progress->atlas,
                    [&](){std::lock_guard<std::mutex> lock(mutex);return stopping||!compatiblePublication();},
                    progress->dynamicReused))return;
                NorthlightGI::retainProbeDisplayFallback(progress->atlas,displayFallback);
                progress->solveMs=GetTickCount()-solveStart;progress->superseded=superseded;
                progress->solveWorkMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-solveWorkStart).count();
                progress->retargeted=retargeted;progress->partial=true;progress->staticOnly=progress->dynamicReused==0;
                progress->relightStats=relightCache.stats();
                progress->visibilityStats=visibilityCache.stats();
                std::lock_guard<std::mutex> lock(mutex);if(stopping||!compatiblePublication())return;
                publishProbesLocked(std::move(progress),serial);publicationCadence.published(GetTickCount());
            };
            auto noteSuperseded=[&](){
                std::lock_guard<std::mutex> lock(mutex);
                if(!stopping&&request.reason==2&&compatiblePublication())++retargeted;else ++superseded;
            };
            const auto order=NorthlightGI::probeSolveOrder(r.probeCenter);
            std::array<NorthlightGI::Probe,8> prefetched;unsigned prefetchedMask=0;
            for(unsigned cursor=0;cursor<order.size();++cursor){
                if(cursor%8==0){
                    // 0.3.153: a finished build also stops a pass on the previous generation.
                    {std::lock_guard<std::mutex> lock(mutex);if(stopping)return;obsolete=request.id!=r.id;replaced=!fixedGi&&builtGeometry!=nullptr;}
                    if(obsolete||replaced)break;
                    publishProgress();
                    // GIThreads>1: solve this group's uncached probes in parallel. A pure,
                    // per-key-seeded function, consumed below in the original order and cache sequence.
                    if(solvePool)prefetchedMask=NorthlightGI::prefetchProbeGroup(*solvePool,probeCache,result->origin,order,cursor,
                        solveStatic,prefetched);
                }
                V p=NorthlightGI::probeWindowPosition(result->origin,order[cursor]);
                NorthlightGI::ProbeGridKey key;
                if(!NorthlightGI::probeGridKey(p,key))throw std::runtime_error("Invalid world probe coordinate");
                bool valid=false;
                if(probeCache.touchValidity(key,valid))++result->reusedProbes;
                else{const auto probe=prefetchedMask&(1u<<(cursor%8))?prefetched[cursor%8]:solveStatic(key,p);probeCache.putMissing(key,probe);valid=probe.valid;++result->solvedProbes;publicationCadence.solved();}
                ++result->processedProbes;if(valid)++result->validProbes;
            }
            if(obsolete||replaced){
                if(obsolete)noteSuperseded();publishProgress();
                // The pending request selects a new nearest-first window and
                // current actor packet. Its static cache and publication clock
                // survive camera-only retargeting; no completed probe is lost.
                continue;
            }
            result->atlas=probeCache.atlas();movingStats={};
            // Preserve all static world slots across pose/culling changes. The
            // observed actor scene only supplies a local radiance correction.
            if(!dynamicProbes.apply(result->atlas,
                [&](NorthlightGI::ProbeGridKey key,V p,NorthlightGI::StaticPathRecord* paths){
                    if(paths&&actors)return NorthlightGI::solveProbeMoving(*bvh,p,preparedLight,quality.giRays,NorthlightGI::probeSeed(key),*actors,*paths,lightingGeneration,&movingStats);
                    return NorthlightGI::solveProbePrepared(*bvh,p,preparedLight,quality.giRays,NorthlightGI::probeSeed(key),actors.get());},
                [&](){std::lock_guard<std::mutex> lock(mutex);return stopping||!compatibleTransport();},
                result->dynamicReused,result->dynamicSolved)){
                noteSuperseded();result->atlas.clear();publishProgress();continue;
            }
            result->solveMs=GetTickCount()-solveStart;result->superseded=superseded;result->retargeted=retargeted;
            result->solveWorkMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-solveWorkStart).count();
            result->movingStats=movingStats;result->pathRecordBytes=dynamicProbes.pathBytes();
            result->relightStats=relightCache.stats();
            result->visibilityStats=visibilityCache.stats();
            // Completion and later camera/actor publications retain the same
            // display-only fallback as partial updates; solved invalid wins.
            NorthlightGI::retainProbeDisplayFallback(result->atlas,displayFallback);
            auto commitResult=[&] {
                publishProbesLocked(result,serial);
                completedActorHash=solvedActorHash;completedBaseId=r.baseId;concurrentSolves+=concurrent;
            };
            bool publishObsolete=false;
            {std::lock_guard<std::mutex> lock(mutex);if(stopping)return;
             if(!compatibleTransport())publishObsolete=true;
             else if(!fixedGi){commitResult();publicationCadence.published(GetTickCount());}}
            if(publishObsolete){noteSuperseded();result->atlas.clear();publishProgress();}
            else coarseProbes.run(r.probeCenter,sceneCenter,NorthlightQuality::giGeometryRadius(quality),*bvh,preparedLight,
                quality.giRays,lightingGeneration,r.map,
                [&](){std::lock_guard<std::mutex> lock(mutex);return stopping||request.id!=r.id||(!fixedGi&&builtGeometry!=nullptr);},
                [&](auto atlas){
                    if(fixedGi){result->coarseAtlas=std::move(atlas);return;}
                    auto progress=std::make_shared<Snapshot>(*result);progress->coarseAtlas=std::move(atlas);
                    std::lock_guard<std::mutex> lock(mutex);if(stopping||!compatiblePublication())return;
                    publishProbesLocked(std::move(progress),serial);
                },[&](){return fixedGi?DWORD(0):GetTickCount();});
            if(fixedGi&&!publishObsolete){
                std::lock_guard<std::mutex> lock(mutex);
                if(stopping)return;
                if(compatibleTransport()&&coarseProbes.completeFor(r.probeCenter))commitResult();
            }
        }
      }catch(const std::bad_alloc&){workerFaultCode.store(1);}
       catch(const std::exception&){workerFaultCode.store(2);}
       catch(...){workerFaultCode.store(3);}
      // The thread has stopped. Do not allocate a diagnostic snapshot in an
      // allocation-failure handler. The render thread reports workerFault().
      finishRequest(NorthlightFrameParityClock::fixedGiPublication());
    }
