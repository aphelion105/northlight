#ifdef NORTHLIGHT_WATER
    IDirect3DTexture9* tideglassMask=nullptr;
    IDirect3DSurface9* tideglassMaskSurface=nullptr;
    IDirect3DPixelShader9 *tideglassMaskPS=nullptr,*tideglassRainPS=nullptr;

    bool legacyTideglassTarget(){
        IDirect3DSurface9* target=nullptr;
        if(ext->peekRenderTarget(0,target))return target==hdrLegacyBack;
        const bool legacy=SUCCEEDED(ext->GetRenderTarget(0,&target))&&target==hdrLegacyBack;
        drop(target);
        return legacy;
    }
    void releaseTideglassTargets(){
        drop(tideglassMaskSurface);drop(tideglassMask);drop(tideglassMaskPS);drop(tideglassRainPS);
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
            const auto* snapshot=NorthlightStream::activeSnapshot;
            const auto waterFrame=SelectNorthlightWaterFrame(snapshot?&snapshot->waterFrame:nullptr,streamStats!=nullptr,
                                                       [&]{return captureNativeWater(viewport);});
            ExtensionDevice::RawScope raw(*ext);
            if(!cooperativeWater.Begin(ext,waterFrame))return;
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
    WaterHostLiquidKind cooperativeLiquidKind(const D3DSURFACE_DESC& desc,const D3DVIEWPORT9& viewport){
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
    }
    template<class Draw> HRESULT cooperativeSurface(D3DPRIMITIVETYPE type,UINT userStride,bool skipped,Draw draw){
        if(skipped||(hdr.active&&!legacyTideglassTarget())||debugMode||worldDebug||!enabled||applied||failed||!terrain||!projectionValid||!worldDepth||type!=D3DPT_TRIANGLESTRIP)return draw();
        const bool outdoors=cooperativeSurfaceDraw!=WaterHostLiquidUnknown;
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
            cooperativeWater.AfterSurface(depthTex,worldDepth,resolved);
        }
        captured=false;
        return hr;
    }
#endif
