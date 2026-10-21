#pragma once
#include "surface_compiled_shaders.h"
#include "quality_settings.h"
#include "surface_frame.h"

// Included after SavedState, drop, logf and NameMaskCompositeBind.
class NorthlightSurfaceRenderer {
    IDirect3DDevice9* d;NorthlightStateBlockPool stateBlocks;
    IDirect3DTexture9 *color=nullptr,*depth=nullptr;IDirect3DSurface9 *colorSurface=nullptr,*depthSurface=nullptr,*resolveSurface=nullptr;
    IDirect3DTexture9 *reflectionScene=nullptr,*reflections=nullptr;IDirect3DSurface9 *reflectionSceneSurface=nullptr,*reflectionSurface=nullptr;
    IDirect3DTexture9* completedDepth=nullptr;UINT completedWidth=0,completedHeight=0;D3DFORMAT completedFormat=D3DFMT_UNKNOWN;
    IDirect3DTexture9* boundsTexture=nullptr;IDirect3DSurface9* boundsScratch=nullptr;IDirect3DPixelShader9* boundsPS=nullptr;
    UINT boundsWidth=0,boundsHeight=0,boundsLevels=0;bool boundsUnavailable=false;
    IDirect3DPixelShader9 *reflectionPS=nullptr,*lateReflectionPS=nullptr;UINT reflectionWidth=0,reflectionHeight=0,reflectionSceneWidth=0,reflectionSceneHeight=0;
    IDirect3DPixelShader9 *copyPS=nullptr,*waterPS[3]={},*weatherPS[2]={};
    IDirect3DPixelShader9* pointsPS=nullptr;
    UINT width=0,height=0,sourceWidth=0,sourceHeight=0;bool attempted=false,available=false,failed=false;
    D3DFORMAT sourceFormat=D3DFMT_UNKNOWN;
    bool weatherUsed=false;unsigned idleWater=0;
    int waterStatus=-1;
    NorthlightSurfaceFrame diagnosticFrame{};NorthlightSurfaceLights diagnosticLights{};float diagnosticValues[26][4]={};bool diagnosticReady=false;
    void reportWater(int status,const char* reason){
        if(waterStatus==status)return;waterStatus=status;
        logf("SURFACE water status=%s snapshot=%ux%u SSR=%ux%u",reason,width,height,reflectionWidth,reflectionHeight);
    }
    unsigned memoryWait=0;
    bool admitMemory(unsigned stage,const char* name,std::uint64_t pending){
        MEMORYSTATUSEX memory{};memory.dwLength=sizeof memory;
        const bool ready=GlobalMemoryStatusEx(&memory)&&memory.ullAvailVirtual>=pending+(96ull<<20);
        const bool waiting=(memoryWait&stage)!=0;
        if(ready==waiting)logf("SURFACE memory %s status=%s pendingMiB=%.2f availableMiB=%.2f",name,ready?"ready":"waiting",double(pending)/(1ull<<20),double(memory.ullAvailVirtual)/(1ull<<20));
        if(ready)memoryWait&=~stage;else memoryWait|=stage;return ready;
    }
    bool check(HRESULT hr,const char* stage){if(SUCCEEDED(hr))return true;if(!failed)logf("SURFACE disabled: %s hr=0x%08lx (native water/weather retained)",stage,(unsigned long)hr);failed=true;return false;}
    bool target(UINT w,UINT h,D3DFORMAT format,IDirect3DTexture9*& t,IDirect3DSurface9*& s){return check(d->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,format,D3DPOOL_DEFAULT,&t,nullptr),"target")&&check(t->GetSurfaceLevel(0,&s),"target surface");}
    HRESULT quad(UINT w,UINT h){struct V{float x,y,z,w,u,v;} v[]={{-.5f,-.5f,0,1,0,0},{float(w)-.5f,-.5f,0,1,1,0},{-.5f,float(h)-.5f,0,1,0,1},{float(w)-.5f,float(h)-.5f,0,1,1,1}};D3DVIEWPORT9 vp={0,0,w,h,0,1};d->SetViewport(&vp);return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(V));}
    void state(){
        d->SetDepthStencilSurface(nullptr);for(unsigned i=1;i<4;++i)d->SetRenderTarget(i,nullptr);d->SetVertexShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetStreamSourceFreq(0,1);
        for(auto s:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_FOGENABLE,D3DRS_LIGHTING,D3DRS_SRGBWRITEENABLE,D3DRS_CLIPPLANEENABLE,D3DRS_WRAP0})d->SetRenderState(s,0);
        d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);d->SetRenderState(D3DRS_MULTISAMPLEMASK,0xffffffff);d->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS,TRUE);
        d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0);d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);
        DWORD point=0;d->GetRenderState(D3DRS_POINTSIZE,&point);if(point==MAKEFOURCC('A','2','M','1'))d->SetRenderState(D3DRS_POINTSIZE,MAKEFOURCC('A','2','M','0'));
        for(unsigned i=0;i<8;++i){d->SetTexture(i,nullptr);d->SetSamplerState(i,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(i,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);d->SetSamplerState(i,D3DSAMP_MINFILTER,i==0||i==4?D3DTEXF_LINEAR:D3DTEXF_POINT);d->SetSamplerState(i,D3DSAMP_MAGFILTER,i==0||i==4?D3DTEXF_LINEAR:D3DTEXF_POINT);d->SetSamplerState(i,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(i,D3DSAMP_MAXMIPLEVEL,0);d->SetSamplerState(i,D3DSAMP_MIPMAPLODBIAS,0);d->SetSamplerState(i,D3DSAMP_SRGBTEXTURE,FALSE);}
    }
    void blend(){d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);}
    void releaseBounds(){drop(boundsTexture);drop(boundsScratch);boundsWidth=boundsHeight=boundsLevels=0;}
    void releaseReflections(bool withDepth=true){releaseBounds();drop(reflectionSceneSurface);drop(reflectionSurface);drop(reflectionScene);drop(reflections);reflectionWidth=reflectionHeight=reflectionSceneWidth=reflectionSceneHeight=0;if(withDepth){drop(completedDepth);completedWidth=completedHeight=0;completedFormat=D3DFMT_UNKNOWN;}}
    bool copyColor(IDirect3DSurface9* source,IDirect3DSurface9* targetSurface,UINT w,UINT h){
        D3DSURFACE_DESC desc{};if(FAILED(source->GetDesc(&desc)))return false;
        if(desc.MultiSampleType!=D3DMULTISAMPLE_NONE&&(w!=desc.Width||h!=desc.Height)){
            if(!resolveSurface&&!check(d->CreateRenderTarget(desc.Width,desc.Height,desc.Format,D3DMULTISAMPLE_NONE,0,FALSE,&resolveSurface,nullptr),"MSAA color resolve"))return false;
            if(!check(d->StretchRect(source,nullptr,resolveSurface,nullptr,D3DTEXF_NONE),"MSAA scene copy"))return false;source=resolveSurface;
        }
        return check(d->StretchRect(source,nullptr,targetSurface,nullptr,w==desc.Width&&h==desc.Height?D3DTEXF_NONE:D3DTEXF_LINEAR),"surface scene copy");
    }
    bool depthBounds(IDirect3DTexture9* finalDepth,UINT w,UINT h){
        if(boundsUnavailable)return false;
        UINT bw=1,bh=1;while(bw<(w+1)/2)bw*=2;while(bh<(h+1)/2)bh*=2;
        UINT levels=1;for(UINT size=std::max(bw,bh);size>1;size/=2)++levels;
        std::uint64_t bytes=0;for(UINT level=0;level<levels;++level)bytes+=std::uint64_t(std::max(1u,bw>>level))*std::max(1u,bh>>level)*8;
        bytes+=std::uint64_t(std::max(1u,bw/2))*std::max(1u,bh/2)*8;
        const bool resize=boundsWidth!=bw||boundsHeight!=bh;
        if(bytes>(64ull<<20)||!admitMemory(8,"SSR depth intervals",resize||!boundsTexture?bytes:0)){releaseBounds();return false;}
        if(resize)releaseBounds();
        if(!boundsTexture){
            const HRESULT textureResult=d->CreateTexture(bw,bh,levels,D3DUSAGE_RENDERTARGET,D3DFMT_G32R32F,D3DPOOL_DEFAULT,&boundsTexture,nullptr);
            const HRESULT scratchResult=SUCCEEDED(textureResult)?d->CreateRenderTarget(std::max(1u,bw/2),std::max(1u,bh/2),D3DFMT_G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&boundsScratch,nullptr):textureResult;
            if(FAILED(textureResult)||FAILED(scratchResult)){releaseBounds();boundsUnavailable=true;logf("SURFACE SSR depth intervals unavailable; bounded direct trace retained");return false;}
            boundsWidth=bw;boundsHeight=bh;boundsLevels=levels;
        }
        if(!boundsPS&&!check(d->CreatePixelShader(kSSRDepthBoundsShader,&boundsPS),"SSR depth interval shader"))return false;
        d->SetTexture(5,nullptr);d->SetTexture(3,finalDepth);d->SetPixelShader(boundsPS);d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
        for(UINT level=0;level<levels;++level){
            IDirect3DSurface9* destination=nullptr;
            if(!check(boundsTexture->GetSurfaceLevel(level,&destination),"SSR depth interval level"))return false;
            const UINT dw=std::max(1u,bw>>level),dh=std::max(1u,bh>>level);
            const float input[]={1.f/(level?std::max(1u,bw>>(level-1)):w),1.f/(level?std::max(1u,bh>>(level-1)):h),float(level?level-1:0),level?0.f:1.f};
            d->SetPixelShaderConstantF(24,input,1);
            const bool ready=check(d->SetRenderTarget(0,level?boundsScratch:destination),"SSR depth interval target")&&check(quad(dw,dh),"SSR depth interval reduction");
            d->SetTexture(5,nullptr);
            const RECT rect={0,0,LONG(dw),LONG(dh)};
            const bool copied=ready&&(!level||check(d->StretchRect(boundsScratch,&rect,destination,nullptr,D3DTEXF_NONE),"SSR depth interval copy"));
            destination->Release();if(!copied)return false;
            if(!level&&!check(d->SetRenderTarget(0,boundsScratch),"SSR depth interval unbind"))return false;
            if(!check(d->SetTexture(5,boundsTexture),"SSR depth interval input"))return false;d->SetSamplerState(5,D3DSAMP_MIPFILTER,D3DTEXF_POINT);
        }
        const float trace[]={1.f/bw,1.f/bh,float(levels-1),1};d->SetPixelShaderConstantF(24,trace,1);return true;
    }
    bool reflectionPass(IDirect3DSurface9* scene,IDirect3DTexture9* mask,IDirect3DTexture9* finalDepth,UINT w,UINT h,const NorthlightQuality::Settings& settings){
        if(!settings.waterSSR||!settings.waterSSRStrength){releaseReflections(false);return false;}
        if(!finalDepth)return false;
        const UINT rw=std::max(1u,(w*settings.waterSSRResolution+99)/100),rh=std::max(1u,(h*settings.waterSSRResolution+99)/100);
        const std::uint64_t bytes=(std::uint64_t(w)*h+std::uint64_t(rw)*rh)*4;
        const bool resize=reflectionWidth!=rw||reflectionHeight!=rh||reflectionSceneWidth!=w||reflectionSceneHeight!=h;
        const std::uint64_t pending=std::uint64_t(w)*h*4*(resize||!reflectionScene)+std::uint64_t(rw)*rh*4*(resize||!reflections);
        if(bytes>(128ull<<20)||!admitMemory(2,"SSR",pending)){releaseReflections(false);return false;}
        if(resize){releaseReflections(false);reflectionWidth=rw;reflectionHeight=rh;reflectionSceneWidth=w;reflectionSceneHeight=h;}
        if(!reflectionScene&&!target(w,h,sourceFormat,reflectionScene,reflectionSceneSurface))return false;
        if(!reflections&&!target(rw,rh,D3DFMT_A8R8G8B8,reflections,reflectionSurface))return false;
        if(!reflectionPS&&!check(d->CreatePixelShader(kWaterSSRShader,&reflectionPS),"water SSR shader"))return false;
        // Called after all native world draws. Never trace the incomplete pre-water refraction snapshot.
        if(!copyColor(scene,reflectionSceneSurface,w,h))return false;
        const float noHierarchy[]={0,0,0,0};d->SetPixelShaderConstantF(24,noHierarchy,1);
        if(!depthBounds(finalDepth,w,h))d->SetPixelShaderConstantF(24,noHierarchy,1);if(failed)return false;
        d->SetTexture(0,reflectionScene);d->SetTexture(2,mask);d->SetTexture(3,finalDepth);
        // Depth and accepted radiance refer to the same source texel, even at quarter-resolution SSR.
        d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
        if(!check(d->SetRenderTarget(0,reflectionSurface),"SSR target"))return false;
        d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);
        if(!check(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0),"SSR clear"))return false;
        d->SetPixelShader(reflectionPS);return check(quad(rw,rh),"world water SSR");
    }
public:
    explicit NorthlightSurfaceRenderer(IDirect3DDevice9* device):d(device),stateBlocks(device){}
    ~NorthlightSurfaceRenderer(){reset();drop(boundsPS);drop(copyPS);drop(pointsPS);drop(reflectionPS);drop(lateReflectionPS);for(auto& p:waterPS)drop(p);for(auto& p:weatherPS)drop(p);}
    void reset(){stateBlocks.clear();releaseReflections();drop(colorSurface);drop(depthSurface);drop(resolveSurface);drop(color);drop(depth);width=height=sourceWidth=sourceHeight=0;sourceFormat=D3DFMT_UNKNOWN;attempted=available=failed=weatherUsed=false;idleWater=0;boundsUnavailable=false;diagnosticReady=false;}
    void endFrame(){
        diagnosticReady=false;
        if((!attempted&&!weatherUsed)||failed)stateBlocks.clear();
        if(attempted)idleWater=0;else if(++idleWater==600){const bool wasFailed=failed;reset();failed=wasFailed;}
        attempted=available=weatherUsed=false;
    }
    bool canCapture()const{return !attempted&&!failed;}
    IDirect3DTexture9* completedDepthTarget(UINT w,UINT h,D3DFORMAT format,const NorthlightQuality::Settings& settings){
        if(!settings.water){releaseReflections();return nullptr;}
        if(!available||failed||!w||!h||format==D3DFMT_UNKNOWN)return nullptr;
        if(completedWidth!=w||completedHeight!=h||completedFormat!=format){drop(completedDepth);completedWidth=w;completedHeight=h;completedFormat=format;}
        if(!completedDepth){const auto bytes=std::uint64_t(w)*h*4;
            if(bytes>(80ull<<20)||!admitMemory(4,"completed depth",bytes))return nullptr;
            if(!check(d->CreateTexture(w,h,1,D3DUSAGE_DEPTHSTENCIL,format,D3DPOOL_DEFAULT,&completedDepth,nullptr),"completed SSR depth"))return nullptr;}
        return completedDepth;
    }
    // A single immutable pre-water snapshot serves every tile, including MSAA. No world-cache dependency.
    bool capture(IDirect3DSurface9* source,IDirect3DTexture9* sourceDepth,UINT w,UINT h,const NorthlightQuality::Settings& settings){
        if(!settings.water)return false;const auto preset=settings.preset;
        if(!canCapture()||!source||!sourceDepth)return false;attempted=true;
        D3DSURFACE_DESC desc{};if(FAILED(source->GetDesc(&desc)))return false;
        if(desc.Format!=D3DFMT_A8R8G8B8&&desc.Format!=D3DFMT_X8R8G8B8)return false;
        UINT tw=preset==NorthlightQuality::Preset::Quality?w:(w+1)/2,th=preset==NorthlightQuality::Preset::Quality?h:(h+1)/2;
        const bool separateResolve=desc.MultiSampleType!=D3DMULTISAMPLE_NONE&&(tw!=w||th!=h);
        const std::uint64_t bytes=std::uint64_t(tw)*th*8+(separateResolve?std::uint64_t(w)*h*4:0),cap=(preset==NorthlightQuality::Preset::Quality?80ull:48ull)<<20;
        if(!w||!h||bytes>cap)return false;
        const bool resize=sourceWidth!=w||sourceHeight!=h||width!=tw||height!=th||sourceFormat!=desc.Format;
        // Resident targets have already consumed address space. Charge only new allocations.
        const std::uint64_t pending=std::uint64_t(tw)*th*4*((settings.waterRefraction&&(resize||!color))+(resize||!depth))+
            (separateResolve&&(resize||!resolveSurface)?std::uint64_t(w)*h*4:0);
        if(!admitMemory(1,"water capture",pending))return false;
        if(resize){reset();attempted=true;sourceWidth=w;sourceHeight=h;width=tw;height=th;sourceFormat=desc.Format;}
        SavedState saved(d,&stateBlocks);if(!saved.ok||saved.targets[1])return false;
        state();
        if(settings.waterRefraction){if(!color&&!target(tw,th,desc.Format,color,colorSurface))return false;}else{drop(colorSurface);drop(color);}
        if(!depth&&!target(tw,th,D3DFMT_R32F,depth,depthSurface))return false;
        if(!copyPS&&!check(d->CreatePixelShader(kSurfaceDepthCopyShader,&copyPS),"depth copy shader"))return false;
        if(settings.waterRefraction&&!copyColor(source,colorSurface,tw,th))return false;
        if(!check(d->SetRenderTarget(0,depthSurface),"pre-water depth target"))return false;d->SetTexture(0,sourceDepth);d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT);d->SetPixelShader(copyPS);
        available=check(quad(tw,th),"pre-water depth copy");return available;
    }
    void water(IDirect3DSurface9* targetSurface,IDirect3DTexture9* mask,IDirect3DTexture9* finalDepth,UINT w,UINT h,const NorthlightSurfaceFrame& context,const NorthlightQuality::Settings& settings,const NorthlightSurfaceLights& lights,IDirect3DSurface9* nameDepth,IDirect3DTexture9* reflectionDepth,bool deferReflections=false){
        if(!settings.water){if(sourceWidth||attempted||available)reset();reportWater(0,"disabled");return;}const auto preset=settings.preset;
        if(failed){reportWater(1,"device failure");return;}
        if(!available){reportWater(2,"waiting for pre-water capture");return;}
        if(!mask){reportWater(3,"waiting for liquid mask");return;}
        if(!finalDepth||!reflectionDepth){reportWater(4,"waiting for completed depth");return;}
        SavedState saved(d,&stateBlocks);if(!saved.ok)return;state();
        const float options[]={float(settings.waterRefraction),float(settings.waterGlints),float(settings.waterSSRStrength)*.01f,float(settings.waterSSR&&settings.waterSSRStrength)};
        const float trace[]={float(settings.waterSSRSteps),float(settings.waterSSRDistance),float(settings.waterSSRThickness)*.01f,float(settings.waterSSRRefineSteps)};
        d->SetPixelShaderConstantF(0,&context.c[0][0],11);d->SetPixelShaderConstantF(20,&context.fog[0][0],2);d->SetPixelShaderConstantF(22,options,1);d->SetPixelShaderConstantF(23,trace,1);
        if(deferReflections&&(!settings.waterSSR||!settings.waterSSRStrength))releaseReflections(false);
        diagnosticLights=lights;
        const bool reflected=!deferReflections&&reflectionPass(targetSurface,mask,reflectionDepth,w,h,settings);
        if(failed)return;
        unsigned i=unsigned(preset);const DWORD* code=i==0?kWaterQualityShader:i==1?kWaterBalancedShader:kWaterPerformanceShader;
        if(!waterPS[i]&&!check(d->CreatePixelShader(code,&waterPS[i]),"water shader"))return;
        // Completed Z is needed by the material too, independently of SSR. Never fall back to early AO Z.
        auto* visibleDepth=reflectionDepth;
        if(!check(d->SetRenderTarget(0,targetSurface),"water target"))return;blend();d->SetRenderState(D3DRS_COLORWRITEENABLE,7);d->SetPixelShader(waterPS[i]);d->SetTexture(0,color);d->SetTexture(1,depth);d->SetTexture(2,mask);d->SetTexture(3,visibleDepth);d->SetTexture(4,reflected?reflections:nullptr);d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
        float activeOptions[4]={options[0],options[1],options[2],reflected?1.f:0.f};d->SetPixelShaderConstantF(22,activeOptions,1);
        d->SetPixelShaderConstantF(20,&context.fog[0][0],2);
        NameMaskCompositeBind protect(d,nameDepth);if(!check(quad(w,h),"water beauty and SSR"))return;
        if(!deferReflections)reportWater(reflected?5:6,reflected?"water + SSR":"water without SSR");
        if(settings.waterPointLights&&lights.count){
            if(!pointsPS&&!check(d->CreatePixelShader(kWaterPointLightsShader,&pointsPS),"water point lights shader"))return;
            d->SetRenderState(D3DRS_COLORWRITEENABLE,7);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE);d->SetPixelShader(pointsPS);
            const float count[]={float(std::min(lights.count,4u)),0,0,0};d->SetPixelShaderConstantF(11,&lights.position[0][0],4);d->SetPixelShaderConstantF(15,&lights.color[0][0],4);d->SetPixelShaderConstantF(19,count,1);
            check(quad(w,h),"water point glints");}
    }
    void waterReflections(IDirect3DSurface9* targetSurface,IDirect3DTexture9* mask,IDirect3DTexture9* reflectionDepth,UINT w,UINT h,const NorthlightSurfaceFrame& context,const NorthlightQuality::Settings& settings,IDirect3DSurface9* nameDepth,IDirect3DTexture9* fogTexture=nullptr,IDirect3DTexture9* fogDistance=nullptr){
        diagnosticReady=false;
        if(!settings.water||!settings.waterSSR||!settings.waterSSRStrength){releaseReflections(false);if(settings.water)reportWater(6,"water without SSR");return;}
        if(failed||!available||!mask||!reflectionDepth)return;
        SavedState saved(d,&stateBlocks);if(!saved.ok)return;state();
        const float options[]={float(settings.waterRefraction),float(settings.waterGlints),float(settings.waterSSRStrength)*.01f,1};
        const float trace[]={float(settings.waterSSRSteps),float(settings.waterSSRDistance),float(settings.waterSSRThickness)*.01f,float(settings.waterSSRRefineSteps)};
        d->SetPixelShaderConstantF(0,&context.c[0][0],11);d->SetPixelShaderConstantF(20,&context.fog[0][0],2);d->SetPixelShaderConstantF(22,options,1);d->SetPixelShaderConstantF(23,trace,1);
        if(!reflectionPass(targetSurface,mask,reflectionDepth,w,h,settings)||failed){if(!failed)reportWater(6,"water without SSR");return;}
        if(!lateReflectionPS&&!check(d->CreatePixelShader(kLateWaterSSRShader,&lateReflectionPS),"late lit water SSR shader"))return;
        D3DSURFACE_DESC fogDesc{},distanceDesc{};
        const bool hasFog=fogTexture&&fogDistance&&SUCCEEDED(fogTexture->GetLevelDesc(0,&fogDesc))&&SUCCEEDED(fogDistance->GetLevelDesc(0,&distanceDesc))&&fogDesc.Width==distanceDesc.Width&&fogDesc.Height==distanceDesc.Height&&fogDesc.Width&&fogDesc.Height;
        const float fog[]={hasFog?1.f/fogDesc.Width:0,hasFog?1.f/fogDesc.Height:0,hasFog?1.f:0,0};d->SetPixelShaderConstantF(25,fog,1);
        d->SetTexture(0,reflectionScene);d->SetTexture(2,mask);d->SetTexture(3,reflectionDepth);d->SetTexture(4,reflections);d->SetTexture(5,nullptr);d->SetTexture(6,hasFog?fogTexture:nullptr);d->SetTexture(7,hasFog?fogDistance:nullptr);
        d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
        if(!check(d->SetRenderTarget(0,targetSurface),"late water SSR target"))return;
        d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_COLORWRITEENABLE,7);d->SetPixelShader(lateReflectionPS);
        NameMaskCompositeBind protect(d,nameDepth);if(!check(quad(w,h),"late lit water SSR"))return;reportWater(7,"water + lit SSR");
        diagnosticFrame=context;std::memset(diagnosticValues,0,sizeof diagnosticValues);std::memcpy(diagnosticValues,context.c,sizeof context.c);
        std::memcpy(diagnosticValues[11],diagnosticLights.position,sizeof diagnosticLights.position);std::memcpy(diagnosticValues[15],diagnosticLights.color,sizeof diagnosticLights.color);diagnosticValues[19][0]=float(std::min(diagnosticLights.count,4u));
        std::memcpy(diagnosticValues[20],context.fog,sizeof context.fog);std::memcpy(diagnosticValues[22],options,sizeof options);std::memcpy(diagnosticValues[23],trace,sizeof trace);
        if(boundsTexture){diagnosticValues[24][0]=1.f/boundsWidth;diagnosticValues[24][1]=1.f/boundsHeight;diagnosticValues[24][2]=float(boundsLevels-1);diagnosticValues[24][3]=1;}
        std::memcpy(diagnosticValues[25],fog,sizeof fog);diagnosticReady=true;
    }
    const NorthlightSurfaceFrame* diagnosticContext()const{return diagnosticReady?&diagnosticFrame:nullptr;}
    const float* diagnosticConstants()const{return diagnosticReady?&diagnosticValues[0][0]:nullptr;}
    template<typename Dump> bool diagnosticDump(IDirect3DTexture9* mask,IDirect3DTexture9* completed,Dump dump){
        if(!diagnosticReady||!available||!mask||!completed)return false;
        SavedState saved(d,&stateBlocks);if(!saved.ok)return false;
        bool result=true;
        if(colorSurface)result=dump(colorSurface,"water-pre-color")&&result;
        result=dump(depthSurface,"water-pre-depth")&&result;
        result=dump(reflectionSceneSurface,"water-lit-source")&&result;
        result=dump(reflectionSurface,"water-ssr-hits")&&result;
        IDirect3DSurface9* maskSurface=nullptr;
        if(SUCCEEDED(mask->GetSurfaceLevel(0,&maskSurface))){result=dump(maskSurface,"water-mask")&&result;maskSurface->Release();}else result=false;
        D3DSURFACE_DESC desc{};IDirect3DSurface9* readable=nullptr;
        if(FAILED(completed->GetLevelDesc(0,&desc))||FAILED(d->CreateRenderTarget(desc.Width,desc.Height,D3DFMT_R32F,D3DMULTISAMPLE_NONE,0,FALSE,&readable,nullptr)))return false;
        state();d->SetTexture(0,completed);d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT);d->SetPixelShader(copyPS);
        const bool copied=SUCCEEDED(d->SetRenderTarget(0,readable))&&SUCCEEDED(quad(desc.Width,desc.Height));
        if(copied)result=dump(readable,"water-completed-depth")&&result;else result=false;
        readable->Release();return result;
    }
    void precipitation(IDirect3DSurface9* targetSurface,IDirect3DTexture9* finalDepth,UINT w,UINT h,const NorthlightSurfaceFrame& context,NorthlightQuality::Preset preset,IDirect3DSurface9* nameDepth){
        if(failed||!finalDepth||context.c[3][0]<=0)return;SavedState saved(d,&stateBlocks);if(!saved.ok)return;state();unsigned i=preset==NorthlightQuality::Preset::Performance?1:0;
        weatherUsed=true;
        if(!weatherPS[i]&&!check(d->CreatePixelShader(i?kPrecipitationPerformanceShader:kPrecipitationQualityShader,&weatherPS[i]),"precipitation shader"))return;
        if(!check(d->SetRenderTarget(0,targetSurface),"precipitation target"))return;blend();d->SetRenderState(D3DRS_COLORWRITEENABLE,7);d->SetPixelShader(weatherPS[i]);d->SetTexture(3,finalDepth);d->SetPixelShaderConstantF(0,&context.c[0][0],11);
        NameMaskCompositeBind protect(d,nameDepth);check(quad(w,h),"supplemental precipitation");
    }
};
