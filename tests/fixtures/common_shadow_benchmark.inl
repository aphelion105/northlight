// Additional members of the existing production-renderer fixture. This file is
// inserted by the experiment runner; no game installation or runtime flag.
struct CommonDrawMeter:ForwardIDirect3DDevice9 {
    unsigned draws=0,mapDraws=0,filterDraws=0,staticRefreshes=0;
    IDirect3DSurface9* current=nullptr;
    std::vector<IDirect3DSurface9*> maps,filters,caches;
    explicit CommonDrawMeter(IDirect3DDevice9* d):ForwardIDirect3DDevice9(d){}
    bool contains(const std::vector<IDirect3DSurface9*>& list){return current&&std::find(list.begin(),list.end(),current)!=list.end();}
    void count(){++draws;mapDraws+=contains(maps);filterDraws+=contains(filters);}
    HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD slot,IDirect3DSurface9* surface)override{if(!slot)current=surface;return real->SetRenderTarget(slot,surface);}
    HRESULT STDMETHODCALLTYPE Clear(DWORD n,const D3DRECT* r,DWORD f,D3DCOLOR c,float z,DWORD s)override{staticRefreshes+=bool(f&D3DCLEAR_TARGET)&&contains(caches);return real->Clear(n,r,f,c,z,s);}
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE t,INT b,UINT m,UINT n,UINT s,UINT c)override{count();return real->DrawIndexedPrimitive(t,b,m,n,s,c);}
    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE t,UINT s,UINT c)override{count();return real->DrawPrimitive(t,s,c);}
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE t,UINT c,const void* v,UINT s)override{count();return real->DrawPrimitiveUP(t,c,v,s);}
};
static void commonGeometry(WorldRenderer& w){
    using Vertex=NorthlightGI::WorldVertex;
    std::vector<Vertex> vertices;std::vector<WORD> indices;w.batches.clear();
    auto face=[&](float x,float y,float width,float height,float z,bool terrain,bool cutout){
        WorldRenderer::Batch b{};b.page=0;b.minVertex=unsigned(vertices.size());b.vertexCount=4;b.start=unsigned(indices.size());b.count=2;
        b.material=cutout?1:0;b.terrain=terrain;b.chunkX=7;b.chunkY=9;b.boundsLow={x,y,z};b.boundsHigh={x+width,y+height,z};
        const WORD base=WORD(vertices.size());
        for(auto v:std::array<Vertex,4>{{{{x,y,z},{0,0,1},0,0},{{x+width,y,z},{0,0,1},1,0},{{x,y+height,z},{0,0,1},0,1},{{x+width,y+height,z},{0,0,1},1,1}}})vertices.push_back(v);
        for(WORD index:{WORD(0),WORD(1),WORD(2),WORD(2),WORD(1),WORD(3)})indices.push_back(base+index);
        w.batches.push_back(b);
    };
    face(-48,-48,96,96,0,true,false);
    for(unsigned row=0;row<8;++row)for(unsigned col=0;col<16;++col){
        const float x=-40+col*5.f,y=-20+row*5.f;
        face(x,y,col%3==0?.065f:2.75f,3.7f,4+float((row+col)%5),false,col%4==1);
    }
    IDirect3DTexture9* alpha=nullptr;must(w.d->CreateTexture(16,16,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&alpha,nullptr));
    D3DLOCKED_RECT lock{};must(alpha->LockRect(0,&lock,nullptr,0));
    for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x)reinterpret_cast<DWORD*>(static_cast<char*>(lock.pBits)+y*lock.Pitch)[x]=((x/2+y/2)%2)?0xffffffff:0x00ffffff;
    must(alpha->UnlockRect(0));w.materials.push_back(alpha);w.uploadedAlphaCutoffs.push_back(.5f);
    auto& page=w.meshBuffers[w.meshStore.bufferOf(0)];drop(page.vb);drop(page.ib);
    must(w.d->CreateVertexBuffer(UINT(vertices.size()*sizeof(Vertex)),0,0,D3DPOOL_MANAGED,&page.vb,nullptr));
    must(w.d->CreateIndexBuffer(UINT(indices.size()*sizeof(WORD)),0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&page.ib,nullptr));void* bytes=nullptr;
    must(page.vb->Lock(0,0,&bytes,0));std::memcpy(bytes,vertices.data(),vertices.size()*sizeof(Vertex));must(page.vb->Unlock());
    must(page.ib->Lock(0,0,&bytes,0));std::memcpy(bytes,indices.data(),indices.size()*sizeof(WORD));must(page.ib->Unlock());
    w.vertices=page.vb;w.indices=page.ib;w.rebuildTerrainLists();++w.meshGeneration;w.invalidateShadowCache();
}
static void commonActors(WorldRenderer& w){
    std::vector<std::unique_ptr<WorldRenderer::Replay>> actors;
    for(unsigned i=0;i<48;++i){
        pose(w,0);auto p=std::move(w.replays[0]);w.replays.clear();
        p->constantStorage[4*31]=.25f;p->constantStorage[4*32+1]=.4f;
        p->constantStorage[4*33+3]=20;
        p->captureRootValid=true;
        if(i%4==1){p->texture=w.materials[1];p->texture->AddRef();p->cutoff=.5f;}
        actors.push_back(std::move(p));
    }
    w.replays=std::move(actors);
}
static void commonFrame(WorldRenderer& w,unsigned scenario,unsigned frame){
    const float travel=scenario==2?float(frame)*.08f:0;
    for(unsigned i=0;i<16;++i)w.context.view[i]=w.context.inverseView[i]=i%5==0?1.f:0.f;
    w.context.camera[0]=travel;w.context.camera[1]=0;w.context.camera[2]=30;
    w.context.inverseView[12]=travel;w.context.view[12]=-travel;
    w.context.inverseView[14]=w.context.view[14]=30;w.context.view[10]=w.context.inverseView[10]=-1;
    w.pivotDistance=30;w.staticPivotReady=false;w.valid=true;w.pipelineActive=true;
    w.projection[0]=w.projection[1]=w.projection[2]=1;
    const auto direction=NorthlightGI::normalized(NorthlightGI::Vec3(.15f,.2f,1));
    w.sourceDirections[0]=direction;
    w.rawSourceDirections[0]=NorthlightGI::normalized(NorthlightGI::Vec3(.15f+(scenario==2?frame*.000001f:0.f),.2f,1));
    w.sourceColors[0]=w.sourceGlowColors[0]={.6f,.5f,.4f};w.sourceWeights[0]=1;
    w.sourceColors[1]=w.sourceGlowColors[1]={};w.sourceWeights[1]=0;
    for(unsigned axis=0;axis<3;++axis){w.context.direct[axis]=.6f;w.context.ambient[axis]=.25f;w.context.lightDirection[axis]=(&direction.x)[axis];}
    for(unsigned i=0;i<w.replays.size();++i){auto& p=*w.replays[i];
        p.constantGroup=i+1; // Distinct captured bone banks, as in real capture.
        const float x=float(int(i%8)-4)*3+(scenario?std::sin(frame*.07f+i*.3f)*.5f:0),y=float(int(i/8)-3)*3;
        p.constantStorage[4*31+3]=x-travel;p.constantStorage[4*32+3]=y;
        p.captureRoot[0]=x;p.captureRoot[1]=y;p.captureRoot[2]=8;
    }
    w.captureMode=WorldRenderer::CaptureFresh;w.shadowSelectionDone=true;
    if(scenario==3&&frame%8==0){++w.meshGeneration;w.invalidateShadowCache();}
}
static void commonSaveVisibility(WorldRenderer& w,const char* folder,const char* name){
    D3DSURFACE_DESC desc{};must(w.softVisibilitySurface->GetDesc(&desc));assert(desc.Format==D3DFMT_A16B16G16R16F);
    IDirect3DSurface9* read=nullptr;must(w.d->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&read,nullptr));
    must(w.d->GetRenderTargetData(w.softVisibilitySurface,read));D3DLOCKED_RECT data{};must(read->LockRect(&data,nullptr,D3DLOCK_READONLY));
    std::ofstream file(std::string(folder)+"/"+name,std::ios::binary);file<<"P5\n"<<desc.Width<<" "<<desc.Height<<"\n255\n";
    for(unsigned y=0;y<desc.Height;++y)for(unsigned x=0;x<desc.Width;++x){
        uint16_t bits;std::memcpy(&bits,static_cast<char*>(data.pBits)+y*data.Pitch+x*8,2);
        const unsigned e=(bits>>10)&31,m=bits&1023;const float value=e?std::ldexp(float(1024+m),int(e)-25):std::ldexp(float(m),-24);
        file.put(char(unsigned(std::clamp(value,0.f,1.f)*255+.5f)));
    }
    must(read->UnlockRect());read->Release();
}
static void commonBenchmark(IDirect3DDevice9* device,const char* folder){
    constexpr unsigned W=1920,H=1080,N=32;
    CommonDrawMeter meter(device);auto world=std::make_unique<WorldRenderer>(&meter);auto& w=*world;
    setup(w,1,1);w.nativeShadowOwned=w.nativeShadowAvailable=true;
    w.quality.worldShadows=w.quality.actorShadows=w.quality.actorShadowDetail=w.quality.softShadows=1;
    w.quality.gi=w.quality.localLights=w.quality.pointShadows=w.quality.weather=w.quality.autoBump=w.quality.contactAO=0;
    w.effects.gi=w.effects.fog=false;w.effects.shadows=true;
    commonGeometry(w);commonActors(w);assert(w.commonExperiment.init(device));
    IDirect3DTexture9 *depth=nullptr,*scene=nullptr;IDirect3DSurface9* target=nullptr;
    must(device->CreateTexture(W,H,1,0,D3DFMT_R32F,D3DPOOL_MANAGED,&depth,nullptr));
    must(device->CreateTexture(W,H,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&scene,nullptr));
    D3DLOCKED_RECT lock{};must(depth->LockRect(0,&lock,nullptr,0));
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)reinterpret_cast<float*>(static_cast<char*>(lock.pBits)+y*lock.Pitch)[x]=1000.f/999.f*(1-1.f/30);
    must(depth->UnlockRect(0));must(scene->LockRect(0,&lock,nullptr,0));
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)reinterpret_cast<DWORD*>(static_cast<char*>(lock.pBits)+y*lock.Pitch)[x]=0xff808080;
    must(scene->UnlockRect(0));must(device->CreateRenderTarget(W,H,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
    IDirect3DQuery9 *event=nullptr,*frequency=nullptr;must(device->CreateQuery(D3DQUERYTYPE_EVENT,&event));must(device->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));
    auto wait=[&]{must(event->Issue(D3DISSUE_END));HRESULT hr;while((hr=event->GetData(nullptr,0,D3DGETDATA_FLUSH))==S_FALSE)SwitchToThread();must(hr);};
    auto draw=[&](unsigned scenario,unsigned frame){commonFrame(w,scenario,frame);must(device->BeginScene());
        const bool ok=w.render(target,depth,W,H,D3DFMT_A8R8G8B8,1,1000,0,1,0,nullptr,nullptr,scene,nullptr,frame);
        must(device->EndScene());if(!ok){std::fprintf(stderr,"COMMON_RENDER_FAIL mode=%u scenario=%u frame=%u reason=%s\n",unsigned(w.commonExperiment.enabled),scenario,frame,w.skipReason);std::abort();}
    };
    // Warm both routes; ensure the new fine map contains real world geometry
    // and never loses a pixel covered by the old detailed actor map.
    for(unsigned frame=0;frame<8;++frame)draw(0,0);wait();
    const auto actors=image(device,w.actorShadowSurface[0]);const auto nearBefore=image(device,w.shadowSurface[0]);
    assert(w.actorCasterCount[0]>0);
    commonSaveVisibility(w,folder,"baseline-visibility.pgm");
    w.commonExperiment.enabled=true;draw(0,0);wait();const auto common=image(device,w.actorShadowSurface[0]);
    unsigned actorPixels=0,worldPixels=0;for(size_t i=0;i<actors.size();++i){
        if(actors[i]<.999f){++actorPixels;assert(std::fabs(common[i])<=actors[i]+2e-6f);}
        else worldPixels+=std::fabs(common[i])<.999f;
    }
    assert(actorPixels&&worldPixels&&nearBefore==image(device,w.shadowSurface[0]));
    commonSaveVisibility(w,folder,"common-visibility.pgm");
    draw(0,0);wait();assert(common==image(device,w.actorShadowSurface[0]));
    w.commonExperiment.valid[0]=false;draw(0,0);wait();assert(common==image(device,w.actorShadowSurface[0]));
    std::printf("COMMON_MAP_COVERAGE actorPixels=%u additionalWorldPixels=%u unchangedCoarseMap=1\n",actorPixels,worldPixels);std::fflush(stdout);
    unsigned coverageCases=0;
    for(unsigned scenario=0;scenario<4;++scenario)for(unsigned frame:{0u,15u,31u}){
        w.commonExperiment.enabled=false;draw(scenario,frame);wait();const auto old=image(device,w.actorShadowSurface[0]);
        w.commonExperiment.enabled=true;draw(scenario,frame);wait();const auto now=image(device,w.actorShadowSurface[0]);
        for(size_t i=0;i<old.size();++i){assert(std::isfinite(now[i])&&std::fabs(now[i])<=1);
            if(old[i]<.999f)assert(std::fabs(now[i])<=old[i]+2e-6f);}
        ++coverageCases;
    }
    std::printf("COMMON_MOTION_COVERAGE cases=%u cachedVsRedraw=exact actorCoverage=retained\n",coverageCases);
    NorthlightGpuFrameTimer timer(device);unsigned measured=0;
    for(unsigned scenario=0;scenario<4;++scenario)for(unsigned block=0;block<16;++block){
        const bool commonMode=block%4==1||block%4==2;w.commonExperiment.enabled=commonMode;w.commonExperiment.timing=false;
        w.invalidateShadowCache();w.resetShadowTransitions();w.actorCenterValid[0]=false;w.cascadeAnchor.valid=false;
        for(unsigned warm=0;warm<4;++warm)draw(scenario,0);wait();
        w.commonExperiment.worldDraws=w.commonExperiment.boundaryDraws=w.commonExperiment.staticRenders=w.commonExperiment.staticReuses=0;meter.draws=meter.mapDraws=meter.filterDraws=meter.staticRefreshes=0;
        meter.maps.clear();meter.filters.clear();meter.caches.clear();
        for(auto* surface:w.shadowSurface)meter.maps.push_back(surface);
        for(auto* surface:w.shadowCacheSurface){meter.maps.push_back(surface);meter.caches.push_back(surface);}
        for(auto* surface:w.actorShadowSurface)meter.maps.push_back(surface);
        for(auto* surface:w.commonExperiment.staticSurface)meter.maps.push_back(surface);
        meter.maps.push_back(w.shadowScratchSurface);
        meter.filters={w.actorTilesMidSurface,w.actorTilesSurface[0],w.actorTilesSurface[1],w.softVisibilitySurface,
                       w.commonExperiment.blockSurface[0],w.commonExperiment.blockSurface[1],w.commonExperiment.tileSurface[0],w.commonExperiment.tileSurface[1]};
        const auto beforeActors=w.actorDrawn;
        must(device->BeginScene());assert(timer.begin(measured++));must(device->EndScene());
        const auto started=std::chrono::steady_clock::now();w.commonExperiment.timing=true;
        for(unsigned frame=0;frame<N;++frame){w.commonExperiment.sample=frame;draw(scenario,frame);}
        w.commonExperiment.timing=false;
        const double fullCPU=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()/N;
        must(device->BeginScene());timer.end();must(frequency->Issue(D3DISSUE_END));must(device->EndScene());wait();
        double fullGPU=-1;assert(timer.poll(fullGPU));UINT64 hz=0;must(frequency->GetData(&hz,sizeof hz,0));assert(hz);
        double mapsGPU=0,filterGPU=0,mapsCPU=0,filterCPU=0;
        for(unsigned frame=0;frame<N;++frame){UINT64 tick[4];for(unsigned stage=0;stage<4;++stage)must(w.commonExperiment.stamps[frame][stage]->GetData(&tick[stage],sizeof(UINT64),0));
            assert(tick[1]>=tick[0]&&tick[3]>=tick[2]);mapsGPU+=double(tick[1]-tick[0])*1000/hz;filterGPU+=double(tick[3]-tick[2])*1000/hz;
            mapsCPU+=std::chrono::duration<double,std::milli>(w.commonExperiment.cpu[frame][1]-w.commonExperiment.cpu[frame][0]).count();
            filterCPU+=std::chrono::duration<double,std::milli>(w.commonExperiment.cpu[frame][3]-w.commonExperiment.cpu[frame][2]).count();
        }
        std::printf("PIPELINE scenario=%u block=%u warm=%u common=%u frames=%u shadowGpuMs=%.6f mapsGpuMs=%.6f filterGpuMs=%.6f shadowCpuMs=%.6f mapsCpuMs=%.6f filterCpuMs=%.6f fullGpuMs=%.6f fullCpuMs=%.6f draws=%u mapDraws=%u filterDraws=%u staticRefreshes=%u selectedActorDraws=%u commonWorldDraws=%u commonBoundaryDraws=%u commonStaticRenders=%u commonStaticReuses=%u\n",
            scenario,block,unsigned(block<4),unsigned(commonMode),N,(mapsGPU+filterGPU)/N,mapsGPU/N,filterGPU/N,(mapsCPU+filterCPU)/N,mapsCPU/N,filterCPU/N,fullGPU/N,fullCPU,meter.draws,meter.mapDraws,meter.filterDraws,meter.staticRefreshes,unsigned(w.actorDrawn-beforeActors),w.commonExperiment.worldDraws,w.commonExperiment.boundaryDraws,w.commonExperiment.staticRenders,w.commonExperiment.staticReuses);
        std::fflush(stdout);
    }
    for(unsigned s=0;s<16;++s)device->SetTexture(s,nullptr);world.reset();drop(event);drop(frequency);drop(depth);drop(scene);drop(target);
    std::puts("COMMON_PIPELINE_PASS scenarios=4 productionWorldRenderer=1 totalMapsAndFilters=1 gameplayFPS=0");
}
