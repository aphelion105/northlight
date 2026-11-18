#pragma once
inline void WorldColdUploadTest::workerEntry(UploadFaultDevice& d,unsigned target,NorthlightGI::Vec3 center){
    auto renderer=std::make_unique<WorldRenderer>(&d);auto& w=*renderer;
    w.valid=true;w.lastRequest.map="Northrend";NorthlightDiagnostics::configure(true);
    assert(w.resources(3840,2160,D3DFMT_A16B16G16R16F));
    auto* baseline=warmBackend(d);
    AddressBallast pressure;pressure.fill(target);
    const DWORD start=GetTickCount();
    {
        std::lock_guard<std::mutex> lock(w.mutex);auto& r=w.request;
        r.enabled=r.giEnabled=r.fogEnabled=true;r.map=w.lastRequest.map;r.camera=r.geometryCenter=r.probeCenter=center;
        r.queuedAt=start;++r.id;w.pending=true;
    }
    w.wake.notify_one();
    std::shared_ptr<WorldRenderer::Snapshot> snapshot;
    DWORD regionMs=0;
    while(DWORD(GetTickCount()-start)<45000){
        assert(!w.workerFaultCode.load());
        {std::lock_guard<std::mutex> lock(w.mutex);snapshot=w.published;}
        if(snapshot&&snapshot->meshPlan&&snapshot->bvh){
            if(!regionMs)regionMs=GetTickCount()-start;
            if(snapshot->serial)break;
        }
        Sleep(5);
    }
    assert(snapshot&&snapshot->serial&&snapshot->meshPlan&&snapshot->bvh&&!w.uploaded.lock());
    const auto payload=snapshot->meshPlan->payload.share();assert(payload&&payload->pages->temporary);
    w.active=snapshot;assert(w.geometryResources(3840,2160));w.stageScreenProbes(snapshot->map);
    assert(w.probeAtlasReady(snapshot->map));
    const DWORD giMs=GetTickCount()-start;
    Spooling=true;complete(w,snapshot);drain(d);
    const DWORD readyMs=GetTickCount()-start;
    assert(w.worldMeshDrawable()&&pressure.available()>=NorthlightGeometryMemory::ProcessReserve);
    verify(w,0,false,payload.get());
    NorthlightMeshPageKey::Hasher pageHash;
    for(const auto& page:payload->pages->pages){
        pageHash.word(page.key.lo);pageHash.word(page.key.hi);
        pageHash.word(page.key.vertexBytes);pageHash.word(page.key.indexBytes);
    }
    const auto digest=pageHash.finish(0,0);
    std::printf("ACTUAL_WORKER_ENTRY_PASS entryMiB=%u regionMs=%lu giMs=%lu meshMs=%lu pages=%zu triangles=%u reach=%.0f pageDigest=%016llx%016llx exactRaster=1\n",
        target,(unsigned long)regionMs,(unsigned long)giMs,(unsigned long)readyMs,payload->pages->pages.size(),
        snapshot->meshPlan->triangleCount,snapshot->meshPlan->shadowTerrainReach,(unsigned long long)digest.lo,(unsigned long long)digest.hi);
    baseline->Release();
}
