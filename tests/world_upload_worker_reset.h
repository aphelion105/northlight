#pragma once
#include "capture_file_reader.h"
inline void WorldColdUploadTest::workerReset(UploadFaultDevice& d,unsigned target,D3DPRESENT_PARAMETERS& parameters,const std::string& cache,NorthlightGI::Vec3 center){
    DeviceLocal=true;Spooling=false;
    auto renderer=std::make_unique<WorldRenderer>(&d);auto& w=*renderer;w.valid=true;w.quality.actorShadows=w.quality.actorShadowDetail=1;w.lastRequest.map="Northrend";
    assert(w.resources(3840,2160,D3DFMT_A16B16G16R16F));auto* baseline=warmBackend(d);
    auto built=NorthlightScenePressureFixture::build(cache,{},center,{},"Northrend",false,true);
    auto snapshot=actualSnapshot(built);snapshot->center=center;complete(w,snapshot);drain(d);
    assert(!built.plan->payload.live());
    assert(w.geometryResources(3840,2160));
    {NorthlightStreaming::Budget budget(4u<<20,2);assert(w.upload(budget));}
    preloadProbes(w);drain(d);
    std::array<IDirect3DTexture9*,7> probes{};
    for(unsigned i=0;i<5;++i)probes[i]=w.probe[i];probes[5]=w.probePrev;probes[6]=w.probeLocal;
    for(auto* texture:probes){assert(texture);texture->AddRef();}
    const auto capture=(std::filesystem::path(rootPath)/L"reset-capture").u8string();
    std::filesystem::create_directories(std::filesystem::u8path(capture));assert(NorthlightDiagnosticFiles::enable(capture));
    assert(SUCCEEDED(d.Clear(0,nullptr,D3DCLEAR_TARGET,0xff183151,1,0)));
    {IDirect3DSurface9* targetSurface=nullptr;assert(SUCCEEDED(d.GetRenderTarget(0,&targetSurface)));
     NorthlightLightingCapture::Capture output(&d,capture,1,1,1,3840,2160);
     assert(output.imageSaved("before-reset",targetSurface));output.finish(true);targetSurface->Release();}
    {const auto pixels=captureFileBytes(capture+"/capture-1-before-reset.fgr");assert(pixels.size()==20+3840u*2160*4);
     for(size_t i=20;i<pixels.size();i+=4){uint32_t color=0;std::memcpy(&color,pixels.data()+i,4);assert((color&0xffffff)==0x183151);}}
    w.geometryMemoryDemand.store(512ull<<20);w.reclaimInactiveCasters();assert(!w.staticRecoveryPaused&&w.worldMeshDrawable());

    w.reset();assert(!w.worldMeshDrawable());w.reclaimInactiveCasters();assert(w.staticRecoveryPaused);
    w.requestStaticCasters("Northrend");assert(w.staticRecoveryPaused);
    assert(SUCCEEDED(d.Reset(&parameters)));w.valid=true;assert(w.resources(3840,2160,D3DFMT_A16B16G16R16F));
    // Another Reset may arrive after targets, before a first mesh page exists.
    assert(w.geometryResources(3840,2160));w.reset();assert(w.keepPressureProbes);
    assert(SUCCEEDED(d.Reset(&parameters)));w.valid=true;assert(w.resources(3840,2160,D3DFMT_A16B16G16R16F));
    AddressBallast pressure;pressure.fill(target);const auto before=pressure.available();const auto start=GetTickCount();
    {
        std::lock_guard<std::mutex> lock(w.mutex);w.published=snapshot;
        auto& r=w.request;r.enabled=true;r.giEnabled=false;r.fogEnabled=false;r.map="Northrend";
        r.camera=r.probeCenter=r.geometryCenter=center;r.queuedAt=GetTickCount();++r.id;
    }
    {NorthlightStreaming::Budget budget(4u<<20,2);w.upload(budget);}
    std::shared_ptr<WorldRenderer::Snapshot> recovered;
    while(DWORD(GetTickCount()-start)<60000){
        assert(!w.workerFaultCode.load());
        {std::lock_guard<std::mutex> lock(w.mutex);if(w.published&&w.published->meshPlan!=snapshot->meshPlan&&w.published->bvh==snapshot->bvh)recovered=w.published;}
        if(recovered)break;assert(SUCCEEDED(d.Present(nullptr,nullptr,nullptr,nullptr)));Sleep(10);
    }
    assert(recovered&&recovered->meshPlan->payload.live()&&recovered->bvh==built.bvh);
    // The game reached target admission with 273 MiB free. The old stand
    // never created the initial GI targets and only tested 307 MiB here.
    pressure.fill(273);
    const auto targetEntry=pressure.available();
    Spooling=true;complete(w,recovered);drain(d);
    assert(w.geometryResources(3840,2160)&&w.actorVisibility);
    for(unsigned i=0;i<5;++i)assert(w.probe[i]==probes[i]);
    assert(w.probePrev==probes[5]&&w.probeLocal==probes[6]);const auto after=pressure.available();
    assert(after>=NorthlightGeometryMemory::ProcessReserve);
    const auto wall=DWORD(GetTickCount()-start);assert(wall<5000&&"Recovery must not wait for driver allocator garbage collection");
    w.geometryMemoryDemand.store(0);w.requestStaticCasters("Northrend");assert(!w.staticRecoveryPaused&&w.worldMeshDrawable());
    for(auto* block:pressure.blocks)assert(VirtualFree(block,0,MEM_RELEASE));pressure.blocks.clear();
    verify(w,0,false,recovered->meshPlan->payload.share().get());
    w.reset();assert(SUCCEEDED(d.Reset(&parameters)));w.valid=true;assert(w.resources(3840,2160,D3DFMT_A16B16G16R16F));complete(w,recovered);
    std::printf("ACTUAL_WORKER_RESET_PASS entryMiB=%.3f afterMiB=%.3f restoreMs=%lu sameBvh=1 newUploadPlan=1 exactRaster=1 repeatedReset=1 retainedProbes=7 capture4kMsaa8=1 interruptedReset=1 staticReclaim=1 targetEntryMiB=%.3f\n",before/1048576.,after/1048576.,(unsigned long)wall,targetEntry/1048576.);
    for(auto* texture:probes)texture->Release();baseline->Release();
}
