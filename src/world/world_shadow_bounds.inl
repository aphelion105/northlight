// Replay bounds shared by directional, point, and flash shadow passes.
    // Directional shadows start after selection; point/flash-only refreshes
    // request the same job on demand. The first bounds reader joins it.
    // Pending or unavailable bounds draw, never cull.
    NorthlightReplayBoundsJob::Worker replayBoundsWorker;
    bool replayBoundsResolved=false;unsigned replayBoundsRuns=0;
    bool replayBoundsDiagnostics=false;double replayBoundsKickMs=0;
    unsigned replayBoundsLogFrame=0;bool replayBoundsLogSeen=false;
    bool replayBoundsDiagnosticSample(){
        // frames counts successful world composites and can stay at zero (or
        // a multiple of 120) while geometry is absent. Sample that value once.
        if(!NorthlightDiagnostics::enabled()||(frames&&frames%120)||(replayBoundsLogSeen&&replayBoundsLogFrame==frames))return false;
        replayBoundsLogSeen=true;replayBoundsLogFrame=frames;return true;
    }
    void replayBoundsBegin(){
        pointReplayOrderReady=false;
        pointReplayCasters.invalidate();
        replayBoundsAbandon();replayBoundsRuns=0;pointBoundsValid=pointMaterialBoundsUsed=0;pointBoundVertices=pointBoundOperations=0;
    }
    bool replayBoundsManyPointShadows()const{
        // The previous selection is only a budget hint on the same map.
        if(!effects.shadows||!quality.pointShadows||!quality.localLights||NorthlightQuality::localShadowLimit(quality)<4||
           !active||pointSelectionMap!=active->map)return false;
        return std::count_if(pointSelection.entries.begin(),pointSelection.entries.end(),[](const auto& entry){return entry.occupied;})>=4;
    }
    static bool replayCrowdBoundsOperations(){
#ifdef NORTHLIGHT_REPLAY_STAND
        static const bool enabled=[] {
            const char* value=std::getenv("NORTHLIGHT_RETRACE_CROWD_BOUNDS_OPERATIONS");
            return !(value&&value[0]=='0'&&value[1]=='\0');
        }();
        return enabled;
#else
        return true;
#endif
    }
    static bool fixedReplayBoundsWork(){
        return NorthlightFrameParityClock::fixedShadowBounds()||NorthlightFrameParityClock::fixedProfileWork();
    }
    // A metadata fallback (per-frame preparation cap) keeps the synchronous
    // pass' full-key evaluation as an unshared snapshot of the same program,
    // at most once per pair and FallbackPreparations pairs per frame. Beyond
    // that a pair is Unsupported this frame: drawn, never culled.
    static constexpr size_t FallbackPreparations=16;
    struct BoundsFallback {IDirect3DVertexShader9* shader;IDirect3DVertexDeclaration9* decl;std::shared_ptr<const NorthlightReplayBounds::Prepared> prepared;};
    std::vector<BoundsFallback> replayBoundsFallbacks;size_t replayBoundsCapped=0;
    std::shared_ptr<const NorthlightReplayBounds::Prepared> replayBoundsPrepared(IDirect3DVertexShader9* shader,IDirect3DVertexDeclaration9* decl){
        bool built=false;
        const auto build=[&]()->std::shared_ptr<const NorthlightReplayBounds::Prepared>{
            built=true;auto program=actorPrograms.find(shader);
            if(program==actorPrograms.end())return {};
            const D3DVERTEXELEMENT9* elements=nullptr;UINT count=0;
            if(!declarationCache.get(decl,elements,count))return {};
            return NorthlightReplayBounds::EnvelopeCache::prepareProgram(*program->second,elements,count);
        };
        auto prepared=replayBoundsMetadata.get(shader,decl,build);
        if(prepared||built)return prepared; /* built and still null: no program/declaration */
        for(const auto& f:replayBoundsFallbacks)if(f.shader==shader&&f.decl==decl)return f.prepared;
        if(replayBoundsFallbacks.size()>=FallbackPreparations){++replayBoundsCapped;return {};}
        prepared=build();
        try{replayBoundsFallbacks.push_back({shader,decl,prepared});}catch(...){}
        return prepared;
    }
    void replayBoundsKick(){
        if(replayBoundsResolved||!NorthlightReplayBoundsJob::Async||replayBoundsWorker.pending())return;
        const auto start=std::chrono::steady_clock::now();
        // Allocation or thread failure leaves the frame to the synchronous pass,
        // before any per-frame cache state has advanced.
        auto& items=replayBoundsWorker.items();
        try{items.reserve(replays.size());replayBoundsFallbacks.reserve(FallbackPreparations);}catch(...){return;}
        if(!replayBoundsWorker.ready())return;
        const bool diagnostics=replayBoundsDiagnosticSample(); /* logs and envelope timing fields only */
        pointBoundsValid=0;replayBoundsCache.beginFrame(diagnostics,replays.size(),replayBoundsManyPointShadows(),replayCrowdBoundsOperations(),fixedReplayBoundsWork(),quality.coldLampBounds!=0);replayBoundsMetadata.beginFrame();
        // Program/declaration lookups stay on the render thread.
        replayBoundsFallbacks.clear();replayBoundsCapped=0;
        replayBoundsWorker.nearList().begin(actorMatrices[0],actorValid[0],actorMatrices[1],actorValid[1]); /* the actor maps as last drawn: casters that can be wholly inside one are visited first, the self's own packets before all */
        NorthlightReplayBoundsJob::gather(items,replays,[&](IDirect3DVertexShader9* shader,IDirect3DVertexDeclaration9* decl){return replayBoundsPrepared(shader,decl);},&replayBoundsWorker.nearList());
        replayBoundsFallbacks.clear(); /* pins now live in the items only */
        if(replayBoundsWorker.start(replayBoundsCache,context.inverseView))++replayBoundsRuns;
        replayBoundsDiagnostics=diagnostics;
        replayBoundsKickMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    }
    // First pointBounds reader; also when render() returns.
    void completeMaterialBounds(){
        if(pointBoundsValid==replays.size())return;
        for(auto& p:replays){
            if(p->pointBounds.valid||!p->materialViewBounds.valid)continue;
            p->pointBounds=NorthlightPoseBounds::inWorld(p->materialViewBounds,context.inverseView);
            if(p->pointBounds.valid){++pointBoundsValid;++pointMaterialBoundsUsed;}
        }
    }
    void replayBoundsJoin(){
        if(!replayBoundsWorker.pending())return;
        const auto start=std::chrono::steady_clock::now();
        const auto result=replayBoundsWorker.finish();
        const double waitMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        NorthlightReplayBoundsJob::apply(replayBoundsWorker.items(),replays,!result.failed);
        replayBoundsResolved=true;
        pointBoundsValid=result.failed?0:result.totals.valid;pointBoundVertices=result.totals.boundVertices;pointBoundOperations=result.totals.boundOperations;
        if(!result.failed)completeMaterialBounds();
        if(NorthlightDiagnostics::enabled()){replayNearSize=unsigned(replayBoundsWorker.nearList().size());replayNearStops+=result.totals.nearStops;}
        if(replayBoundsDiagnostics){
            const double mainMs=replayBoundsKickMs+std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            pointLogReplayBounds(result.totals,result.workerMs,mainMs,result.workerMs,waitMs,result.startLagMs,true);
        }
    }
    // Before the bounds cache or the packets change outside render(): the job's
    // results are discarded (the packets still hold cleared bounds).
    void replayBoundsAbandon(){
        replayBoundsResolved=false;
        if(!replayBoundsWorker.pending())return;
        replayBoundsWorker.finish(true);
        for(auto& item:replayBoundsWorker.items())item=NorthlightReplayBoundsJob::Item{};
    }
    void pointCalculateReplayBounds(){
        if(replayBoundsResolved)return;
        if(replayBoundsWorker.pending())return; /* 0.3.143: in flight since selection, joined by the first reader */
        ++replayBoundsRuns;
        const auto start=std::chrono::steady_clock::now();
        using WorkKind=NorthlightReplayBounds::WorkKind;
        NorthlightReplayBounds::Budget cheapBudget,heavyBudget,buildBudget;
        cheapBudget.maxVertices=0;cheapBudget.maxOperations=163840;
        heavyBudget.maxVertices=0;heavyBudget.maxOperations=32768;
        buildBudget.maxVertices=2048;buildBudget.maxOperations=65536;
        const bool diagnostics=replayBoundsDiagnosticSample(); /* logs and envelope timing fields only */
        pointBoundsValid=0;replayBoundsCache.beginFrame(diagnostics,replays.size(),replayBoundsManyPointShadows(),replayCrowdBoundsOperations(),fixedReplayBoundsWork(),quality.coldLampBounds!=0);replayBoundsMetadata.beginFrame();
        // Hints contain no entry/declaration pointers. Current pose bounds and
        // classifications never survive to another frame or recycled packet.
        for(auto& p:replays){p->pointBounds={};p->boundsWork={};p->boundsPrepared.reset();}
        size_t cheapVisited=0,heavyVisited=0,buildVisited=0;
        const auto visit=[&](size_t index,unsigned phase){
            auto& p=replays[index];if(p->pointBounds.valid)return false;
            // This check precedes declaration/program/cache lookups. In a warm
            // crowd the cold pass consequently performs no repeated lookups.
            if(phase==1&&p->boundsWork.kind!=WorkKind::Heavy)return false;
            if(phase==2&&p->boundsWork.kind!=WorkKind::Cold)return false;
            // The final cheap turn may consume a source completed by the cold
            // turn, using only the remaining evaluation budget. It cannot
            // build unfinished sources or spend the closed heavy allowance.
            if((phase==0||phase==3)&&(p->boundsWork.kind==WorkKind::Heavy||p->boundsWork.kind==WorkKind::Unsupported||(phase==0&&p->boundsWork.kind==WorkKind::Cold)))return false;
            if(!p->shared){p->boundsWork.kind=WorkKind::Unsupported;return false;}
            if((phase==0||phase==3)&&!p->boundsPrepared){
                p->boundsPrepared=replayBoundsMetadata.get(p->originalShader,p->decl,[&]()->std::shared_ptr<const NorthlightReplayBounds::Prepared>{
                    // Key preparation used to run inside each cache lookup.
                    // Charge misses to the same budget; hits need no new timer.
                    struct PreparationCost {
                        NorthlightReplayBounds::Cache& cache;
                        std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
                        ~PreparationCost(){cache.chargeCheapPreparation(std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count()));}
                    } cost{replayBoundsCache};
                    auto program=actorPrograms.find(p->originalShader);
                    if(program==actorPrograms.end())return {};
                    const D3DVERTEXELEMENT9* elements=nullptr;UINT count=0;
                    if(!declarationCache.get(p->decl,elements,count))return {};
                    return NorthlightReplayBounds::EnvelopeCache::prepareProgram(*program->second,elements,count);
                });
            }
            NorthlightReplayBounds::Status status;
            if(p->boundsPrepared){
                const auto& prepared=*p->boundsPrepared;
                if(phase==0||phase==3){++cheapVisited;status=replayBoundsCache.calculateCheap(prepared,p->mesh(),p->shared,p->constants,context.inverseView,cheapBudget,p->pointBounds,p->boundsWork);}
                else if(phase==1){++heavyVisited;status=replayBoundsCache.evaluateHeavy(prepared,p->mesh(),p->shared,p->constants,context.inverseView,heavyBudget,p->pointBounds,p->boundsWork);}
                else{++buildVisited;status=replayBoundsCache.buildEnclosed(prepared,p->mesh(),p->shared,p->constants,context.inverseView,buildBudget,p->pointBounds);}
            }else{
                // Allocation/metadata-cap failures retain the audited full key
                // path and do not turn missing metadata into missing shadows.
                auto program=actorPrograms.find(p->originalShader);
                if(program==actorPrograms.end()){p->boundsWork.kind=WorkKind::Unsupported;return false;}
                const D3DVERTEXELEMENT9* elements=nullptr;UINT count=0;
                if(!declarationCache.get(p->decl,elements,count)){p->boundsWork.kind=WorkKind::Unsupported;return false;}
                if(phase==0||phase==3){++cheapVisited;status=replayBoundsCache.calculateCheap(*program->second,p->mesh(),p->shared,elements,count,p->constants,context.inverseView,cheapBudget,p->pointBounds,p->boundsWork);}
                else if(phase==1){++heavyVisited;status=replayBoundsCache.evaluateHeavy(*program->second,p->mesh(),p->shared,elements,count,p->constants,context.inverseView,heavyBudget,p->pointBounds,p->boundsWork);}
                else{++buildVisited;status=replayBoundsCache.buildEnclosed(*program->second,p->mesh(),p->shared,elements,count,p->constants,context.inverseView,buildBudget,p->pointBounds);}
            }
            if(status==NorthlightReplayBounds::Status::Valid)++pointBoundsValid;
            return status==NorthlightReplayBounds::Status::Budget;
        };
        replayBoundsCache.readyResume(NorthlightReplaySchedule::pass(replays.size(),replayBoundsCache.readyStart(replays.size()),
            [&](){return replayBoundsCache.canCheap()&&cheapBudget.operations<cheapBudget.maxOperations;},[&](size_t index){return visit(index,0);}));
        replayBoundsCache.heavyResume(NorthlightReplaySchedule::pass(replays.size(),replayBoundsCache.heavyStart(replays.size()),
            [&](){return replayBoundsCache.canHeavy()&&heavyBudget.operations<heavyBudget.maxOperations;},[&](size_t index){return visit(index,1);}));
        replayBoundsCache.lendToBuild(buildBudget);
        replayBoundsCache.continuePendingBuild(buildBudget);
        replayBoundsCache.buildResume(NorthlightReplaySchedule::pass(replays.size(),replayBoundsCache.buildStart(replays.size()),
            [&](){return replayBoundsCache.canBuild()&&buildBudget.vertices<buildBudget.maxVertices&&buildBudget.operations<buildBudget.maxOperations;},[&](size_t index){return visit(index,2);}));
        const size_t reservedCheapVisited=cheapVisited;
        replayBoundsCache.finishReservedTurnsAndLend();
        const size_t operationLimit=replayBoundsCache.operationLimit();
        cheapBudget.maxOperations=operationLimit-heavyBudget.operations-buildBudget.operations;
        // Preserve the first turn's cursor for the next frame. Advancing it
        // through this bonus turn could strand cold/heavy packets exclusively
        // after their reserved phases on every frame of a periodic draw list.
        NorthlightReplaySchedule::pass(replays.size(),replayBoundsCache.readyStart(replays.size()),
            [&](){return replayBoundsCache.canCheap()&&cheapBudget.operations<cheapBudget.maxOperations;},[&](size_t index){return visit(index,3);});
        replayBoundsCache.settleClock();
        const size_t evaluationOperations=cheapBudget.operations+heavyBudget.operations;
        buildBudget.maxOperations=std::min(buildBudget.maxOperations,evaluationOperations>=operationLimit?size_t(0):operationLimit-evaluationOperations);
        const auto live=[] {return true;};const auto cold=[&](size_t index){return !replays[index]->pointBounds.valid&&replays[index]->boundsWork.kind==WorkKind::Cold;};
        if(buildVisited||replayBoundsCache.hasPendingBuild())NorthlightReplaySchedule::finishCold(replays.size(),replayBoundsCache.buildStart(replays.size()),replayBoundsCache,cheapBudget,heavyBudget,buildBudget,live,visit,cold);
        pointBoundVertices=buildBudget.vertices;pointBoundOperations=cheapBudget.operations+heavyBudget.operations+buildBudget.operations;
        completeMaterialBounds();
        replayBoundsResolved=true;
        const double wallMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        if(diagnostics)pointLogReplayBounds({cheapVisited,heavyVisited,buildVisited,reservedCheapVisited,pointBoundVertices,pointBoundOperations,pointBoundsValid},wallMs,wallMs,0,0,0,false);
    }
    void replayBoundsRequest(){
        if(replayBoundsResolved||replayBoundsWorker.pending())return;
        replayBoundsKick();pointCalculateReplayBounds();
    }
    void pointLogReplayBounds(const NorthlightReplayBoundsJob::Totals& visits,double wallMs,double mainMs,double workerMs,double waitMs,double startLagMs,bool async){
        using WorkKind=NorthlightReplayBounds::WorkKind;
        const size_t cheapVisited=visits.cheapVisited,heavyVisited=visits.heavyVisited,buildVisited=visits.buildVisited,reservedCheapVisited=visits.reservedCheapVisited;
        if(NorthlightDiagnostics::enabled()){ /* callers pass only sampled frames */
            size_t unclassified=0,cheap=0,heavy=0,cold=0,unsupported=0;
            for(const auto& p:replays)switch(p->boundsWork.kind){
                case WorkKind::Unknown:++unclassified;break;case WorkKind::Cheap:++cheap;break;
                case WorkKind::Heavy:++heavy;break;case WorkKind::Cold:++cold;break;case WorkKind::Unsupported:++unsupported;break;
            }
            const auto& stats=replayBoundsCache.envelopeStats();
            deferLogf("WORLD replay bounds total=%zu valid=%u envelopeValid=%u fallbackValid=%u deferred=%u buildVertices=%zu operations=%zu envelopeEntries=%zu envelopeBytes=%zu envelopeMs=%.3f readyMs=%.3f heavyMs=%.3f buildMs=%.3f classifyMs=%.3f wallMs=%.3f skinValid=%zu coldVisits=%zu buildIndices=%zu completed=%zu timeDeferred=%zu vertexDeferred=%zu operationDeferred=%zu unsupported=%zu tupleLimit=%zu invalid=%zu evictions=%zu readyEvictions=%zu expiredEvictions=%zu releasedGroupBytes=%zu buildSkippedReady=%zu cheapValid=%zu heavyAttempts=%zu heavyValid=%zu cheapSliceDeferred=%zu async=%u mainMs=%.3f workerMs=%.3f waitMs=%.3f startLagMs=%.3f fallbackCapped=%zu sourceIdentityHits=%zu sourceReconstructedHits=%zu rigidPeerHits=%zu",
                replays.size(),pointBoundsValid,replayBoundsCache.envelopeValid(),replayBoundsCache.fallbackValid(),replayBoundsCache.deferred(),stats.buildVertices,pointBoundOperations,
                replayBoundsCache.envelopeEntries(),replayBoundsCache.envelopeBytes(),replayBoundsCache.envelopeMilliseconds(),
                replayBoundsCache.envelopeReadyMilliseconds(),replayBoundsCache.envelopeHeavyMilliseconds(),replayBoundsCache.envelopeBuildMilliseconds(),
                replayBoundsCache.envelopeClassifyMilliseconds(),wallMs,stats.skinValid,stats.cold,stats.buildIndices,stats.completed,
                stats.timeDeferred,stats.vertexDeferred,stats.operationDeferred,stats.unsupported,stats.tupleLimit,stats.invalid,stats.evictions,stats.readyEvictions,stats.expiredEvictions,stats.releasedGroupBytes,stats.buildSkippedReady,
                stats.cheapValid,stats.heavyAttempts,stats.heavyValid,stats.cheapSliceDeferred,unsigned(async),mainMs,workerMs,waitMs,startLagMs,async?replayBoundsCapped:size_t(0),stats.sourceIdentityHits,stats.sourceReconstructedHits,stats.rigidPeerHits);
            deferLogf("WORLD replay priority cheapVisited=%zu heavyVisited=%zu buildVisited=%zu cheap=%zu heavy=%zu cold=%zu unsupported=%zu unclassified=%zu",
                cheapVisited,heavyVisited,buildVisited,cheap,heavy,cold,unsupported,unclassified);
            deferLogf("WORLD shadow bounds coverage total=%zu valid=%u unbounded=%zu readyBudgetMs=%.3f buildBudgetMs=%.3f shared=directional-and-point",
                replays.size(),pointBoundsValid,replays.size()-pointBoundsValid,double(replayBoundsCache.phaseLimit(0))/1000000.,double(replayBoundsCache.phaseLimit(2))/1000000.);
            deferLogf("WORLD replay build pending=%u pendingVertices=%zu pendingIndices=%zu resumes=%zu admissionDeferred=%zu ownerExpired=%zu abandoned=%zu",
                unsigned(replayBoundsCache.hasPendingBuild()),replayBoundsCache.pendingBuildVertices(),replayBoundsCache.pendingBuildIndices(),
                stats.pendingResumes,stats.pendingDeferred,stats.pendingOwnerExpired,stats.pendingAbandoned);
            const auto& metadata=replayBoundsMetadata.stats();
            deferLogf("WORLD replay adaptive fastValid=%zu borrowedUs=%.3f extraCheapVisited=%zu preparedHits=%zu preparedMisses=%zu preparedNew=%zu preparedFallbacks=%zu preparedEvictions=%zu preparedEntries=%zu preparedBytes=%zu diagnosticsSampled=1",
                stats.fastValid,replayBoundsCache.borrowedMicroseconds(),cheapVisited-reservedCheapVisited,metadata.hits,metadata.misses,metadata.prepared,metadata.fallbacks,metadata.evictions,replayBoundsMetadata.entries(),replayBoundsMetadata.bytes());
            const auto& slow=replayBoundsCache.lastExpensiveEnvelope();
            if(slow.valid){
                const char* stage=slow.stage==NorthlightReplayBounds::EnvelopeCache::Stage::Cheap?"cheap":"heavy";
                const char* result=slow.status==NorthlightReplayBounds::Status::Valid?"valid":slow.status==NorthlightReplayBounds::Status::Budget?"budget":slow.status==NorthlightReplayBounds::Status::Invalid?"invalid":"unsupported";
                deferLogf("WORLD replay expensive cacheFrame=%llu program=%016llx groups=%zu operations=%zu elapsedUs=%.3f stage=%s result=%s skin=%u",
                    (unsigned long long)slow.frame,(unsigned long long)slow.programHash,slow.groups,slow.operations,slow.elapsedUs,stage,result,unsigned(slow.skin));
            }
        }
    }
