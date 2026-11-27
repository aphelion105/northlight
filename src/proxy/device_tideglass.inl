#ifdef NORTHLIGHT_WATER
    IDirect3DTexture9* tideglassMask=nullptr;
    IDirect3DSurface9* tideglassMaskSurface=nullptr;
    IDirect3DPixelShader9 *tideglassMaskPS=nullptr,*tideglassRainPS=nullptr;
#ifdef _WIN64
    bool tideglassSurfaceLogged=false;
    unsigned tideglassSpecialLogged=0;
    unsigned directTideglassFrame=~0u;
    NorthlightTideglassX64::Inputs directTideglass;
    NorthlightTideglassX64::Wakes tideglassWakes;
    const NorthlightTideglassX64::Inputs& tideglassInputs(){
        if(streamStats){
            static const NorthlightTideglassX64::Inputs empty;
            const auto* snapshot=NorthlightStream::activeSnapshot;
            return snapshot&&snapshot->water64?*snapshot->water64:empty;
        }
        if(directTideglassFrame!=frame){
            directTideglass={};captureTideglass64(directTideglass);directTideglassFrame=frame;
        }
        return directTideglass;
    }
    bool tideglassSubmerged(){
        if(!cooperativeWaterEnabled)return false;
        const auto camera=tideglassInputs().camera;
        return camera==WaterHostLiquidWater||camera==WaterHostLiquidSlime;
    }
    void reportTideglassFeature(unsigned bit,const char* feature){
        if(tideglassSpecialLogged&bit)return;
        tideglassSpecialLogged|=bit;
        logf("TIDEGLASS x64 %s frame=%u hdr=%u stream=%u",feature,frame,unsigned(hdr.active),unsigned(streamStats!=nullptr));
    }
    void reportTideglassSurface(){
        if(tideglassSurfaceLogged)return;
        WaterHostSurfaceStatus status{};
        if(cooperativeWater.SurfaceStatus(status)&&status.outcome==WaterHostSurfaceShaded){
            tideglassSurfaceLogged=true;
            logf("TIDEGLASS x64 water surface shaded frame=%u hdr=%u stream=%u",frame,unsigned(hdr.active),unsigned(streamStats!=nullptr));
        }
    }
#endif

    void supplyTideglassEnvironment(){
        bool wantsLamps=false;
        if(!world||!cooperativeWater.WantsEnvironment(wantsLamps))return;
        WaterEnvironmentValues values{};
        WaterLampSamples lamps{};
        if(world->tideglassEnvironment(values,lamps,wantsLamps,farZ))
            cooperativeWater.Environment(values,nullptr,0.f,lamps);
    }

    bool legacyTideglassTarget(){
        IDirect3DSurface9* target=nullptr;
        if(ext->peekRenderTarget(0,target))return target==hdrLegacyBack;
        const bool legacy=SUCCEEDED(ext->GetRenderTarget(0,&target))&&target==hdrLegacyBack;
        drop(target);
        return legacy;
    }
    void releaseTideglassTargets(){
        drop(tideglassMaskSurface);drop(tideglassMask);drop(tideglassMaskPS);drop(tideglassRainPS);
#ifdef _WIN64
        directTideglassFrame=~0u;
        tideglassWakes.reset();
#endif
    }
    IDirect3DTexture9* tideglassCoverage(IDirect3DTexture9* mask){
        D3DSURFACE_DESC desc{};
        if(!mask||FAILED(mask->GetLevelDesc(0,&desc)))return nullptr;
        if(desc.Format==D3DFMT_G32R32F)return mask;
        if(desc.Format!=D3DFMT_R32F)return nullptr;
        D3DSURFACE_DESC existing{};
        if(tideglassMask&&(FAILED(tideglassMask->GetLevelDesc(0,&existing))||
           desc.Width!=existing.Width||desc.Height!=existing.Height)){
            drop(tideglassMaskSurface);drop(tideglassMask);
        }
        if(!tideglassMask){
            if(FAILED(ext->CreateTexture(desc.Width,desc.Height,1,D3DUSAGE_RENDERTARGET,D3DFMT_G32R32F,
                                        D3DPOOL_DEFAULT,&tideglassMask,nullptr)))return nullptr;
            if(FAILED(tideglassMask->GetSurfaceLevel(0,&tideglassMaskSurface))){
                drop(tideglassMask);return nullptr;
            }
        }
        if(!tideglassMaskPS&&FAILED(ext->CreatePixelShader(kTideglassCoverageShader,&tideglassMaskPS)))return nullptr;
        SavedState state(ext,&stateBlocks);
        if(!state.ok)return nullptr;
        effectState();
        const bool drew=SUCCEEDED(ext->SetRenderTarget(0,tideglassMaskSurface))&&
            SUCCEEDED(ext->SetTexture(0,mask))&&SUCCEEDED(ext->SetPixelShader(tideglassMaskPS))&&
            SUCCEEDED(quad(desc.Width,desc.Height));
        return state.restore()&&drew?tideglassMask:nullptr;
    }
    bool snapshotTideglassRain(IDirect3DSurface9* target,IDirect3DTexture9* coverage){
        return coverage&&(tideglassRainPS||SUCCEEDED(ext->CreatePixelShader(kTideglassRetainRainShader,&tideglassRainPS)))&&
            SUCCEEDED(ext->StretchRect(target,nullptr,sceneSurface,nullptr,D3DTEXF_NONE));
    }
    void restoreTideglassRain(IDirect3DSurface9* target,IDirect3DTexture9* coverage){
        SavedState state(ext,&stateBlocks);
        if(!state.ok)return;
        effectState();
        if(FAILED(ext->SetRenderTarget(0,target))||
           FAILED(ext->SetTexture(0,scene))||FAILED(ext->SetTexture(1,coverage))||
           FAILED(ext->SetPixelShader(tideglassRainPS))||
           FAILED(ext->SetRenderState(D3DRS_COLORWRITEENABLE,7))||
           FAILED(ext->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE))||
           FAILED(ext->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA))||
           FAILED(ext->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA))||
           FAILED(ext->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD)))return;
        quad(width,height);
    }
    void prepareCooperativeWater(const D3DSURFACE_DESC& desc,const D3DVIEWPORT9& viewport,bool liquid=true) {
        if((hdr.active&&!legacyTideglassTarget())||!projectionValid||!world||menuScene||
           debugMode||worldDebug||memoryGuard.capsHalved())return;
        if(!cooperativeWater.Attempted()){
#ifdef _WIN64
            // The renderer's world context comes from this frame's validated draw and camera.
            // In CommandStream mode this uses the context already built from the producer snapshot.
            if(fogForward<=0.f)return;
            WaterHostWorldFrame inputsFrame{};
            if(!world->tideglassFrame(inputsFrame))return;
            auto& values=inputsFrame.values;
            values.viewport=viewport;
            values.viewport.MinZ=worldMinDepth;
            values.viewport.MaxZ=worldMaxDepth;
            // Tideglass expects [-1,1] clip Z. D3D9 receives the same projection remapped to [0,1].
            const float extent=farZ-nearZ;
            values.glProjection[0]=scaleX;values.glProjection[5]=scaleY;
            values.glProjection[10]=1.f+2.f*nearZ/extent;
            values.glProjection[11]=1.f;
            values.glProjection[14]=-2.f*nearZ*(farZ/extent);
            const auto& inputs=tideglassInputs();
            values.cameraInWater=inputs.camera==WaterHostLiquidWater;
            values.cameraInSlime=inputs.camera==WaterHostLiquidSlime;
            if(values.cameraInWater||values.cameraInSlime){
                values.cameraWaterHeightKnown=1;
                values.cameraWaterHeight=values.cameraPosition[2]+inputs.depth;
            }
            tideglassWakes.sample(inputs,GetTickCount(),values);
            const auto waterFrame=northlightWaterSettings.Module().Copy(inputsFrame);
#else
            const auto* snapshot=NorthlightStream::activeSnapshot;
            const auto waterFrame=SelectNorthlightWaterFrame(snapshot?&snapshot->waterFrame:nullptr,streamStats!=nullptr,
                                                       [&]{return captureNativeWater(viewport);});
#endif
            ExtensionDevice::RawScope raw(*ext);
            if(!cooperativeWater.Begin(ext,waterFrame))return;
#ifdef _WIN64
            if(values.boatCount)reportTideglassFeature(1,"wake tracks accepted");
#endif
        }
        if(!liquid||!cooperativeWater.Current()||cooperativeWater.Liquid())return;
        rainMrtUnbind();
        ExtensionDevice::RawScope raw(*ext);
        SavedState saved(ext,&stateBlocks);
        if(saved.ok&&resources(desc.Width,desc.Height,desc.Format)){
            captured=false; // This snapshot belongs to the pre-liquid scene.
            if(resolveDepthInto(depthTex,false))cooperativeWater.BeforeLiquid(depthTex,worldDepth);
        }
    }
    WaterHostLiquidKind cooperativeSurfaceDraw=WaterHostLiquidUnknown;
    const NorthlightAutoDisplacement::Args* cooperativeDrawArgs=nullptr;
    bool cooperativeLiquidBounds(){
        if(!cooperativeDrawArgs)return false;
        const auto& args=*cooperativeDrawArgs;
        const auto& draw=args.draw;
        const UINT count=LiquidDrawVertices(draw.topology,draw.primitives);
        const LiquidDraw liquidDraw{draw.base,draw.indexed?draw.minimum:draw.start,
            draw.indexed?draw.vertices:count,args.vertices,args.stride,draw.start,
            draw.indexed?count:0,args.indices,args.format};
        return LiquidDrawBounds<NorthlightReplayCopies::Reader>(ext,liquidDraw,liquidBounds);
    }
    float liquidBounds[6]{};
    bool liquidBoundsKnown=false;
    int liquidRamp=-1;
#ifdef _WIN64
    bool tideglassMagmaShader(){
        IDirect3DPixelShader9* pixel=nullptr;
        if(!ext->peekPixelShader(pixel)||!pixel)return false;
        const auto hash=psHashes.find(pixel);
        // Stock psLiquidMagma variants from the client archive. The shader is
        // shared by magma and slime. The native material bank decides which.
        return hash!=psHashes.end()&&
            (hash->second==UINT64_C(0x5ae01f3dfba83ace)||hash->second==UINT64_C(0x2965e7011e79b205));
    }
#endif
    WaterHostLiquidKind cooperativeLiquidKind(const D3DSURFACE_DESC& desc,const D3DVIEWPORT9& viewport){
#ifdef _WIN64
        const auto& inputs=tideglassInputs();
        IDirect3DBaseTexture9* texture=nullptr;
        if(!ext->peekTexture(0,texture)||!texture)return WaterHostLiquidUnknown;
        const auto material=inputs.material(reinterpret_cast<std::uintptr_t>(texture));
        if(material.kind==WaterHostLiquidUnknown||
           (material.kind==WaterHostLiquidMagma&&inputs.camera==WaterHostLiquidMagma))return WaterHostLiquidUnknown;
        prepareCooperativeWater(desc,viewport,false);
        if(!cooperativeWater.Current())return WaterHostLiquidUnknown;
        liquidBoundsKnown=cooperativeLiquidBounds();
        liquidRamp=material.ramp;
        return material.kind;
#else
        prepareCooperativeWater(desc,viewport,false);
        if(!cooperativeWater.Current())return WaterHostLiquidUnknown;
        for(DWORD stage=0;stage<8;++stage){
            IDirect3DBaseTexture9* texture=nullptr;
            if(FAILED(ext->GetTexture(stage,&texture)))return WaterHostLiquidUnknown;
            if(!texture)continue;
            const bool colour=texture->GetType()==D3DRTYPE_TEXTURE;
            liquidBoundsKnown=colour&&cooperativeLiquidBounds();
            const auto kind=colour
                ? cooperativeWater.ClassifyLiquid(texture,liquidBoundsKnown?liquidBounds:nullptr,&liquidRamp)
                : WaterHostLiquidUnknown;
            texture->Release();
            if(colour){
                cooperativeWater.ObserveLiquid(kind,liquidBoundsKnown?liquidBounds:nullptr);
                return kind;
            }
        }
        return WaterHostLiquidUnknown;
#endif
    }
    template<class Draw> HRESULT cooperativeSurface(D3DPRIMITIVETYPE type,UINT userStride,bool skipped,Draw draw){
        if(skipped||debugMode||worldDebug||!enabled||applied||failed||!terrain||!projectionValid||!worldDepth||type!=D3DPT_TRIANGLESTRIP)return draw();
        const bool outdoors=cooperativeSurfaceDraw!=WaterHostLiquidUnknown;
#ifdef _WIN64
        if(!cooperativeWaterEnabled||(!outdoors&&!tideglassMagmaShader()))return draw();
#endif
        if(hdr.active&&!legacyTideglassTarget())return draw();
        WaterHostLiquidKind surfaceKind=cooperativeSurfaceDraw;
        UINT stride=userStride;
        if(!outdoors&&!stride){
            IDirect3DVertexBuffer9* buffer=nullptr;UINT offset=0;
            if(FAILED(ext->GetStreamSource(0,&buffer,&offset,&stride)))return draw();
            drop(buffer);
        }
        if(!outdoors&&stride!=24)return draw();
        DWORD blend=0,destination=0,write=0,depth=0;
        if(FAILED(ext->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend))||
           FAILED(ext->GetRenderState(D3DRS_DESTBLEND,&destination))||
           FAILED(ext->GetRenderState(D3DRS_ZWRITEENABLE,&write))||
           FAILED(ext->GetRenderState(D3DRS_ZENABLE,&depth))||!depth||(blend&&destination==D3DBLEND_ONE))return draw();
        D3DSURFACE_DESC desc={};D3DVIEWPORT9 viewport={};
        if(!worldDepthBound()||!fullViewport(desc,&viewport))return draw();
        // The native cloud dome also uses a textured 24-byte triangle strip.
        // Only classify liquid colours inside the current world's depth interval.
        if(!NorthlightWorldDrawDomain::accepts(projectionValid,depth!=FALSE,worldMinDepth,worldMaxDepth,viewport.MinZ,viewport.MaxZ))return draw();
        if(!outdoors)surfaceKind=cooperativeLiquidKind(desc,viewport);
        // Indoor fade uses the same path as slime.
        if(!outdoors&&surfaceKind==WaterHostLiquidFade)surfaceKind=WaterHostLiquidSlime;
        if(surfaceKind!=WaterHostLiquidSlime&&surfaceKind!=WaterHostLiquidMagma&&surfaceKind!=WaterHostLiquidFade)return draw();
        rainMrtUnbind();
        bool ready=false;
        {
            ExtensionDevice::RawScope raw(*ext);
            if(outdoors)prepareCooperativeWater(desc,viewport);
            captured=false;
            ready=resources(desc.Width,desc.Height,desc.Format)&&resolveDepthInto(depthTex,false)&&cooperativeWater.BeforeSurface(depthTex,worldDepth,outdoors,surfaceKind,liquidBoundsKnown?liquidBounds:nullptr,liquidRamp);
        }
        if(!ready)return draw();
        // The native translucent duplicate over the player's tile is not a new
        // surface. Suppress it only after the enabled core accepts the capture.
        if(!outdoors&&blend&&!write){cooperativeWater.AfterSurface(depthTex,worldDepth,false);return D3D_OK;}
        if(FAILED(ext->SetRenderState(D3DRS_ZWRITEENABLE,TRUE))){
            cooperativeWater.AfterSurface(depthTex,worldDepth,false);
            return draw();
        }
        struct DepthWriteRestore {
            IDirect3DDevice9* device;
            DWORD value;
            ~DepthWriteRestore(){device->SetRenderState(D3DRS_ZWRITEENABLE,value);}
        } restore{ext,write};
        const HRESULT hr=draw();
        {
            ExtensionDevice::RawScope raw(*ext);
            const bool resolved=SUCCEEDED(hr)&&resolveDepthInto(depthTex,false);
            const bool capturedSurface=cooperativeWater.AfterSurface(depthTex,worldDepth,resolved);
#ifdef _WIN64
            if(capturedSurface)reportTideglassFeature(surfaceKind==WaterHostLiquidMagma?2:4,
                surfaceKind==WaterHostLiquidMagma?"magma surface captured":"slime surface captured");
#else
            (void)capturedSurface;
#endif
        }
        captured=false;
        return hr;
    }
#endif
