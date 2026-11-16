#pragma once
inline void WorldColdUploadTest::deviceLocalActual(UploadFaultDevice& d,unsigned target,D3DPRESENT_PARAMETERS& parameters,const std::string& cache,NorthlightGI::Vec3 center,bool compare){
    Spooling=false;
    for(bool local:{false,true}){
        if(!local&&!compare)continue;
        DeviceLocal=local;auto renderer=std::make_unique<WorldRenderer>(&d);auto& w=*renderer;w.valid=true;
        assert(w.resources(3840,2160,D3DFMT_A16B16G16R16F));auto* baseline=warmBackend(d);
        auto make=[&]{return NorthlightScenePressureFixture::build(cache,{},center,{},"Northrend",false,local);};
        auto built=make();auto snapshot=actualSnapshot(built);
        assert(built.plan->payload.share()->pages->deviceLocal==local&&!built.plan->payload.share()->pages->temporary);
        AddressBallast pressure;
        while(pressure.available()>((uint64_t(target)+16)<<20)){auto* block=VirtualAlloc(nullptr,16u<<20,MEM_RESERVE,PAGE_NOACCESS);assert(block);pressure.blocks.push_back(block);}
        const auto before=pressure.available();const auto started=GetTickCount();complete(w,snapshot);drain(d);
        const auto after=pressure.available();const auto uploadMs=DWORD(GetTickCount()-started);
        assert(!built.plan->payload.live()&&w.materials.size()>100&&!w.meshStore.table().empty());
        uint64_t gpuBytes=0;for(auto id:w.meshStore.table()){const auto& page=w.meshBuffers[id];assert(page.vbPool==(local?D3DPOOL_DEFAULT:D3DPOOL_MANAGED)&&page.ibPool==page.vbPool);gpuBytes+=w.meshStore.entries()[id].capacity();}
        std::printf("DEVICE_LOCAL_MEASURE defaultPool=%u fileStaging=0 triangles=%u beforeMiB=%.3f afterMiB=%.3f addressDeltaMiB=%.3f gpuGeometryMiB=%.3f uniqueTextureMiB=%.3f uploadWallMs=%lu\n",unsigned(local),built.plan->triangleCount,before/1048576.,after/1048576.,double(int64_t(before)-int64_t(after))/1048576,gpuBytes/1048576.,built.plan->textureBytes/1048576.,(unsigned long)uploadMs);
        // Rebuild an independent byte/raster oracle after measuring production
        // residency, so the oracle does not keep the released upload bytes alive.
        for(auto* block:pressure.blocks)assert(VirtualFree(block,0,MEM_RELEASE));pressure.blocks.clear();
        auto oracle=make();auto expected=oracle.plan->payload.share();verify(w,0,false,expected.get());
        assert(w.residentPages&&w.residentPages->size()==w.meshStore.table().size());
        // Identical content must reuse DEFAULT buffers as well as MANAGED ones.
        auto repeated=make();auto repeatedPages=std::const_pointer_cast<NorthlightWorldMeshPages::Plan>(repeated.plan->payload.share()->pages);
        const auto elided=NorthlightWorldMeshPages::elide(*repeatedPages,*w.residentPages);assert(elided.pages==repeatedPages->pages.size());
        const auto previousTable=w.meshStore.table();snapshot=actualSnapshot(repeated,2);complete(w,snapshot);assert(w.meshStore.table()==previousTable);
        w.reset();if(local)assert(!w.vertices&&!w.resetReplayScoped);
        assert(SUCCEEDED(d.Reset(&parameters)));w.valid=true;assert(w.resources(3840,2160,D3DFMT_A16B16G16R16F));
        if(local){
            const auto rebuilds=w.payloadRebuilds;NorthlightStreaming::Budget budget(4u<<20,2);w.upload(budget);
            assert(w.payloadRebuilds==rebuilds+1&&w.geometryRebuild);
            snapshot=actualSnapshot(oracle,2);
        }
        complete(w,snapshot);verify(w,0,false,expected.get());
        std::printf("DEVICE_LOCAL_ACTUAL_PASS defaultPool=%u fileStaging=0 exactRaster=1 cpuPayloadReleased=1 residentReuse=1 resetRebuild=%u\n",unsigned(local),unsigned(local));
        baseline->Release();
    }
}
