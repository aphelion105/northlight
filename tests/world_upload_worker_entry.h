#pragma once
inline void WorldColdUploadTest::workerEntry(UploadFaultDevice& d,unsigned target,NorthlightGI::Vec3 center,bool fragment){
    auto renderer=std::make_unique<WorldRenderer>(&d);auto& w=*renderer;
    w.valid=true;w.lastRequest.map="Northrend";NorthlightDiagnostics::configure(true);
    assert(w.resources(3840,2160,D3DFMT_A16B16G16R16F));
    auto* baseline=warmBackend(d);
    AddressBallast pressure;pressure.fill(target,fragment);
    std::printf("WORKER_ADDRESS entryMiB=%u largestMiB=%llu fragmented=%u\n",target,(unsigned long long)(pressure.largest()>>20),unsigned(fragment));
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
    // Compare staged coverage to the GI scene's original RGBA, independently
    // of the packer, including differently sized textures reduced to white.
    uint64_t storedBytes=0,checkedTexels=0;size_t opaqueCutouts=0;
    for(const auto& m:payload->materials)if(m.textureOwner==UINT32_MAX)storedBytes+=m.pixelBytes();
    const auto& originals=snapshot->bvh->scene().materials;
    std::array<uint8_t,4096*4> row;
    for(size_t i=0;i<originals.size();++i){
        const auto& src=originals[i];const auto& mask=payload->materials[i];auto rgba=src.texels();
        assert(src.alphaCutoff==mask.alphaCutoff);
        if(src.alphaCutoff>0&&w.uploadedAlphaCutoffs[i]==0){assert(mask.width==1&&mask.height==1);++opaqueCutouts;}
        else assert(src.alphaCutoff==w.uploadedAlphaCutoffs[i]);
        const auto stride=mask.pixelBytes()/(uint64_t(mask.width)*mask.height);assert(stride==1||stride==4);
        if(mask.width==1&&mask.height==1){
            assert(payload->pages->copyMaterial(mask,0,stride,row.data()));
            if(src.alphaCutoff<=0||rgba.empty()){assert(row[stride-1]==255);continue;}
            for(size_t p=3;p<rgba.size();p+=4)assert(row[stride-1]==rgba[p]);
        }else{
            assert(mask.width==src.width&&mask.height==src.height);
            for(size_t y=0;y<mask.height;++y){
                assert(payload->pages->copyMaterial(mask,y*mask.width*stride,mask.width*stride,row.data()));
                for(size_t x=0;x<mask.width;++x)assert(row[(x+1)*stride-1]==rgba[(y*mask.width+x)*4+3]);
            }
        }
        checkedTexels+=rgba.size()/4;
    }
    assert(checkedTexels);
    std::printf("WORKER_MASKS storedBytes=%llu checkedTexels=%llu slots=%zu gpuTextures=%zu opaqueCutouts=%zu\n",
        (unsigned long long)storedBytes,(unsigned long long)checkedTexels,payload->materials.size(),
        std::set<IDirect3DTexture9*>(w.materials.begin(),w.materials.end()).size(),opaqueCutouts);
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
