// Included inside WorldRenderer; geometry targets are independent of screen targets.
    unsigned geometryGroups=0;
    bool keepPressureProbes=false;bool geometryTargetsReady=false,geometryTargetsRetry=false,actorTargetsPending=false;DWORD geometryTargetsRetryAt=0,geometryTargetsReportAt=0;bool geometryTargetsReported=false;
    void releaseGeometryTargets(){
        releaseShadowTransitions();releaseNearEnvironment();
        for(auto& surface:shadowSurface)drop(surface);for(auto& texture:shadow)drop(texture);
        for(auto& surface:shadowCacheSurface)drop(surface);for(auto& texture:shadowCache)drop(texture);
        drop(shadowDepth);drop(shadowCacheDepth);drop(shadowScratchSurface);drop(shadowScratch);releaseActorTargetTextures();
        if(!keepPressureProbes){for(auto& texture:probe)drop(texture);drop(probePrev);}
        if(uploadedSerial){probeActivation.reset();probeBlend.reset();}uploadedSerial=0;
        drop(shadowVerifySurface);drop(shadowVerify);for(auto& read:shadowVerifyRead)drop(read);
        geometryTargetsReady=false;geometryGroups=0;invalidateShadowCache();
    }
    bool geometryResources(UINT w,UINT h,NorthlightStreaming::Budget* prewarm=nullptr){
        const auto needs=frameRequirements();const auto requiredGroups=needs.geometryGroups();
        if(geometryTargetsReady&&(geometryGroups&requiredGroups)==requiredGroups&&!actorTargetsPending)return true;
        const DWORD now=GetTickCount();if(geometryTargetsRetry&&DWORD(now-geometryTargetsRetryAt)<1000)return false;
        const bool hadGeometryTargets=geometryTargetsReady;
        if(actorTargetsPending){D3DCAPS9 caps={};d->GetDeviceCaps(&caps);actorShaderResources(caps);}
        const uint64_t n=NorthlightGI::probeLayout().atlas;
        HRESULT hr=S_OK;const char* stage="scene target";bool admissionRefused=false,exact=true,deferred=false;unsigned allocations=0;
        // Stage one allocation per loading frame under the same upload deadline.
        // Partial targets are private until the complete set is ready. A normal
        // render still finishes the set synchronously, never delaying geometry.
        const auto allocate=[&]{if(prewarm&&(!prewarm->available()||allocations++)){deferred=true;return false;}return true;};
        NorthlightGeometryMemory::Budget refusedBudget;NorthlightGeometryMemory::Sample refusedMemory;
        auto admit=[&](uint64_t bytes,bool defaultTarget=false){
            const auto budget=defaultTarget?NorthlightGeometryMemory::replayBudget(NorthlightGeometryMemory::defaultTargetBudget(),pressureReplayAllocation()):NorthlightGeometryMemory::dynamicBudget(bytes);bool sampleExact=true;
            const auto memory=geometryAdmission(budget,&sampleExact);
            if(NorthlightGeometryMemory::admits(memory,budget))return true;
            refusedBudget=budget;refusedMemory=memory;exact=sampleExact;admissionRefused=true;hr=E_OUTOFMEMORY;stage="scene target admission";return false;
        };
        auto required=[&](HRESULT result,const char* name){if(FAILED(result)){hr=result;stage=name;return false;}return true;};
        auto makeTarget=[&](UINT tw,UINT th,D3DFORMAT format,IDirect3DTexture9** texture,IDirect3DSurface9** surface){
            if(*texture&&*surface)return true;
            return allocate()&&admit(uint64_t(tw)*th*4,true)&&required(d->CreateTexture(tw,th,1,D3DUSAGE_RENDERTARGET,format,D3DPOOL_DEFAULT,texture,nullptr),"scene texture")&&
                   required((*texture)->GetSurfaceLevel(0,surface),"scene surface");
        };
        auto makeDepth=[&](UINT tw,UINT th,IDirect3DSurface9** surface,const char* name){
            if(*surface)return true;
            return allocate()&&admit(uint64_t(tw)*th*4,true)&&required(d->CreateDepthStencilSurface(tw,th,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,surface,nullptr),name);
        };
        if(keepPressureProbes){
            const auto compatible=[&](IDirect3DTexture9* texture,UINT tw,UINT th){D3DSURFACE_DESC desc{};return texture&&SUCCEEDED(texture->GetLevelDesc(0,&desc))&&desc.Width==tw&&desc.Height==th&&desc.Format==D3DFMT_A32B32G32R32F&&desc.Pool==D3DPOOL_MANAGED;};
            for(int i=0;i<5;++i)if(probe[i]&&!compatible(probe[i],UINT(n*n),UINT(i==3?n*6:n)))drop(probe[i]);
            if(probePrev&&!compatible(probePrev,UINT(n*n*3),UINT(n)))drop(probePrev);
        }
        auto make=[&](){
            if(needs.shadows){
            for(int i=0;i<4;++i)if(!makeTarget(1024,1024,D3DFMT_R32F,&shadow[i],&shadowSurface[i]))return false;
            if(!makeDepth(1024,1024,&shadowDepth,"shadow depth"))return false;
            for(int i=0;i<4;++i)if(!makeTarget(ShadowCacheSize,ShadowCacheSize,D3DFMT_R32F,&shadowCache[i],&shadowCacheSurface[i]))return false;
            if(!makeDepth(ShadowCacheSize,ShadowCacheSize,&shadowCacheDepth,"shadow cache depth")||!makeTarget(1024,1024,D3DFMT_R32F,&shadowScratch,&shadowScratchSurface))return false;
            if(actorShadowPS){
                const UINT count=NorthlightActorShadowMap::Size/NorthlightActorShadowMap::TileTexels;
                if(!makeTarget(NorthlightActorShadowMap::Size,NorthlightActorShadowMap::Size,D3DFMT_R32F,&actorShadow[0],&actorShadowSurface[0])||!makeTarget(NorthlightActorShadowMap::Size,NorthlightActorShadowMap::Size,D3DFMT_R32F,&actorShadow[1],&actorShadowSurface[1])
                   ||!makeTarget(NorthlightActorShadowMap::Size/NorthlightActorShadowMap::TileStep,NorthlightActorShadowMap::Size/NorthlightActorShadowMap::TileStep,D3DFMT_R32F,&actorTilesMid,&actorTilesMidSurface)||!makeTarget(count,count,D3DFMT_R32F,&actorTiles[0],&actorTilesSurface[0])||!makeTarget(count,count,D3DFMT_R32F,&actorTiles[1],&actorTilesSurface[1])||!makeTarget(w/2,h/2,D3DFMT_A8R8G8B8,&actorVisibility,&actorVisibilitySurface)){
                    if(deferred||hr==E_OUTOFMEMORY||hr==D3DERR_OUTOFVIDEOMEMORY)return false;
                    releaseActorTargets();logf("WORLD actor shadow map disabled: render targets unavailable");hr=S_OK;
                }
            }
            }
            if(!needs.probes)return true;
            for(int i=0;i<5;++i)if(!probe[i]&&(!allocate()||!admit(n*n*(i==3?n*6:n)*16)||!required(d->CreateTexture(UINT(n*n),UINT(i==3?n*6:n),1,0,D3DFMT_A32B32G32R32F,D3DPOOL_MANAGED,&probe[i],nullptr),"GI probe texture")))return false;
            if(probePrev)return true;
            if(!allocate()||!admit(n*n*3*n*16))return false;
            const HRESULT previous=d->CreateTexture(UINT(n*n*3),UINT(n),1,0,D3DFMT_A32B32G32R32F,D3DPOOL_MANAGED,&probePrev,nullptr);
            if(FAILED(previous)){drop(probePrev);if(previous==E_OUTOFMEMORY||previous==D3DERR_OUTOFVIDEOMEMORY){hr=previous;stage="GI probe blend texture";return false;}static bool logged=false;if(!logged){logged=true;logf("GI probe blend texture unavailable; same-key probe re-publication switches instantly");}}
            return true;
        };
        if(!make()){
            if(deferred)return false;
            releaseGeometryTargets();geometryTargetsRetry=true;geometryTargetsRetryAt=now;
            if(!geometryTargetsReported||DWORD(now-geometryTargetsReportAt)>=60000){geometryTargetsReported=true;geometryTargetsReportAt=now;if(admissionRefused&&NorthlightDiagnostics::enabled())logGeometryMemory("scene-targets-deferred",refusedMemory,0,exact,&refusedBudget);logf("WORLD scene targets deferred: stage=%s HRESULT=%08lx retryMs=1000",stage,(unsigned long)hr);}
            return false;
        }
        geometryGroups|=requiredGroups;geometryTargetsReady=true;geometryTargetsRetry=false;actorTargetsPending=false;keepPressureProbes=false;if(!hadGeometryTargets)invalidateShadowCache();return true;
    }
