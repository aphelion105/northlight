// The geometry thread builds the local BVH and pages before publishing a region.
            // BEGIN REGION BUILD: tests substitute a synthetic region for this block.
            {
                const auto memoryBefore=workerAdmissionProbe.stats();const double memoryMsBefore=workerMemoryMs;
                auto phaseStart=std::chrono::steady_clock::now();double previousProfileMs=memoryProfileMs;
                auto phaseElapsed=[&](){const auto now=std::chrono::steady_clock::now();const double ms=std::chrono::duration<double,std::milli>(now-phaseStart).count()-(memoryProfileMs-previousProfileMs);phaseStart=now;previousProfileMs=memoryProfileMs;return std::max(0.,ms);};
                // Probe with the lifted budget: a witness block below its contiguous floor falls back to a full walk.
                auto memory=workerMemory(entryLift.apply(NorthlightGeometryMemory::buildBudget(64*NorthlightGeometryMemory::MiB),GetTickCount()));
                if(!restore&&!generations.canAdmit()&&NorthlightStreaming::RetireOversizedWhenIdle){
                    // An old generation may be in the reaper queue instead of
                    // having been freed on the render thread (0.3.137). Wait
                    // for that destruction (bounded) rather than 100 ms steps.
                    const auto waitStart=std::chrono::steady_clock::now();
                    NorthlightStreaming::cpuRetirement().waitIdle(NorthlightStreaming::RetireWaitMs);++generationWaits;
                    const unsigned us=unsigned(std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-waitStart).count());
                    unsigned peak=generationWaitPeakUs.load();while(us>peak&&!generationWaitPeakUs.compare_exchange_weak(peak,us)){}
                }
                if(!restore&&!generations.canAdmit()){++generationDeferrals;{std::lock_guard<std::mutex> lock(mutex);generationStall.deferred(GetTickCount());}deferBuild("generation-deferred",memory,100);continue;}
                {std::lock_guard<std::mutex> lock(mutex);generationStall.admitted();}
                // Reclaim optional caches against the same budget used to admit the build,
                // including any temporary increase after a previous allocation refusal.
                const auto plainEntry=restore?NorthlightGeometryMemory::coldInBuildBudget():NorthlightGeometryMemory::buildBudget(64*NorthlightGeometryMemory::MiB);
                profileMemory("before-entry-reclaim");
                auto entryBudget=restore?plainEntry:entryLift.apply(plainEntry,GetTickCount());
                // BEGIN STAGED ENTRY RECLAIM: the offline MPQ episode test uses this exact block.
                if(!NorthlightGeometryMemory::admits(memory,entryBudget)){
                    const auto before=memory;const auto sourceBefore=sceneSources.liveBytes(),preparationBefore=meshPreparation.stats().bytes;
                    unsigned reclaimed=1;sceneSources.trimPayloads();memory=workerMemory(entryBudget);
                    if(!NorthlightGeometryMemory::admits(memory,entryBudget)){reclaimed=2;localGeometry.reset();memory=workerMemory(entryBudget);}
                    if(!NorthlightGeometryMemory::admits(memory,entryBudget)){reclaimed=3;resetPreparation();memory=workerMemory(entryBudget);}
                    if(NorthlightDiagnostics::enabled())logf("WORLD entry reclaim buildId=%llu stage=%u admitted=%u beforeAvailableMiB=%llu afterAvailableMiB=%llu beforeLargestLowerMiB=%llu afterLargestLowerMiB=%llu beforeValid=%u afterValid=%u sourceBeforeMiB=%.3f sourceAfterMiB=%.3f preparationBeforeMiB=%.3f preparationAfterMiB=%.3f requiredAvailableMiB=%llu requiredLargestMiB=%llu",
                        (unsigned long long)r.id,reclaimed,unsigned(NorthlightGeometryMemory::admits(memory,entryBudget)),
                        (unsigned long long)(before.available>>20),(unsigned long long)(memory.available>>20),(unsigned long long)(before.largest>>20),(unsigned long long)(memory.largest>>20),unsigned(before.valid),unsigned(memory.valid),
                        double(sourceBefore)/1048576,double(sceneSources.liveBytes())/1048576,double(preparationBefore)/1048576,double(meshPreparation.stats().bytes)/1048576,
                        (unsigned long long)(entryBudget.available>>20),(unsigned long long)(entryBudget.largest>>20));
                }
                // END STAGED ENTRY RECLAIM
                // BEGIN CHUNKED ENTRY SELECTION
                if(!NorthlightGeometryMemory::admits(memory,entryBudget)){
                    const auto chunkBudget=chunkLift.apply(NorthlightGeometryMemory::chunkBuildBudget(),GetTickCount());
                    memory=workerMemory(chunkBudget);entryBudget=chunkBudget;chunkedLocal=true;
                }
                if(!NorthlightGeometryMemory::admits(memory,entryBudget)){
                    // A bounded candidate also replaces a retained generation.
                    // Waiting for that generation to expire made lighting blink
                    // at every 160-unit boundary below the normal 480 MiB floor.
                    // A warm build peak does not describe this file-backed candidate.
                    // Only an earlier cold refusal may raise its entry budget.
                    const auto cold=coldLift.apply(NorthlightGeometryMemory::coldChunkBuildBudget(),GetTickCount());
                    memory=workerMemory(cold);entryBudget=cold;coldRecovery=true;
                }
                // END CHUNKED ENTRY SELECTION
                if(!NorthlightGeometryMemory::admits(memory,entryBudget)){stallBegin("build-deferred",memory,entryBudget.available,entryBudget.largest);deferBuild("build-deferred",memory,1000,&entryBudget);continue;}
                const auto geometryBudget=[&](uint64_t bytes=0){return (coldRecovery||spoolTerrain)?NorthlightGeometryMemory::coldInBuildBudget(bytes):NorthlightGeometryMemory::inBuildBudget(bytes);};
                const uint64_t entryAvailable=memory.available,entryLargest=memory.largest;
                geometryMemoryDemand.store(0,std::memory_order_relaxed); // admitted: the build's own use is not a demand (the episode stays open)
                // A memory refusal after entry: the next entry waits for what this attempt lacked.
                auto liftEntry=[&](const char* stage,uint64_t required,uint64_t observed,uint64_t requiredLargest){
                    entryLiftMap=r.map;entryLiftCenter=r.geometryCenter;
                    auto& lift=coldRecovery?coldLift:chunkedLocal?chunkLift:entryLift;lift.refused(entryAvailable,required,observed,GetTickCount(),requiredLargest);
                    logf("WORLD geometry entry raised stage=%s requiredAvailableMiB=%llu requiredLargestMiB=%llu entryMiB=%llu refusedMiB=%llu stageRequiredMiB=%llu holdMs=%u",
                        stage,(unsigned long long)(lift.available>>20),(unsigned long long)(lift.largest>>20),(unsigned long long)(entryAvailable>>20),
                        (unsigned long long)(observed>>20),(unsigned long long)(required>>20),unsigned(NorthlightGeometryMemory::LiftMs));
                };
                if(NorthlightDiagnostics::enabled())logGeometryMemory("before-load",memory,generations.live(),workerMemoryExact,&entryBudget);
                profileMemory("before-load");
                std::string error;
                const V center=r.geometryCenter;
                int tx=int(std::floor((17066.6666667-center.y)/533.3333333));
                int ty=int(std::floor((17066.6666667-center.x)/533.3333333));
                const int giChunkRadius=int((NorthlightQuality::giChunkSide(quality)-1)/2);
                const float giGeometryRadius=NorthlightQuality::giGeometryRadius(quality);
                std::vector<std::string> tiles;
                for(int y=ty-giChunkRadius;y<=ty+giChunkRadius;++y)for(int x=tx-giChunkRadius;x<=tx+giChunkRadius;++x){
                    char name[MAX_PATH*3];std::snprintf(name,sizeof name,"%sworld-cache/%s/%d_%d.fg3",root.c_str(),r.map.c_str(),x,y);
                    FILE* f=NorthlightFiles::open(name,"rb");if(f){std::fclose(f);tiles.emplace_back(name);}
                }
                bool localDeferred=false;NorthlightGeometryMemory::Sample localSample;uint64_t localRequired=0,localLargest=0;
                // Interior checks use inBuildBudget (stage entry already reserved the build headroom).
                NorthlightGI::AllocationAdmission localAdmission=[&](uint64_t bytes){
                    if(bytes<NorthlightGeometryMemory::MiB)return true;
                    const auto budget=geometryBudget(bytes);auto sample=workerMemory(budget);
                    if(NorthlightGeometryMemory::admits(sample,budget))return true;
                    localDeferred=true;localSample=sample;localRequired=budget.available;localLargest=budget.largest;logGeometryMemory("local-geometry-allocation-deferred",sample,generations.live(),workerMemoryExact,&budget);return false;
                };
                auto replacement=restore?restore->bvh:std::make_shared<NorthlightGI::BVH>();
                if(!restore&&!generations.track(replacement))throw std::runtime_error("Geometry generation admission invariant");
                const auto sourceBefore=sceneSources.stats();const auto preparationBefore=meshPreparation.stats();
                std::shared_ptr<const NorthlightGI::BVH> texelReuse;
                {std::lock_guard<std::mutex> lock(mutex);if(published&&published->map==r.map)texelReuse=published->bvh;}
                auto buildLocal=[&]{return !tiles.empty()&&(coldRecovery?pressureGeometry:localGeometry).build(tiles,root+"world-cache/models",r.map,center-V(giGeometryRadius,giGeometryRadius,320),center+V(giGeometryRadius,giGeometryRadius,320),*replacement,error,localAdmission,quality.giFastBVH!=0,coldRecovery?NorthlightGI::SceneSourceProvider{}:sceneSource,coldRecovery?NorthlightGI::SceneSourceRevision{}:sceneRevision,chunkedLocal,texelReuse.get());};
                bool localBuilt=restore||buildLocal();
                auto noteLocalAllocationFailure=[&]{if(!localBuilt&&!localDeferred&&(error.find("Geometry allocation deferred")!=std::string::npos||error.find("Cannot allocate")!=std::string::npos)){const auto budget=geometryBudget();localSample=workerMemory(budget);localRequired=budget.available;localLargest=budget.largest;localDeferred=true;}};
                noteLocalAllocationFailure();
                if(!localBuilt&&localDeferred&&!chunkedLocal){
                    resetPreparation();chunkedLocal=true;localDeferred=false;const auto budget=chunkLift.apply(NorthlightGeometryMemory::chunkBuildBudget(),GetTickCount());
                    const auto retryMemory=workerMemory(budget);if(NorthlightGeometryMemory::admits(retryMemory,budget))localBuilt=buildLocal();
                    else{localDeferred=true;localSample=retryMemory;localRequired=budget.available;localLargest=budget.largest;}
                }
                noteLocalAllocationFailure();
                if(!localBuilt&&localDeferred&&!coldRecovery&&currentGeometry()){
                    resetPreparation();
                    const auto budget=coldLift.apply(NorthlightGeometryMemory::coldChunkBuildBudget(),GetTickCount());
                    if(NorthlightGeometryMemory::admits(workerMemory(budget),budget)){
                        coldRecovery=chunkedLocal=true;localDeferred=false;
                        localBuilt=buildLocal();noteLocalAllocationFailure();
                    }
                }
                if(localBuilt)localDeferred=false;
                if(!localBuilt){
                    replacement.reset();
                    if(localDeferred){resetPreparation();liftEntry("local-geometry",localRequired,localSample.available,localLargest);stallBegin("local-geometry",localSample,localRequired,localLargest);deferBuild("local-geometry-retry",workerMemory(),1000);continue;}
                    result->message=error.empty()?"No cached geometry for "+r.map:error;publishError();continue;
                }
                if(coldRecovery&&!restore)replacement->shareMaterialTexels();
                if(restore)logf("WORLD reset rebuild reused local BVH triangles=%zu cpuMiB=%.2f",replacement->triangleCount(),double(replacement->retainedBytes())/1048576);
                const auto localStats=(coldRecovery?pressureGeometry:localGeometry).stats();
                if(NorthlightDiagnostics::enabled())logf("WORLD local incremental: reads=%llu built=%llu reused=%llu reusedTriangles=%llu builtTriangles=%llu retainedMiB=%.2f flatMiB=%.2f bvhMiB=%.2f loadMs=%.3f pieceMs=%.3f assembleMs=%.3f bvhMs=%.3f totalMs=%.3f",
                    (unsigned long long)localStats.modelReads,(unsigned long long)localStats.pieceBuilds,(unsigned long long)localStats.pieceReuses,
                    (unsigned long long)localStats.reusedTriangles,(unsigned long long)localStats.builtTriangles,double(localStats.retainedBytes)/1048576,
                    double(localStats.flatBytes)/1048576,double(localStats.bvhBytes)/1048576,localStats.loadMs,localStats.pieceMs,localStats.assembleMs,localStats.bvhMs,localStats.totalMs);
                if(!currentGeometry()){++superseded;abandonGeometry();continue;}
                // The local BVH and its cache are this build's own memory: entry reserved their headroom,
                // so this gate keeps the in-build floor. The entry floor here refused every attempt that
                // entered within one BVH of the configured entry floor and rebuilt it each second (0.3.212 player log).
                spoolTerrain=coldRecovery;
                // A scratch allocation failure belongs to the shadow plan. Keep the
                // completed local BVH and retry only the remaining stages on disk.
                auto spillShadow=[&]{
                    if(spoolTerrain)return false;
                    spoolTerrain=true;resetPreparation();
                    if(NorthlightDiagnostics::enabled())logf("WORLD shadow build switched to temporary storage buildId=%llu retainedLocalTriangles=%zu",(unsigned long long)r.id,replacement->triangleCount());
                    return true;
                };
                auto planBudget=geometryBudget();
                memory=workerMemory(planBudget);if(NorthlightDiagnostics::enabled())logGeometryMemory("after-bvh",memory,generations.live(),workerMemoryExact,&planBudget);
                profileMemory("after-bvh");
                if(!NorthlightGeometryMemory::admits(memory,planBudget)&&spillShadow()){
                    planBudget=geometryBudget();memory=workerMemory(planBudget);
                }
                if(!NorthlightGeometryMemory::admits(memory,planBudget)){
                    replacement.reset();liftEntry("plan",planBudget.available,memory.available,planBudget.largest);stallBegin("plan",memory,planBudget.available,planBudget.largest);deferBuild("plan-deferred",memory,1000,&planBudget);continue;
                }
                // Reduced-reach retry re-enters here with the local BVH (`replacement`, still tracked in
                // `generations`) kept: it does not depend on the terrain stages.
                float reach=0,flightReach=0;
                const double localPhaseMs=phaseElapsed();
                // The point crop and cache revision are fixed for this build request.
                bool pointBudgetExceeded=false;
                terrainStage:
                auto plan=std::make_shared<NorthlightWorldMesh::WorldMeshUploadPlan>();
                // GI stays local. Load only authored terrain over the complete
                // directional caster volume into a separate GPU shadow source.
                bool terrainDeferred=false;NorthlightGeometryMemory::Sample terrainSample;uint64_t terrainLargest=0,terrainRequired=0,terrainRequiredLargest=0;unsigned terrainChecks=0;
                NorthlightGI::AllocationAdmission terrainAdmission=[&](uint64_t bytes){
                    terrainLargest=std::max(terrainLargest,bytes);
                    // Small metadata cannot fragment a large block. Recheck the applicable
                    // in-build budget before each large allocation.
                    if(bytes<NorthlightGeometryMemory::MiB)return true;
                    ++terrainChecks;
                    const auto budget=geometryBudget(bytes);auto sample=workerMemory(budget);
                    if(!NorthlightGeometryMemory::admits(sample,budget)){
                        // Published BVHs retain their shared pieces. Drop the
                        // optional owners, then measure what actually became free.
                        // Admission can run inside meshPreparation itself;
                        // never reset that memo reentrantly while it is borrowed.
                        sceneSources.reset();localGeometry.reset();sample=workerMemory(budget);
                        if(NorthlightDiagnostics::enabled())logf("WORLD geometry optional source/local owners dropped availableMiB=%llu stage=shadow-plan",
                             (unsigned long long)(sample.available>>20));
                    }
                    if(NorthlightGeometryMemory::admits(sample,budget))return true;
                    terrainDeferred=true;terrainSample=sample;terrainRequired=budget.available;terrainRequiredLargest=budget.largest;logGeometryMemory("shadow-terrain-allocation-deferred",sample,generations.live(),workerMemoryExact,&budget);
                    logf("WORLD terrain allocation requestMiB=%.2f requiredContiguousMiB=%.2f",double(bytes)/1048576,double(bytes+NorthlightGeometryMemory::ContiguousMargin)/1048576);
                    return false;
                };
#if NORTHLIGHT_DEV_TUNING
                const uint32_t reachZone=NorthlightRegionalFog::zoneAt(paletteRegion->region,center.x,center.y);
                const float profileReach=NorthlightZoneTuning::terrainRadius(*tuning,shadowRanges.at(r.map,reachZone),r.map,reachZone);
#else
                const float profileReach=shadowRanges.at(r.map,NorthlightRegionalFog::zoneAt(paletteRegion->region,center.x,center.y));
#endif
                const auto reachChoice=NorthlightTerrainReach::choose(reachState,std::max(profileReach,flightReach),GetTickCount(),entryAvailable,entryLargest);
                reach=reachChoice.reach;
#if NORTHLIGHT_DEV_TUNING
                devTerrainReach.store(reach);
#endif
                if(reachChoice.tierChanged)logf("WORLD shadow terrain reach tier from=%.0f to=%.0f reason=headroom availableMiB=%llu largestMiB=%llu",reachChoice.tierFrom,reach,(unsigned long long)(entryAvailable>>20),(unsigned long long)(entryLargest>>20));
                if(reachChoice.restored)logf("WORLD shadow terrain reach restored to=%.0f",reach);
                // Memory refusal of an extended build: rebuild the terrain stages at the base reach now.
                // Interior allocations use the applicable working guard, so the extended
                // build falls back only when an interior allocation is refused under that floor. terrainAdmission
                // re-samples on every call and the refused attempt's allocations are freed (goto destroys them) first.
                auto retryShadowStage=[&]{
                    // Restore only once an entry sample covers what this extended attempt lacked.
                    uint64_t restore=0;
                    if(!NorthlightGeometryMemory::add(entryAvailable,terrainRequired>terrainSample.available?terrainRequired-terrainSample.available:0,restore))restore=UINT64_MAX;
                    const bool reduced=NorthlightTerrainReach::memoryRefused(reachState,reach,GetTickCount(),restore,terrainRequiredLargest);
                    if(reduced){
                        memoryStall.markReduced();
                        logf("WORLD shadow terrain reach reduced from=%.0f to=%.0f reason=memory",reach,NorthlightTerrainReach::BaseReach);
                    }
                    return spillShadow()||reduced;
                };
                const bool extended=reach>NorthlightShadowTerrain::Radius;
                const int tileReach=int(std::ceil(reach/NorthlightRegionalFog::TileSize));
                std::function<bool(V,V)> terrainFilter,terrainChunkFilter;
                if(extended)terrainFilter=[center,flightReach](V lo,V hi){return NorthlightRegionalShadow::selected(center,lo,hi,std::max(NorthlightRegionalShadow::DefaultRadius,flightReach));};
                if(extended)terrainChunkFilter=[center,flightReach](V lo,V hi){return NorthlightRegionalShadow::selectedChunk(center,lo,hi,std::max(NorthlightRegionalShadow::DefaultRadius,flightReach));};
                std::vector<std::string> shadowTiles;
                for(int y=ty-tileReach;y<=ty+tileReach;++y)for(int x=tx-tileReach;x<=tx+tileReach;++x){
                    const double zero=NorthlightRegionalFog::WorldZero,size=NorthlightRegionalFog::TileSize;
                    if(terrainFilter&&!terrainFilter(V(float(zero-(y+1)*size),float(zero-(x+1)*size),-100000),V(float(zero-y*size),float(zero-x*size),100000)))continue;
                    char name[MAX_PATH*3];std::snprintf(name,sizeof name,"%sworld-cache/%s/%d_%d.fg3",root.c_str(),r.map.c_str(),x,y);
                    FILE* f=NorthlightFiles::open(name,"rb");if(f){std::fclose(f);shadowTiles.emplace_back(name);}
                }
                NorthlightGI::WorldScene shadowTerrain;
                { uint32_t reachBits;std::memcpy(&reachBits,&reach,sizeof reachBits);terrainSizeHint.key=reachBits; } // same map (reset above) and same reach only
                const bool terrainLoaded=NorthlightGI::loadInstancedScenes(shadowTiles,root+"world-cache/models",V(center.x-reach,center.y-reach,-100000),V(center.x+reach,center.y+reach,center.z+reach),shadowTerrain,error,1,terrainAdmission,terrainFilter,terrainChunkFilter,&terrainSizeHint,spoolTerrain?NorthlightGI::SceneSourceProvider{}:sceneSource,true,chunkedLocal||spoolTerrain,spoolTerrain);
                if(terrainLoaded)terrainDeferred=false; // Optional reserve/provenance refusals preserve all accepted geometry.
                const float shadowGround=terrainLoaded?NorthlightFlightShadow::ground(shadowTerrain,center):std::numeric_limits<float>::quiet_NaN();
                if(!flightReach&&std::isfinite(shadowGround)&&center.z-shadowGround>128){
                    const auto coverage=NorthlightFlightShadow::coverage(center,shadowGround,4096);
                    flightReach=NorthlightFlightShadow::terrainReach(coverage.radius);
                    if(flightReach>NorthlightRegionalShadow::DefaultRadius)goto terrainStage;
                }
                NorthlightGI::WorldScene pointTransmission;
                if(terrainLoaded&&!pointBudgetExceeded){
                    std::vector<std::string> pointTiles;
                    for(int py=ty-1;py<=ty+1;++py)for(int px=tx-1;px<=tx+1;++px){
                        char file[MAX_PATH*3];std::snprintf(file,sizeof file,"%sworld-cache/lights/point-transmission/%s/%d_%d.fg3",root.c_str(),r.map.c_str(),px,py);
                        FILE* available=NorthlightFiles::open(file,"rb");if(available){std::fclose(available);pointTiles.emplace_back(file);}
                    }
                    NorthlightGI::PointTransmissionBudget pointBudget;
                    // Optional casters must never evict the base scene or mark its terrain as deferred.
                    NorthlightGI::AllocationAdmission pointAdmission=[&](uint64_t bytes){
                        if(bytes<NorthlightGeometryMemory::MiB)return true;
                        const auto budget=geometryBudget(bytes);return NorthlightGeometryMemory::admits(workerMemory(budget),budget);
                    };
                    const bool pointLoaded=pointTiles.empty()||NorthlightGI::loadPointTransmission(pointTiles,root+"world-cache/lights/point-transmission/models",
                        center-V(NorthlightShadowTerrain::LocalHorizontal,NorthlightShadowTerrain::LocalHorizontal,NorthlightShadowTerrain::LocalVertical),
                        center+V(NorthlightShadowTerrain::LocalHorizontal,NorthlightShadowTerrain::LocalHorizontal,NorthlightShadowTerrain::LocalVertical),pointTransmission,error,pointAdmission,pointBudget);
                    pointBudgetExceeded=!pointLoaded&&!pointBudget.systemRefused&&pointBudget.lastQuotaRefusal;
                    if(NorthlightDiagnostics::enabled())logf("POINT_TRANSMISSION worker tiles=%zu triangles=%zu retainedKiB=%zu peakChargedKiB=%llu refusedBytes=%llu systemRefused=%u loaded=%u",
                        pointTiles.size(),pointTransmission.triangles.size(),pointTransmission.retainedBytes()/1024,(unsigned long long)(pointBudget.peak/1024),(unsigned long long)pointBudget.refusedBytes,unsigned(pointBudget.systemRefused),unsigned(pointLoaded));
                    // Optional transmission cannot discard an otherwise valid GI/shadow generation.
                    if(!pointLoaded){pointTransmission=NorthlightGI::WorldScene{};if(NorthlightDiagnostics::enabled())logf("POINT_TRANSMISSION unavailable: %s",error.c_str());error.clear();}
                }
                const double terrainLoadMs=phaseElapsed();
                profileMemory("after-terrain-load",plan.get(),&shadowTerrain);
                // In the bounded path, release scratch terrain as its last triangle is read.
                // Point transmission retains the existing retryable path.
                const bool consumeTerrain=(chunkedLocal||spoolTerrain)&&pointTransmission.triangles.empty();
                NorthlightWorldMesh::PlanOptions shadowOptions;shadowOptions.alphaOnly=true;
                bool shadowPlanBuilt=terrainLoaded&&(consumeTerrain
                    ?NorthlightShadowTerrain::buildConsuming(replacement->scene(),shadowTerrain,center,*plan,error,terrainAdmission,reach,shadowOptions,replacement.get(),spoolTerrain,giGeometryRadius)
                    :NorthlightShadowTerrain::build(replacement->scene(),shadowTerrain,center,*plan,error,terrainAdmission,reach,shadowOptions,true,replacement.get(),&meshPreparation,&pointTransmission,spoolTerrain,giGeometryRadius));
                if(!shadowPlanBuilt&&!pointTransmission.triangles.empty()){
                    if(NorthlightDiagnostics::enabled())logf("POINT_TRANSMISSION assembly refused; retaining base geometry: %s",error.c_str());
                    pointTransmission=NorthlightGI::WorldScene{};terrainDeferred=false;error.clear();
                    shadowPlanBuilt=NorthlightShadowTerrain::build(replacement->scene(),shadowTerrain,center,*plan,error,terrainAdmission,reach,shadowOptions,true,replacement.get(),&meshPreparation,nullptr,spoolTerrain,giGeometryRadius);
                }
                if(shadowPlanBuilt){terrainDeferred=false; // an oversized reservation may have been refused before a successful fallback
                    if(plan->pagingChunks&&NorthlightDiagnostics::enabled())logf("WORLD shadow geometry prepared in bounded chunks");
                }else if(!terrainDeferred&&error=="Geometry allocation deferred"){
                    const auto budget=geometryBudget();terrainSample=workerMemory(budget);
                    terrainDeferred=true;terrainRequired=budget.available;terrainRequiredLargest=budget.largest;
                }
                if(!terrainLoaded||!shadowPlanBuilt){
                    if(terrainDeferred){
                        meshPreparation.reset();
                        stallBegin("shadow-terrain",terrainSample,terrainRequired,terrainRequiredLargest);
                        if(retryShadowStage())goto terrainStage;
                        shadowTerrain=NorthlightGI::WorldScene{};plan.reset();replacement.reset();liftEntry("shadow-terrain",terrainRequired,terrainSample.available,terrainRequiredLargest);deferBuild("shadow-terrain-retry",workerMemory(),1000);continue;}
                    if(error.rfind("Temporary geometry",0)==0||error=="Paged mesh temporary storage failed"){
                        shadowTerrain=NorthlightGI::WorldScene{};plan.reset();replacement.reset();
                        deferBuild("temporary-storage-retry",workerMemory(),1000);continue;
                    }
                    result->message=error;publishError();continue;
                }
                plan->shadowGround=shadowGround;plan->shadowTerrainReach=reach;
                const double shadowPlanMs=phaseElapsed();
                profileMemory("after-shadow-plan",plan.get(),&shadowTerrain);
                if(NorthlightDiagnostics::enabled())logf("WORLD terrain allocation largestMiB=%.2f checks=%u hinted=%u",double(terrainLargest)/1048576,terrainChecks,terrainSizeHint.hinted?1u:0u);
                if(NorthlightDiagnostics::enabled())logf("WORLD shadow terrain: localTriangles=%zu shadowTriangles=%u fixedChunks=%zu reach=%.0f tiles=%zu",replacement->triangleCount(),plan->triangleCount,plan->fixedTerrainChunks.size(),reach,shadowTiles.size());
                if(!currentGeometry()){++superseded;abandonGeometry();continue;}
                shadowTerrain=NorthlightGI::WorldScene{};pointTransmission=NorthlightGI::WorldScene{}; // merged shadow plan already owns its data
                auto pages=std::make_shared<NorthlightWorldMeshPages::Plan>();
                // 0.3.216 (issue #66): pages the GPU already holds (same map, keys published at the last commit) are freed inside
                // build() the moment they are finished, so the worker's peak holds only the pages that must be uploaded.
                std::shared_ptr<const NorthlightMeshPageKey::KeySet> resident;
                {std::lock_guard<std::mutex> lock(mutex);if(residentPages&&residentMap==r.map)resident=residentPages;}
                // CPU construction can fit while a second managed GPU copy
                // cannot. Select bounded DEFAULT pages before publishing an
                // upload plan that would otherwise retry forever. DEFAULT geometry
                // and cutout textures have bounded upload staging; normal staging
                // stays entirely in RAM until the atomic GPU commit.
                // With no resident pages the entire DEFAULT mesh would disappear at
                // Reset. The complete file-backed plan can replay immediately and
                // releases its construction memory at commit. A reset rebuild also
                // needs a self-contained plan, independent of pre-Reset keys.
                bool spoolUpload=spoolTerrain||!resident||resident->empty();
                if(!spoolUpload){
                    uint64_t largestTexture=0;for(const auto& material:plan->materials)largestTexture=std::max(largestTexture,material.bgraBytes());
                    auto uploadBudget=NorthlightGeometryMemory::uploadBudget(0,0,0,0);
                    uploadBudget.largest=2*std::max<uint64_t>(NorthlightWorldMeshPages::PageVertexByteLimit,largestTexture)+NorthlightGeometryMemory::ChunkMargin;
                    const auto uploadMemory=workerMemory(uploadBudget);
                    spoolUpload=!NorthlightGeometryMemory::admits(uploadMemory,uploadBudget);
                    if(spoolUpload)logf("WORLD upload staging uses temporary storage availableMiB=%llu requiredMiB=%llu uniqueTextureMiB=%.3f",
                        (unsigned long long)(uploadMemory.available>>20),(unsigned long long)(uploadBudget.available>>20),double(plan->textureBytes)/1048576);
                }
                NorthlightWorldMeshPages::Options pagingOptions;pagingOptions.resident=spoolUpload?nullptr:resident.get();pagingOptions.preparation=spoolUpload?nullptr:&meshPreparation;pagingOptions.spooled=spoolUpload;pagingOptions.deviceLocal=true;
                if(!NorthlightWorldMeshPages::build(*plan,*pages,error,terrainAdmission,pagingOptions)){
                    if(!terrainDeferred&&error=="Paged mesh worker allocation failed"){
                        const auto budget=geometryBudget();terrainSample=workerMemory(budget);
                        terrainDeferred=true;terrainRequired=budget.available;terrainRequiredLargest=budget.largest;
                    }
                    if(terrainDeferred){
                        meshPreparation.reset();
                        stallBegin("mesh-page",terrainSample,terrainRequired,terrainRequiredLargest);
                        if(retryShadowStage())goto terrainStage;
                        pages.reset();plan.reset();replacement.reset();liftEntry("mesh-page",terrainRequired,terrainSample.available,terrainRequiredLargest);deferBuild("mesh-page-retry",workerMemory(),1000);continue;}
                    if(error.rfind("Temporary geometry",0)==0||error=="Paged mesh temporary storage failed"){
                        shadowTerrain=NorthlightGI::WorldScene{};plan.reset();replacement.reset();
                        deferBuild("temporary-storage-retry",workerMemory(),1000);continue;
                    }
                    result->message=error;publishError();continue;
                }
                NorthlightWorldMeshPages::ElisionStats elided;
                profileMemory("after-pages",plan.get(),nullptr,pages.get());
                for(const auto& page:pages->pages)if(page.elided){++elided.pages;elided.bytes+=uint64_t(page.key.vertexBytes)+page.key.indexBytes;}
                if(!NorthlightWorldMeshPages::seal(*plan,pages,error,terrainAdmission)){
                    if(error=="paged ownership temporary storage failed"){
                        pages.reset();plan.reset();replacement.reset();resetPreparation();
                        deferBuild("temporary-storage-retry",workerMemory(),1000);continue;
                    }
                    if(!terrainDeferred&&error=="paged ownership allocation failed"){
                        const auto budget=geometryBudget();terrainSample=workerMemory(budget);
                        terrainDeferred=true;terrainRequired=budget.available;terrainRequiredLargest=budget.largest;
                    }
                    if(terrainDeferred){
                        meshPreparation.reset();
                        stallBegin("mesh-page-seal",terrainSample,terrainRequired,terrainRequiredLargest);
                        if(retryShadowStage())goto terrainStage;
                        pages.reset();plan.reset();replacement.reset();liftEntry("mesh-page-seal",terrainRequired,terrainSample.available,terrainRequiredLargest);deferBuild("mesh-page-seal-retry",workerMemory(),1000);continue;}
                    result->message=error;publishError();continue;
                }
                if(NorthlightDiagnostics::enabled())logf("WORLD mesh pages=%zu batches=%zu originalBatches=%zu vertexMiB=%.2f indexMiB=%.2f maxResourceKiB=512 elidedPages=%zu elidedMiB=%.2f",pages->pages.size(),pages->batches.size(),plan->batches.size(),double(pages->vertexBytes)/1048576,double(pages->indexBytes)/1048576,elided.pages,double(elided.bytes)/1048576);
                memory=workerMemory();if(NorthlightDiagnostics::enabled())logGeometryMemory("after-plan",memory,generations.live(),workerMemoryExact);
                profileMemory("after-seal",plan.get());
                const double pagesMs=phaseElapsed();
                // Worker-side owner map for the render-thread commit. Failure
                // only falls back to the 0.3.136 render-thread construction.
                std::shared_ptr<const PreparedCommit> prepared;
                if(PreparedCommitData){try {auto next=std::make_shared<PreparedCommit>();next->plan=plan.get();next->owners=StaticShadow::makeOwners(ownerPlacements(*plan));
                    next->fixed=std::make_shared<const FixedChunks>(plan->fixedTerrainChunks);prepared=std::move(next);}catch(...){prepared.reset();}}
                built->plan=plan;built->prepared=std::move(prepared);built->bvh=replacement;built->map=r.map;built->center=center;
                float eye[]={center.x,center.y,center.z};
                built->lightAttachments=lightAttachmentCache.load(r.map);
                if(lightCache.loadLights(r.map,eye,520,built->rawLights))for(auto& light:built->rawLights)if(!(light.flags&NorthlightLocalLights::RequiresNative))
                    built->lights.push_back({vec(light.position),vec(light.diffuse)*3.14159265f,light.attenuationStart,light.attenuationEnd});
                if(NorthlightDiagnostics::enabled())logf("GI local lights map=\"%s\" count=%zu (indirect only; authored direct light retained)",r.map.c_str(),built->lights.size());
                built->fog=fogRegion&&fogRegion->map==r.map?fogRegion->field:nullptr;
                const double environmentMs=phaseElapsed();const auto& memoryAfter=workerAdmissionProbe.stats();
                if(NorthlightDiagnostics::enabled()){
                    const auto& sources=sceneSources.stats();const auto& preparation=meshPreparation.stats();
                    logf("WORLD shared scene buildId=%llu sourceHits=%llu sourceReads=%llu sourceIdentityHits=%llu sourceLoadMs=%.3f sourceIdentityMs=%.3f sourceLiveMiB=%.2f sourceEntries=%zu preparationHits=%llu preparationMisses=%llu preparationReusedTriangles=%llu preparationBuiltTriangles=%llu preparationMiB=%.2f pageMemoHits=%llu reusedMemoPages=%llu reusedMemoMiB=%.2f sourceDecodedLiveMiB=%.2f sourceUncachedLeaseMiB=%.2f optionalLimitMiB=32",
                         (unsigned long long)r.id,(unsigned long long)(sources.hits-sourceBefore.hits),(unsigned long long)(sources.reads-sourceBefore.reads),
                         (unsigned long long)(sources.identityHits-sourceBefore.identityHits),sources.loadMs-sourceBefore.loadMs,sources.identityMs-sourceBefore.identityMs,
                         double(sceneSources.liveBytes())/1048576,sceneSources.entries(),(unsigned long long)(preparation.hits-preparationBefore.hits),
                         (unsigned long long)(preparation.misses-preparationBefore.misses),(unsigned long long)(preparation.reusedTriangles-preparationBefore.reusedTriangles),
                         (unsigned long long)(preparation.preparedTriangles-preparationBefore.preparedTriangles),double(preparation.bytes)/1048576,
                         (unsigned long long)(preparation.pageHits-preparationBefore.pageHits),(unsigned long long)(preparation.reusedPages-preparationBefore.reusedPages),
                         double(preparation.reusedPageBytes-preparationBefore.reusedPageBytes)/1048576,double(sceneSources.decodedLiveBytes())/1048576,double(sceneSources.uncachedLeaseBytes())/1048576);
                    logf("WORLD shared acceleration buildId=%llu blasBuilt=%llu blasReused=%llu builtTriangles=%llu reusedTriangles=%llu tlasPieces=%llu legacyFallbacks=%llu sharedLiveMiB=%.2f compatibilityMiB=%.2f tlasKiB=%.2f fallbackTriangles=%llu mandatoryTreeMiB=%.2f",
                         (unsigned long long)r.id,(unsigned long long)localStats.blasBuilds,(unsigned long long)localStats.blasReuses,
                         (unsigned long long)localStats.blasTrianglesBuilt,(unsigned long long)localStats.blasTrianglesReused,(unsigned long long)localStats.tlasPieces,
                         (unsigned long long)localStats.legacyFallbacks,double(localStats.sharedLiveBytes)/1048576,double(localStats.compatibilityBytes)/1048576,double(localStats.tlasBytes)/1024,
                         (unsigned long long)localStats.fallbackTriangles,double(localStats.mandatoryTreeBytes)/1048576);
                    logf("WORLD shared terrain chunks buildId=%llu built=%llu reused=%llu builtTriangles=%llu reusedTriangles=%llu",(unsigned long long)r.id,
                         (unsigned long long)localStats.chunkBuilds,(unsigned long long)localStats.chunkReuses,(unsigned long long)localStats.chunkTrianglesBuilt,(unsigned long long)localStats.chunkTrianglesReused);
                }
                if(NorthlightDiagnostics::enabled())logf("WORLD geometry phases buildId=%llu localMs=%.3f terrainLoadMs=%.3f shadowPlanMs=%.3f pagesMs=%.3f environmentMs=%.3f memoryMs=%.3f memoryRequests=%llu witnessHits=%llu fullScans=%llu regionQueries=%llu memoryTimeIncluded=1",
                    (unsigned long long)r.id,localPhaseMs,terrainLoadMs,shadowPlanMs,pagesMs,environmentMs,workerMemoryMs-memoryMsBefore,
                    (unsigned long long)(memoryAfter.requests-memoryBefore.requests),(unsigned long long)(memoryAfter.witnessHits-memoryBefore.witnessHits),
                    (unsigned long long)(memoryAfter.fullScans-memoryBefore.fullScans),(unsigned long long)(memoryAfter.regionQueries-memoryBefore.regionQueries));
                if(profileThisBuild)logf("WORLD MEMORY diagnostic buildId=%llu cpuMs=%.3f phaseTimesExcludeDiagnostics=1 maxObservedCapacityMiB=%.3f",(unsigned long long)r.id,memoryProfileMs,double(maximumObservedCapacity)/1048576);
            }
