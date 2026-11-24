#pragma once
#include <windows.h>
#include <d3d9.h>
#include <cstdint>
#include "effects_buckets.h"

namespace NorthlightContactAO {
// The frame owner lends the targets and shaders for a single submission.
struct Targets {
    IDirect3DTexture9 *scene, *ao, *aoRaw, *waterMask;
    IDirect3DSurface9 *aoSurface, *aoRawSurface;
};
struct Shaders {
    IDirect3DPixelShader9 *aoPS, *aoOnlyPS, *aoContactBloomPS, *contactBloomPS, *aoBlurPS, *aoBlurNoWaterPS;
};
struct Options {
    unsigned width, height, aoMethod;
    bool contactAO, linearHdr, fold, cpuClockAvailable;
    float bounce, bloom;
};
struct Actor {
    IDirect3DTexture9* texture=nullptr;
    IDirect3DPixelShader9 *prepare=nullptr, *contact=nullptr;
};
struct BlurTiming {
    LONGLONG setup=0,submit=0;
    std::uint64_t frame=0;
    unsigned method=0,width=0,height=0;
    bool pending=false;
};
struct Outputs {
    bool& neutralAOPending;
    bool& aoActorUsed;
    bool& diagnosticAoRaw;
    bool& diagnosticAo;
    BlurTiming& blurTiming;
};

template<class Device,class Profile,class Error>
bool clearNeutral(Device* ext,Profile* gpuProfile,IDirect3DSurface9* aoSurface,bool& neutralAOPending,const Error& error){
    if(error(ext->SetRenderTarget(0,aoSurface),"neutral AO render target"))return false;
    if(error(ext->Clear(0,nullptr,D3DCLEAR_TARGET,D3DCOLOR_ARGB(255,0,0,0),1.f,0),"neutral AO clear"))return false;
    gpuProfile->mark("AOClear");
    neutralAOPending=false;
    return true;
}

template<class CpuScope,class Device,class Profile,class Error,class Quad,class SelectActor,class BindBlurInputs>
bool render(Device* ext,Profile* gpuProfile,NorthlightEffectsBuckets::Frame& effectsBuckets,
            const Targets& targets,const Shaders& shaders,const Options& options,Outputs outputs,
            const Error& error,const Quad& quad,const SelectActor& selectActor,const BindBlurInputs& bindBlurInputs){
    using NorthlightEffectsBuckets::Bucket;
    const auto& [scene,ao,aoRaw,waterMask,aoSurface,aoRawSurface]=targets;
    const auto& [aoPS,aoOnlyPS,aoContactBloomPS,contactBloomPS,aoBlurPS,aoBlurNoWaterPS]=shaders;
    const auto& [width,height,aoMethod,contactAO,linearHdr,fold,cpuClockAvailable,bounce,bloom]=options;
    auto [neutralAOPending,aoActorUsed,diagnosticAoRaw,diagnosticAo,blurTiming]=outputs;
    if (!contactAO) {
        // 0.3.201 (task 18) ContactAO=0: no AO and no AOBlur. A ready world gets the bloom with AO 1 straight into `ao` (ContactBloom);
        // the legacy composite gets a neutral ambient (AO 1, no bounce) from a clear. SetRenderTarget resets the viewport; scissor and depth are off (effectState).
        if (bounce==0.f&&!linearHdr&&bloom!=0.f) {
            if (error(ext->SetRenderTarget(0,aoSurface),"contact bloom render target")) return false;
            ext->SetPixelShader(contactBloomPS);
            if (error(quad(width/2,height/2,false),"contact bloom pass")) return false;
            gpuProfile->mark("ContactBloom");
        } else if(linearHdr&&fold)neutralAOPending=true; // The world already owns a 1x1 neutral AO input.
        else if(!clearNeutral(ext,gpuProfile,aoSurface,neutralAOPending,error))return false;
        effectsBuckets.mark(Bucket::AO);
    } else {
        const bool alphaOnly=linearHdr||bounce!=0.f;
        const Actor actor=selectActor(alphaOnly);
        aoActorUsed=actor.texture!=nullptr;
        if(aoActorUsed){
            ext->SetTexture(4,actor.texture);
            ext->SetSamplerState(4,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);ext->SetSamplerState(4,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
            ext->SetSamplerState(4,D3DSAMP_MINFILTER,D3DTEXF_POINT);ext->SetSamplerState(4,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
            ext->SetSamplerState(4,D3DSAMP_MIPFILTER,D3DTEXF_NONE);ext->SetSamplerState(4,D3DSAMP_SRGBTEXTURE,FALSE);
            // Reuse the existing ping-pong target for normals/contact;
            // no extra model replay or intermediate normal texture is needed.
            if(error(ext->SetRenderTarget(0,aoSurface),"actor AO preparation target"))return false;
            ext->SetPixelShader(actor.prepare);
            if(error(quad(width/2,height/2,false),"actor AO preparation pass"))return false;
            gpuProfile->mark("AOActorPrepare");
        }
        IDirect3DPixelShader9* contactShader=aoActorUsed?actor.contact:(alphaOnly?aoOnlyPS:aoContactBloomPS);
        if (error(ext->SetRenderTarget(0,aoRawSurface),"AO render target")) return false;
        if(aoActorUsed)ext->SetTexture(2,ao);
        ext->SetPixelShader(linearHdr||bounce==0.f?contactShader:aoPS);
        if (error(quad(width/2,height/2,false),"AO pass")) return false;
        if (!linearHdr && bounce!=0.f && aoMethod) {
            // Keep the original fallback colour bounce in RGB; replace only AO.
            // Splitting these kernels keeps GTAO below the ps_3_0 slot limit.
            ext->SetRenderState(D3DRS_COLORWRITEENABLE,D3DCOLORWRITEENABLE_ALPHA);
            ext->SetPixelShader(contactShader);
            const HRESULT aoResult=quad(width/2,height/2,false);
            ext->SetRenderState(D3DRS_COLORWRITEENABLE,15);
            if (error(aoResult,"fallback AO alpha pass")) return false;
        }
        gpuProfile->mark("AO");
        diagnosticAoRaw=true;
        // The preparation target becomes the blur destination below.
        if(aoActorUsed){ext->SetTexture(2,nullptr);ext->SetTexture(4,nullptr);}
        // 0.3.201 (task 18): the raw IGN-rotated AO is denoised by a half-resolution bilateral pass into `ao`,
        // which every consumer (legacy composite, world fold) keeps reading. s2 is released afterwards.
        // Task 43: s0 (LINEAR) carries the raw AO too for the blur's 9-read bilinear early-out (AOBlur never reads Scene); the scene copy is rebound after.
        const bool timeAOBlur=gpuProfile->recording()&&cpuClockAvailable;
        const auto aoBlurFrame=gpuProfile->sampledFrame();
        LONGLONG aoBlurSetup=0,aoBlurSubmit=0;
        {CpuScope timing(timeAOBlur?&aoBlurSetup:nullptr);
         if (error(ext->SetRenderTarget(0,aoSurface),"AO blur render target")) return false;
         bindBlurInputs();ext->SetTexture(2,aoRaw);ext->SetTexture(0,aoRaw);ext->SetPixelShader(!waterMask&&aoBlurNoWaterPS?aoBlurNoWaterPS:aoBlurPS);}
        // Separate state/resource preparation from the draw interval. Both
        // timestamps still include command-stream gaps and dependencies.
        gpuProfile->mark("AOBlurSetup");
        HRESULT aoBlurResult;
        {CpuScope timing(timeAOBlur?&aoBlurSubmit:nullptr);aoBlurResult=quad(width/2,height/2,false);}
        if (error(aoBlurResult,"AO blur pass")) return false;
        diagnosticAo=true;
        ext->SetTexture(2,nullptr);ext->SetTexture(0,scene);
        gpuProfile->mark("AOBlur");effectsBuckets.mark(Bucket::AO);
        if(timeAOBlur)blurTiming={aoBlurSetup,aoBlurSubmit,aoBlurFrame,aoMethod,width/2,height/2,true};
    }
    return true;
}
}
