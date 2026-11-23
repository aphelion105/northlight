// Directional static-cache redraw planning and worker handoff.
    // Static-model-only content changes redraw just the texels the changed
    // slices can touch (old and new bounds), under an identical scissor, with the
    // complete ordered draw list: per pixel this is the full redraw. false=0.3.135.
    static constexpr bool StaticCacheDirtyRects=true;static constexpr long StaticCacheDirtyMargin=2,StaticCacheDirtyTile=64;static constexpr size_t StaticCacheDirtyMaxRects=8;
    unsigned shadowCachePartialRenders=0,shadowCacheStaticFullRenders=0,shadowCachePartialSkippedDraws=0,shadowCachePartialRects=0;unsigned long long shadowCachePartialTexels=0;
    // 0.3.151 StaticCacheSlices>1: per slot, a rect redraw in progress over several frames.
    NorthlightStaticSlices::Cycle staticSlices[4];unsigned staticSliceBands=0,staticSliceRestarts=0;
    std::vector<StaticShadow::CasterBounds> staticDirtyScratch;std::vector<NorthlightShadowBounds::TexelRect> staticDirtyFootprints,staticDirty;std::vector<D3DRECT> staticDirtyClears;std::vector<const NorthlightShadowBounds::TexelRect*> staticDirtyHits;std::vector<NorthlightShadowBounds::TexelRect> staticDirtyBounding;std::vector<size_t> staticDirtyCosts;
    // 0.3.137 direction-step plan prebuild (static_plan_prebuild.h).
    NorthlightStaticPrebuild::Scheduler staticPrebuild;V rawSourceDirections[2];
    int staticScissorCaps=-1;unsigned staticDedupUncommitted=0;unsigned shadowCachePartialBounding=0,shadowCacheCostFull=0;unsigned long long shadowCachePartialCalls=0,shadowCachePartialFullCalls=0;
    // 0.3.152: the buffers and counters of one dirty-rect evaluation: the render
    // thread's members, or a kicked slot's StaticDirtyJob (on the plan worker).
    struct DirtyWork {std::vector<StaticShadow::CasterBounds>& scratch;std::vector<NorthlightShadowBounds::TexelRect>& footprints;std::vector<NorthlightShadowBounds::TexelRect>& dirty;std::vector<NorthlightShadowBounds::TexelRect>& bounding;std::vector<size_t>& costs;
        unsigned& boundingChoices;unsigned& costFull;unsigned long long& calls;unsigned long long& fullCalls;};
    struct StaticDirtyJob {
        bool eligible=false,ready=false,result=false;uint64_t frame=0;
        std::vector<StaticShadow::CasterBounds> scratch;std::vector<NorthlightShadowBounds::TexelRect> footprints,dirty,bounding;std::vector<size_t> costs;
        unsigned boundingChoices=0,costFull=0;unsigned long long calls=0,fullCalls=0;
        DirtyWork work(){return {scratch,footprints,dirty,bounding,costs,boundingChoices,costFull,calls,fullCalls};}
    };
    StaticDirtyJob staticDirtyJobs[4];float staticSlotMatrix[4][16]={};unsigned long long staticDirtyJobsUsed=0;double staticKickMs=0; /* kickMs: render-thread kick incl. the first slot's prepare */
    // R3: renderer-side inputs of a worker result (keys, batches, terrain chunks) change only
    // after a settle; these writers also drop any result not used yet.
    void dropStaticDirtyJobs(){for(auto& job:staticDirtyJobs)job.ready=false;}
    // Disjoint texel rects covering every changed static slice (old and new).
    // 0.3.152: a slot whose plan the plan worker built may already hold the result
    // (same inputs, frozen since the kick: the slot's key, its plan, the batches).
    bool staticDirtyRects(const ShadowCacheKey& key,const float* matrix,bool staticChanged=true){
        staticDirty.clear();staticDirtyFootprints.clear();
        if(staticScissorCaps<0){D3DCAPS9 caps{};staticScissorCaps=SUCCEEDED(d->GetDeviceCaps(&caps))&&(caps.RasterCaps&D3DPRASTERCAPS_SCISSORTEST)?1:0;}if(!staticScissorCaps)return false;
        const ptrdiff_t slot=&key-shadowCacheKey;
        if(slot>=0&&slot<4&&staticChanged){auto& job=staticDirtyJobs[slot];staticCasters.joinPlan(matrix);
            if(job.ready&&job.frame==staticFrame&&staticCasters.kickedModeCurrent()&&!std::memcmp(staticSlotMatrix[slot],matrix,sizeof staticSlotMatrix[slot])){
                job.ready=false;staticDirty.swap(job.dirty);shadowCachePartialBounding+=job.boundingChoices;shadowCacheCostFull+=job.costFull;shadowCachePartialCalls+=job.calls;shadowCachePartialFullCalls+=job.fullCalls;++staticDirtyJobsUsed;
                return job.result;}}
        return dirtyRectsFrom({staticDirtyScratch,staticDirtyFootprints,staticDirty,staticDirtyBounding,staticDirtyCosts,shadowCachePartialBounding,shadowCacheCostFull,shadowCachePartialCalls,shadowCachePartialFullCalls},
            key,matrix,staticChanged,staticCasters.stats().instancing,staticCasters);
    }
    // The CPU part, for either thread. Plan: the GpuCache or the worker's detached
    // view (same changedBounds/drawCalls).
    template<class Plan> bool dirtyRectsFrom(DirtyWork w,const ShadowCacheKey& key,const float* matrix,bool staticChanged,bool instancing,const Plan& plan){
        w.dirty.clear();w.footprints.clear();size_t models=0;
        w.scratch.clear();
        if(staticChanged){try {if(!plan.changedBounds(matrix,key.staticContent,w.scratch,models))return false;}catch(...){return false;}}
        // Unchanged static content is redrawn inside the rects too: it must be the recorded
        // content in the same instancing mode (changedBounds checks this when static changed).
        else if(!key.staticContent.valid||key.staticContent.instancing!=instancing)return false;
        for(const auto& box:w.scratch){NorthlightShadowBounds::TexelRect footprint;
            if(!box.bounded||!NorthlightShadowBounds::texelFootprint(box.low,box.high,matrix,long(ShadowCacheSize),StaticCacheDirtyMargin,footprint))return false;
            w.footprints.push_back(footprint);}
        NorthlightShadowBounds::dirtyRects(w.footprints,long(ShadowCacheSize),StaticCacheDirtyTile,StaticCacheDirtyMaxRects,w.dirty);
        // A wide draw is issued once per rect it touches. Pick the cheapest
        // exact option by draw calls: disjoint rects, their bounding rect, or full.
        w.bounding.clear();if(w.dirty.size()>1){NorthlightShadowBounds::TexelRect all;for(const auto& r:w.dirty)all=NorthlightShadowBounds::unite(all,r);w.bounding.push_back(all);}
        const std::vector<const std::vector<NorthlightShadowBounds::TexelRect>*> candidates={nullptr,&w.dirty,w.bounding.empty()?&w.dirty:&w.bounding};
        try {plan.drawCalls(matrix,candidates,long(ShadowCacheSize),StaticCacheDirtyMargin,w.costs);}catch(...){return false;}
        const bool affine=NorthlightShadowBounds::affineLightMatrix(matrix);
        directionalCacheMesh.visit(batches,meshGeneration,fixedTerrainChunks(),fixedTerrainBits,[&](const Batch& b){
            if(affine&&NorthlightShadowBounds::directionalClipRejectAffine(b.boundsLow,b.boundsHigh,matrix))return true;
            NorthlightShadowBounds::TexelRect footprint;const bool known=NorthlightShadowBounds::texelFootprint(b.boundsLow,b.boundsHigh,matrix,long(ShadowCacheSize),StaticCacheDirtyMargin,footprint);
            ++w.costs[0];for(size_t k=1;k<candidates.size();++k)for(const auto& r:*candidates[k])if(!known||NorthlightShadowBounds::intersects(footprint,r))++w.costs[k];
            return true;
        });
        if(w.costs[2]<w.costs[1]){w.dirty.swap(w.bounding);w.costs[1]=w.costs[2];++w.boundingChoices;}
        if(w.costs[1]>=w.costs[0]){++w.costFull;return false;}
        w.calls+=w.costs[1];w.fullCalls+=w.costs[0];
        return true;
    }
    // 0.3.152 (static_plan_job.h): with every plan input final for the frame, the
    // first drawn slot's plan is prepared now and the later slots' missing plans
    // are built on the plan worker in loop order, joined at each slot's first use
    // (GpuCache::kickPlans). A later slot that may take the static-models rect path
    // (valid key and content, no placement reason) gets its
    // dirty-rect CPU part computed there right after its build. Until the join the
    // job reads only frozen state: that slot's key, the batches and terrain chunks.
    void staticPlanKick(const NorthlightWorldMath::ShadowCachePlacement* placements,const bool* sourceActive){
        int order[4];unsigned count=0;
        for(int source=0;source<2;++source)if(sourceActive[source])for(int cascade=0;cascade<2;++cascade)order[count++]=source*2+cascade;
        if(count<2)return;
        if(staticScissorCaps<0){D3DCAPS9 caps{};staticScissorCaps=SUCCEEDED(d->GetDeviceCaps(&caps))&&(caps.RasterCaps&D3DPRASTERCAPS_SCISSORTEST)?1:0;}
        for(auto& job:staticDirtyJobs){job.ready=job.eligible=false;job.frame=staticFrame;job.boundingChoices=job.costFull=0;job.calls=job.fullCalls=0;}
        for(unsigned i=1;i<count;++i){const int slot=order[i];const auto& key=shadowCacheKey[slot];
            staticDirtyJobs[slot].eligible=StaticCacheDirtyRects&&staticScissorCaps==1&&!placements[slot].reason&&key.valid&&key.staticContent.valid;}
        const float* later[3];for(unsigned i=1;i<count;++i)later[i-1]=staticSlotMatrix[order[i]];
        // A throwing changedBounds/drawCalls leaves the slot to the render thread (which returns false only if it throws there too).
        struct Traced {const StaticShadow::GpuCache::DetachedView& view;bool& threw;
            bool changedBounds(const float* m,const StaticShadow::GpuCache::ContentRecord& old,std::vector<StaticShadow::CasterBounds>& out,size_t& models)const{try{return view.changedBounds(m,old,out,models);}catch(...){threw=true;throw;}}
            void drawCalls(const float* m,const std::vector<const std::vector<NorthlightShadowBounds::TexelRect>*>& c,long size,long margin,std::vector<size_t>& out)const{try{view.drawCalls(m,c,size,margin,out);}catch(...){threw=true;throw;}}};
        try {staticCasters.kickPlans(staticSlotMatrix[order[0]],later,count-1,[this,order,count](unsigned,const StaticShadow::GpuCache::DetachedView& view){
                for(unsigned i=1;i<count;++i){const int slot=order[i];auto& job=staticDirtyJobs[slot];
                    if(!job.eligible||std::memcmp(staticSlotMatrix[slot],view.matrix(),sizeof staticSlotMatrix[slot])||view.signature()==shadowCacheKey[slot].staticSignature)continue; /* same signature: no static change */
                    bool threw=false;
                    try {const bool result=dirtyRectsFrom(job.work(),shadowCacheKey[slot],staticSlotMatrix[slot],true,false,Traced{view,threw});if(!threw){job.result=result;job.ready=true;}}catch(...){}
                }});}
        catch(...){staticCasters.reset();staticOwnerGeneration=UINT64_MAX;staticRetryTick=GetTickCount();} /* the first slot's prepare, as staticSignature() */
    }
    // Debug self-check (off): after each rect redraw, render the same slot in full
    // into a scratch target, read both back and count differing texels.
    static constexpr bool StaticCacheDirtyRectVerify=false;
    unsigned shadowCacheVerified=0,shadowCacheVerifyMismatches=0,shadowCacheScissorLeaks=0;
    template<class Render> bool verifyStaticCache(int slot,Render render){
        unsigned long long differing=0;
        if(!directionalMaps.verify(d,slot,[&](HRESULT hr,const char* name){return check(hr,name);},
            render,differing))return false;
        ++shadowCacheVerified;if(differing){++shadowCacheVerifyMismatches;logf("WORLD shadow cache VERIFY MISMATCH slot=%d texels=%llu",slot,differing);}
        return true;
    }
