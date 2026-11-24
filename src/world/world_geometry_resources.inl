// Included inside WorldRenderer; geometry targets are independent of screen targets.
    unsigned geometryGroups=0;
    bool keepPressureProbes=false;bool geometryTargetsReady=false,geometryTargetsRetry=false;DWORD geometryTargetsRetryAt=0,geometryTargetsReportAt=0;bool geometryTargetsReported=false;
    void releaseLiveTerrainBuffers(){
        liveTerrainGPU.clear();
        liveTerrainIndices.release(fence());
    }

    void releaseShadowMapsOnReset(){
        directionalMaps.releaseMaps();
        actorMaps.reset();
        screenFallbacks.releaseShadow();
        actorValid[0]=actorValid[1]=false;
        actorCenterValid[0]=actorCenterValid[1]=false;
        shadowPrograms.releaseUnion();
        invalidateShadowCache();
    }

    void releaseGeometryTargets(){
        releaseShadowTransitions();releaseNearEnvironment();
        directionalMaps.releaseMaps();
        actorMaps.releaseTextures();
        if(!keepPressureProbes)probeAtlas.releaseTextures();
        else probeAtlas.resetVisibility();
        if(uploadedSerial)probeAtlas.resetHistory();
        uploadedSerial=0;
        geometryTargetsReady=false;geometryGroups=0;invalidateShadowCache();
    }
    bool geometryResources(UINT w,UINT h,NorthlightStreaming::Budget* prewarm=nullptr){
        const auto needs=frameRequirements();const auto requiredGroups=needs.geometryGroups();
        if(geometryTargetsReady&&(geometryGroups&requiredGroups)==requiredGroups&&!actorMaps.pending())return true;
        const DWORD now=GetTickCount();if(geometryTargetsRetry&&DWORD(now-geometryTargetsRetryAt)<1000)return false;
        const bool hadGeometryTargets=geometryTargetsReady;
        if(actorMaps.pending()){D3DCAPS9 caps={};d->GetDeviceCaps(&caps);prepareActorShaders(caps);}
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
            return allocate()&&admit(uint64_t(tw)*th*4,true)&&
                   NorthlightWorldRenderTarget::ensure(d,tw,th,format,*texture,*surface,required,"scene texture","scene surface");
        };
        auto makeDepth=[&](UINT tw,UINT th,IDirect3DSurface9** surface,const char* name){
            if(*surface)return true;
            return allocate()&&admit(uint64_t(tw)*th*4,true)&&NorthlightWorldRenderTarget::ensureDepth(d,tw,th,*surface,required,name);
        };
        if(keepPressureProbes)probeAtlas.validateRetained();
        auto make=[&](){
            if(needs.shadows){
            if(!directionalMaps.create(makeTarget,makeDepth))return false;
            if(!actorMaps.createTargets(w,h,makeTarget)){
                if(deferred||hr==E_OUTOFMEMORY||hr==D3DERR_OUTOFVIDEOMEMORY)return false;
                actorMaps.dropResources();
                logf("WORLD actor shadow map disabled: render targets unavailable");
                hr=S_OK;
            }
            }
            return !needs.probes||probeAtlas.create(d,[&](uint64_t bytes){return allocate()&&admit(bytes);},required,
                static_cast<void(*)(const char*,...)>(&logf));
        };
        if(!make()){
            if(deferred)return false;
            releaseGeometryTargets();geometryTargetsRetry=true;geometryTargetsRetryAt=now;
            if(!geometryTargetsReported||DWORD(now-geometryTargetsReportAt)>=60000){geometryTargetsReported=true;geometryTargetsReportAt=now;if(admissionRefused&&NorthlightDiagnostics::enabled())logGeometryMemory("scene-targets-deferred",refusedMemory,0,exact,&refusedBudget);logf("WORLD scene targets deferred: stage=%s HRESULT=%08lx retryMs=1000",stage,(unsigned long)hr);}
            return false;
        }
        geometryGroups|=requiredGroups;geometryTargetsReady=true;geometryTargetsRetry=false;actorMaps.completeGeometry();keepPressureProbes=false;if(!hadGeometryTargets)invalidateShadowCache();return true;
    }
