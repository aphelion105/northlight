// Included inside WorldRenderer. Resource groups follow the frame's consumers;
// completed groups remain warm across switches until reset, resize or pressure.
unsigned screenGroups=0;
bool createResources(UINT w,UINT h,D3DFORMAT fmt,const NorthlightFrameRequirements::Plan& needs){
    if(width!=w||height!=h){releaseGPU(true);width=w;height=h;}
    const auto required=needs.screenGroups();
    if((screenGroups&required)==required&&liveTerrainDecl)return true;
    auto pixel=[&](const DWORD* code,IDirect3DPixelShader9*& shader,const char* name){return shader||check(d->CreatePixelShader(code,&shader),name);};
    auto vertex=[&](const DWORD* code,IDirect3DVertexShader9*& shader,const char* name){return shader||check(d->CreateVertexShader(code,&shader),name);};
    auto image=[&](D3DFORMAT format,IDirect3DTexture9*& texture,IDirect3DSurface9*& surface){
        if(texture&&surface)return true;
        drop(surface);drop(texture);return target(w/2,h/2,format,&texture,&surface);
    };
    if(!pixel(kWorldCompositeShader,finalPS,"world composite shader"))return false;
    if(needs.lighting){
        if(!pixel(kWorldLightingShader,lightingPS,"world lighting shader")||!pixel(kRemovalSmoothShader,removalPS,"removal smoothing shader")||
           !pixel(kTemporalLightShader,temporalPS,"temporal light shader")||!pixel(kWorldNormalsShader,normalsPS,"world normals shader"))return false;
        if(!image(D3DFMT_A16B16G16R16F,baselineLight,baselineSurface)||!image(D3DFMT_A16B16G16R16F,light,lightSurface)||
           !image(D3DFMT_A16B16G16R16F,normalBuffer,normalSurface)||!image(D3DFMT_A16B16G16R16F,fogBlurred,fogBlurredSurface))return false;
        for(unsigned i=0;i<2;++i)if(!image(D3DFMT_A16B16G16R16F,temporalLight[i],temporalLightSurface[i]))return false;
    }
    if(needs.distance){
        if(!pixel(kWorldDistanceShader,distancePS,"world distance shader"))return false;
        for(unsigned i=0;i<2;++i)if(!image(D3DFMT_R32F,temporalDepth[i],temporalDepthSurface[i]))return false;
    }
    if(needs.probes&&!pixel(kWorldGIShader,giPS,"world GI shader"))return false;
    if(needs.local){
        if(!pixel(kLocalDirectShader,localDirectPS,"local direct light shader")||!pixel(kLocalDirect16Shader,localDirect16PS,"local direct 16-light shader")||
           !pixel(kLocalFlashShader,localFlashPS,"lightning light shader"))return false;
    }
    if(needs.fog){
        if(!foreverFogPS){
            if(FAILED(d->CreatePixelShader(kForeverFogShader,&foreverFogPS)))logf("FOREVER atmosphere shader unavailable; regional fallback retained");
            D3DCAPS9 caps={};if(SUCCEEDED(d->GetDeviceCaps(&caps))&&caps.MaxPixelShader30InstructionSlots>=kForeverFogActorShaderSlots)d->CreatePixelShader(kForeverFogActorShader,&foreverFogActorPS);
            if(caps.MaxPixelShader30InstructionSlots>=kForeverFogTransitionShaderSlots)d->CreatePixelShader(kForeverFogTransitionShader,&foreverFogFadePS);
            if(caps.MaxPixelShader30InstructionSlots>=kForeverFogActorTransitionShaderSlots)d->CreatePixelShader(kForeverFogActorTransitionShader,&foreverFogActorFadePS);
            d->CreatePixelShader(kForeverFogNoNoiseShader,&foreverFogNoNoisePS);
            if(caps.MaxPixelShader30InstructionSlots>=kForeverFogActorNoNoiseShaderSlots)d->CreatePixelShader(kForeverFogActorNoNoiseShader,&foreverFogActorNoNoisePS);
            if(caps.MaxPixelShader30InstructionSlots>=kForeverFogNoNoiseTransitionShaderSlots)d->CreatePixelShader(kForeverFogNoNoiseTransitionShader,&foreverFogNoNoiseFadePS);
            if(caps.MaxPixelShader30InstructionSlots>=kForeverFogActorNoNoiseTransitionShaderSlots)d->CreatePixelShader(kForeverFogActorNoNoiseTransitionShader,&foreverFogActorNoNoiseFadePS);
        }
        if(!pixel(kWorldFogShader,fogPS,"volume shader")||!pixel(kFogBlurShader,fogBlurPS,"volume blur shader")||
           !image(D3DFMT_A16B16G16R16F,fog,fogSurface)||!image(D3DFMT_A16B16G16R16F,fogBlurred,fogBlurredSurface)||!image(D3DFMT_A16B16G16R16F,fogHistory,fogHistorySurface))return false;
        if(needs.local&&(!pixel(kLocalFogShader,localFogPS,"local fog glow shader")||!pixel(kLocalFogCombineShader,localFogCombinePS,"local fog combine shader")))return false;
        if(!(screenGroups&4u)){
            D3DCAPS9 caps={};const bool extended=SUCCEEDED(d->GetDeviceCaps(&caps))&&caps.MaxPixelShader30InstructionSlots>=std::max(kWorldFogActorShaderSlots,kFogCloudsActorShaderSlots);
            if(extended&&((!fogActorPS&&FAILED(d->CreatePixelShader(kWorldFogActorShader,&fogActorPS)))||(!fogCloudsActorPS&&FAILED(d->CreatePixelShader(kFogCloudsActorShader,&fogCloudsActorPS))))){drop(fogActorPS);drop(fogCloudsActorPS);}
            if(!fogCloudsPS&&FAILED(d->CreatePixelShader(kFogCloudsShader,&fogCloudsPS)))fogCloudsPS=nullptr;
            if(!fogTemporalPS&&FAILED(d->CreatePixelShader(kFogTemporalShader,&fogTemporalPS)))fogTemporalPS=nullptr;
        }
    }
    if(needs.materials&&!pixel(kMaterialCompositeShader,materialPS,"full-resolution material shader"))return false;
    if(needs.shadows){
        if(!pixel(kShadowPSShader,shadowPS,"shadow shader")||!pixel(kShadowReplayPSShader,replayPS,"replay shader")||
           !vertex(kShadowVSShader,shadowVS,"shadow vertex shader")||!vertex(kShadowCacheVSShader,cachedShadowVS,"cached shadow vertex shader")||
           !pixel(kStaticCasterPSShader,cachedShadowPS,"cached shadow depth shader")||!pixel(kShadowUnionShader,unionPS,"shadow union shader"))return false;
        if(!(screenGroups&16u)){
            if(!cachedFastPS&&FAILED(d->CreatePixelShader(kStaticCasterFastPSShader,&cachedFastPS)))drop(cachedFastPS);
            if(!cachedOpaqueFastPS&&FAILED(d->CreatePixelShader(kStaticCasterOpaqueFastPSShader,&cachedOpaqueFastPS)))drop(cachedOpaqueFastPS);
            if(!cachedOpaquePS&&FAILED(d->CreatePixelShader(kStaticCasterOpaquePSShader,&cachedOpaquePS)))drop(cachedOpaquePS);
        }
        D3DCAPS9 caps={};d->GetDeviceCaps(&caps);actorShaderResources(caps);
    }
    if(!neutralAO){
        if(!check(d->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&neutralAO,nullptr),"neutral AO texture"))return false;
        D3DLOCKED_RECT lock={};if(!check(neutralAO->LockRect(0,&lock,nullptr,0),"neutral AO lock"))return false;*static_cast<DWORD*>(lock.pBits)=0xff000000u;neutralAO->UnlockRect(0);
    }
    if(!neutralWhite){
        if(!check(d->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&neutralWhite,nullptr),"neutral shadow texture"))return false;
        D3DLOCKED_RECT lock={};if(!check(neutralWhite->LockRect(0,&lock,nullptr,0),"neutral shadow lock"))return false;*static_cast<DWORD*>(lock.pBits)=0xffffffffu;neutralWhite->UnlockRect(0);
    }
    if(!neutralZero&&SUCCEEDED(d->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&neutralZero,nullptr))){
        D3DLOCKED_RECT lock={};if(SUCCEEDED(neutralZero->LockRect(0,&lock,nullptr,0))){*static_cast<DWORD*>(lock.pBits)=0;neutralZero->UnlockRect(0);}else drop(neutralZero);
    }
    if(!ditherNoise){using namespace NorthlightDitherNoise;
        if(SUCCEEDED(d->CreateTexture(N,N,1,0,D3DFMT_A32B32G32R32F,D3DPOOL_MANAGED,&ditherNoise,nullptr))){
            static const std::vector<float> tile=offsets();
            D3DLOCKED_RECT lock={};if(SUCCEEDED(ditherNoise->LockRect(0,&lock,nullptr,0))){
                for(unsigned y=0;y<N;++y)for(unsigned x=0;x<N;++x){auto* p=reinterpret_cast<float*>(static_cast<char*>(lock.pBits)+size_t(y)*lock.Pitch)+x*4;p[0]=p[1]=p[2]=tile[y*N+x];p[3]=0;}
                ditherNoise->UnlockRect(0);
            }else drop(ditherNoise);
        }
    }
    const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_NORMAL,0},{0,24,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
    static_assert(sizeof(StaticShadow::Vertex)==20,"live terrain shadow vertex stride");
    const D3DVERTEXELEMENT9 liveElements[]={{0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
    if((!shadowDecl&&!check(d->CreateVertexDeclaration(elements,&shadowDecl),"shadow declaration"))||(!liveTerrainDecl&&!check(d->CreateVertexDeclaration(liveElements,&liveTerrainDecl),"live terrain declaration")))return false;
    screenGroups|=required;return true;
}
bool resources(UINT w,UINT h,D3DFORMAT fmt,const NorthlightFrameRequirements::Plan& needs){
    if(width==w&&height==h&&liveTerrainDecl&&(screenGroups&needs.screenGroups())==needs.screenGroups())return true;
    const DWORD now=GetTickCount();if(screenResourceRetry&&DWORD(now-screenResourceRetryAt)<1000)return false;
    screenResourceOom=false;
    struct Scope {bool& flag;explicit Scope(bool& value):flag(value){flag=true;}~Scope(){flag=false;}};
    bool ok;{Scope scope(buildingScreenResources);ok=createResources(w,h,fmt,needs);}
    if(!ok&&screenResourceOom){releaseGPU(true);screenResourceRetry=true;screenResourceRetryAt=now;if(!screenResourceReported||DWORD(now-screenResourceReportAt)>=60000){screenResourceReported=true;screenResourceReportAt=now;logf("WORLD screen resources deferred: out of memory retryMs=1000");}}
    else if(ok)screenResourceRetry=false;
    return ok;
}
bool resources(UINT w,UINT h,D3DFORMAT fmt){return resources(w,h,fmt,frameRequirements(quality.autoBump));}

// One half-resolution MRT, only after a proven prelit receiver is present.
// Allocation/capability failure keeps its authored lighting and can retry.
bool prelitDaylightResources(UINT w,UINT h){
    if(prelitDaylightSurface)return true;
    const DWORD now=GetTickCount();
    if(prelitDaylightRetryAt&&LONG(now-prelitDaylightRetryAt)<0)return false;
    prelitDaylightRetryAt=now+1000;
    D3DCAPS9 caps={};
    const auto budget=NorthlightGeometryMemory::defaultTargetBudget();
    if(FAILED(d->GetDeviceCaps(&caps))||caps.NumSimultaneousRTs<3||
       !NorthlightGeometryMemory::admits(geometryAdmission(budget),budget))return false;
    HRESULT hr=d->CreateTexture(w/2,h/2,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&prelitDaylight,nullptr);
    if(SUCCEEDED(hr)&&prelitDaylight)hr=prelitDaylight->GetSurfaceLevel(0,&prelitDaylightSurface);
    if(SUCCEEDED(hr)&&prelitDaylightSurface){prelitDaylightRetryAt=0;return true;}
    drop(prelitDaylightSurface);drop(prelitDaylight);return false;
}
