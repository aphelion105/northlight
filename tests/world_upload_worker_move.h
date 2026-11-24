#pragma once
inline void WorldColdUploadTest::workerMove(UploadFaultDevice& d,unsigned target,D3DPRESENT_PARAMETERS&,const std::string&,NorthlightGI::Vec3 center){
    DeviceLocal=true;Spooling=false;
    auto renderer=std::make_unique<WorldRenderer>(&d);auto& w=*renderer;w.valid=true;
    w.quality.actorShadows=w.quality.actorShadowDetail=1;w.lastRequest.map="Northrend";
    assert(w.resources(3840,2160,D3DFMT_A16B16G16R16F));auto* baseline=warmBackend(d);
    const auto request=[&](NorthlightGI::Vec3 position){
        {std::lock_guard<std::mutex> lock(w.mutex);
         auto& r=w.request;r.enabled=true;r.giEnabled=r.fogEnabled=false;r.map="Northrend";
         r.camera=r.probeCenter=r.geometryCenter=position;r.queuedAt=GetTickCount();++r.id;w.pending=true;}
        w.wake.notify_one();
    };
    // Both regions must use the same worker-owned preparation and source cache.
    request(center);
    const auto warmStart=GetTickCount();std::shared_ptr<WorldRenderer::Snapshot> snapshot;
    while(DWORD(GetTickCount()-warmStart)<45000){
        assert(!w.workerFaultCode.load());
        {std::lock_guard<std::mutex> lock(w.mutex);if(w.published&&w.published->bvh)snapshot=w.published;}
        if(snapshot)break;
        assert(SUCCEEDED(d.Present(nullptr,nullptr,nullptr,nullptr)));Sleep(10);
    }
    assert(snapshot&&snapshot->meshPlan);
    Spooling=snapshot->meshPlan->payload.share()->pages->temporary!=nullptr;
    complete(w,snapshot);drain(d);
    assert(w.geometryResources(3840,2160));
    NorthlightStreaming::cpuRetirement().waitIdle(5000);
    const auto previousTable=w.meshStore.table();AddressBallast pressure;pressure.fill(target);
    const auto entry=pressure.available();const auto started=GetTickCount();
    const auto moved=center+NorthlightGI::Vec3(48,0,0);
    request(moved);std::shared_ptr<WorldRenderer::Snapshot> next;
    while(DWORD(GetTickCount()-started)<15000){
        assert(!w.workerFaultCode.load());
        {std::lock_guard<std::mutex> lock(w.mutex);if(w.published&&w.published->bvh&&w.published->bvh!=snapshot->bvh)next=w.published;}
        assert(w.worldMeshDrawable()&&w.meshStore.table()==previousTable);
        if(next)break;assert(SUCCEEDED(d.Present(nullptr,nullptr,nullptr,nullptr)));Sleep(10);
    }
    assert(next&&"Moved region must finish while the old scene remains drawable");
    uint64_t sharedBytes=0;
    {
        std::set<const uint8_t*> oldTexels,newTexels;
        using TexelKey=NorthlightMeshPageKey::Key;
        std::unordered_multimap<TexelKey,const NorthlightGI::WorldMaterial*,NorthlightMeshPageKey::KeyHash> oldContents;
        const auto contentKey=[](const NorthlightGI::WorldMaterial& material){
            const auto pixels=material.texels();
            return NorthlightMeshPageKey::keyOf(pixels.data(),pixels.size(),nullptr,0);
        };
        uint64_t oldBytes=0,newBytes=0,reusableBytes=0;
        for(const auto& m:snapshot->bvh->scene().materials){
            if(!m.sharedTexels||!oldTexels.insert(m.sharedTexels.get()).second)continue;
            oldBytes+=m.sharedTexelBytes;
            oldContents.emplace(contentKey(m),&m);
        }
        for(const auto& m:next->bvh->scene().materials){
            if(!m.sharedTexels||!newTexels.insert(m.sharedTexels.get()).second)continue;
            newBytes+=m.sharedTexelBytes;
            if(oldTexels.count(m.sharedTexels.get()))sharedBytes+=m.sharedTexelBytes;
            const auto candidates=oldContents.equal_range(contentKey(m));
            if(std::any_of(candidates.first,candidates.second,[&](const auto& old){return old.second->texels()==m.texels();}))
                reusableBytes+=m.sharedTexelBytes;
        }
        std::printf("ACTUAL_WORKER_MOVE_TEXELS oldBytes=%llu newBytes=%llu sharedBytes=%llu reusableBytes=%llu\n",
            (unsigned long long)oldBytes,(unsigned long long)newBytes,(unsigned long long)sharedBytes,(unsigned long long)reusableBytes);
        assert(reusableBytes&&sharedBytes==reusableBytes);
    }
    Spooling=next->meshPlan->payload.share()->pages->temporary!=nullptr;
    complete(w,next);drain(d);const auto available=pressure.available();assert(available>=NorthlightGeometryMemory::ProcessReserve);
    for(auto* block:pressure.blocks)assert(VirtualFree(block,0,MEM_RELEASE));pressure.blocks.clear();
    std::printf("ACTUAL_WORKER_MOVE_PASS entryMiB=%.3f afterMiB=%.3f sharedTexelMiB=%.3f wallMs=%lu oldMeshHeld=1 replacementCommitted=1\n",entry/1048576.,available/1048576.,sharedBytes/1048576.,(unsigned long)DWORD(GetTickCount()-started));
    baseline->Release();
}
