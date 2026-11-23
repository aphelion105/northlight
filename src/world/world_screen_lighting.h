#pragma once
#include "effects_buckets.h"
#include "gpu_profile.h"
#include "probe_local_visibility.h"
#include "screen_quad.h"
#include "world_compiled_shaders.h"
#include "world_diagnostics.h"
#include "world_lighting_capture.h"
#include <optional>

// Direct light and GI write raw_. Temporal resolve consumes it before local
// lighting reuses it. Baseline and prelit daylight remain live for composition.
class NorthlightWorldScreenLighting {
    friend struct WorldScreenEffectsTest;
    friend struct PointTerrainCoverageTest;

public:
    struct Image {
        IDirect3DTexture9* texture=nullptr;
        IDirect3DSurface9* surface=nullptr;
    };
    struct Probes {
        bool requested;
        float gridActive;
        IDirect3DTexture9 *visibility,*previous,*current,*regionalFog;
    };
    struct Input {
        UINT width,height;
        unsigned rendererFrame;
        int firstSource;
        const float (&sourceWeights)[2];
        bool applyDirectionalShadows,softResolved,prelitDaylight;
        IDirect3DPixelShader9* resolvedLighting;
        IDirect3DTexture9* directionalVisibility;
        Probes probes;
        IDirect3DTexture9* material;
        const float (&bumpControl)[4];
        const float (&materialLight)[4][4];
    };
    struct Audit {
        const std::string* directory;
        NorthlightLightingCapture::Capture* lighting;
        unsigned worldFrame;
        uint64_t worldSerial,atlasSerial,request,generation;
        const float *sunDirection,*sunIrradiance,*skyRadiance;
    };
    struct Source {
        IDirect3DTexture9 *nearMap,*farMap;
        const float *receiver,*nearMatrix,*farMatrix,*direction,*color,*weights;
    };
    struct FrameInput {
        UINT width,height;
        bool prelit,materialOcclusion;
        float occlusionDistance;
        bool hdrSkyBase,importedAtmosphere;
    };

    NorthlightWorldScreenLighting()=default;
    NorthlightWorldScreenLighting(const NorthlightWorldScreenLighting&)=delete;
    NorthlightWorldScreenLighting& operator=(const NorthlightWorldScreenLighting&)=delete;
    ~NorthlightWorldScreenLighting(){releaseAuxiliaryTargets();releaseRawTarget();releaseShaders();}

    const Image& raw()const{return raw_;}
    const Image& baseline()const{return baseline_;}
    const Image& prelit()const{return prelit_;}
    IDirect3DPixelShader9* directionalShader()const{return directional_;}
    IDirect3DPixelShader9* giShader()const{return gi_;}

    // Publish the shared screen inputs before any pass borrows their slots.
    template<class Device>
    static void bindFrameBank(Device* d,const float (&rows)[68][4],const float (&shadowTuning)[4],
                              IDirect3DTexture9* const (&textures)[14]){
        d->SetPixelShaderConstantF(0,rows[0],68);
        d->SetPixelShaderConstantF(115,shadowTuning,1);
        for(UINT slot=0;slot<14;++slot){
            d->SetTexture(slot,textures[slot]);
            d->SetSamplerState(slot,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
            d->SetSamplerState(slot,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
            const DWORD filter=slot==0||slot==9?D3DTEXF_LINEAR:D3DTEXF_POINT;
            d->SetSamplerState(slot,D3DSAMP_MINFILTER,filter);
            d->SetSamplerState(slot,D3DSAMP_MAGFILTER,filter);
            d->SetSamplerState(slot,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
            d->SetSamplerState(slot,D3DSAMP_SRGBTEXTURE,FALSE);
        }
    }

    // Fog binds its actor maps between the light color and source weights.
    template<class Device,class FogInputs>
    static void bindSource(Device* d,const Source& source,const FogInputs& fogInputs){
        d->SetPixelShaderConstantF(2,source.receiver,1);
        d->SetTexture(2,source.nearMap);
        d->SetTexture(3,source.farMap);
        d->SetPixelShaderConstantF(7,source.nearMatrix,4);
        d->SetPixelShaderConstantF(11,source.farMatrix,4);
        d->SetPixelShaderConstantF(16,source.direction,1);
        d->SetPixelShaderConstantF(17,source.color,1);
        if(const auto* fog=fogInputs())d->SetPixelShaderConstantF(21,fog,1);
        d->SetPixelShaderConstantF(27,source.weights,1);
    }

    template<class Device>
    static void bindDistanceOnly(Device* d,IDirect3DTexture9* neutralZero,IDirect3DTexture9* ao,float probeSpacing){
        d->SetTexture(12,neutralZero);
        const float noNightLight[]={0,0,0,probeSpacing};
        d->SetPixelShaderConstantF(19,noNightLight,1);
        d->SetTexture(10,ao);
        d->SetSamplerState(10,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);
        d->SetSamplerState(10,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
    }

    template<class Pixel> bool createDirectional(const Pixel& pixel){
        return pixel(kWorldLightingShader,directional_,"world lighting shader");
    }
    template<class Pixel> bool createGi(const Pixel& pixel){
        return pixel(kWorldGIShader,gi_,"world GI shader");
    }
    template<class Allocate> bool createTargets(const Allocate& image){
        return image(D3DFMT_A16B16G16R16F,baseline_.texture,baseline_.surface)&&
               image(D3DFMT_A16B16G16R16F,raw_.texture,raw_.surface);
    }

    template<class Admit>
    bool prepareFrame(IDirect3DDevice9* d,const FrameInput& in,const Admit& admit){
        d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE);
        d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE);
        d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);
        const bool prelit=in.prelit&&createPrelit(d,in.width,in.height,admit);
        const float occlusionInfo[]={in.materialOcclusion?1.f:0.f,in.occlusionDistance,
                                     in.hdrSkyBase?1.f:0.f,in.importedAtmosphere?1.f:0.f};
        d->SetPixelShaderConstantF(78,occlusionInfo,1);
        return prelit;
    }

    // An optional MRT for a proven prelit receiver. A failed attempt leaves
    // its authored lighting intact and resumes after the retry interval.
    template<class Admit>
    bool createPrelit(IDirect3DDevice9* d,UINT width,UINT height,const Admit& admit){
        if(prelit_.surface)return true;
        const DWORD now=GetTickCount();
        if(prelitRetryAt_&&LONG(now-prelitRetryAt_)<0)return false;
        prelitRetryAt_=now+1000;
        D3DCAPS9 caps={};
        if(FAILED(d->GetDeviceCaps(&caps))||caps.NumSimultaneousRTs<3||!admit())return false;
        HRESULT hr=d->CreateTexture(width/2,height/2,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,
                                    D3DPOOL_DEFAULT,&prelit_.texture,nullptr);
        if(SUCCEEDED(hr)&&prelit_.texture)hr=prelit_.texture->GetSurfaceLevel(0,&prelit_.surface);
        if(SUCCEEDED(hr)&&prelit_.surface){prelitRetryAt_=0;return true;}
        release(prelit_);
        return false;
    }
    void releaseAuxiliaryTargets(){release(baseline_);release(prelit_);prelitRetryAt_=0;}
    void releaseRawTarget(){release(raw_);}
    void releaseShaders(){
        drop(directional_);
        drop(unshadowed_);
        unshadowedRefused_=false;
        drop(unshadowedGi_);
        unshadowedGiRefused_=false;
        drop(gi_);
    }

    template<class Check,class BindSource,class Report>
    bool draw(IDirect3DDevice9* d,const Input& in,const Audit& audit,
              NorthlightGpuProfile* profile,NorthlightEffectsBuckets::Frame* buckets,
              const Check& check,const BindSource& bindSource,const Report& report){
        const bool giRequested=in.probes.requested;
        if(!in.applyDirectionalShadows)
            createOptional(d,kWorldLightingUnshadowedShader,unshadowed_,unshadowedRefused_);
        const bool unshadowedLighting=!in.applyDirectionalShadows&&unshadowed_;
        if(unshadowedLighting&&giRequested)
            createOptional(d,kWorldLightingUnshadowedGIShader,unshadowedGi_,unshadowedGiRefused_);
        // The fused fallback has no packed cascade reads.
        const bool mergedGI=unshadowedLighting&&giRequested&&unshadowedGi_&&in.probes.gridActive<.5f;
        std::optional<NorthlightLightingCapture::Capture> giAudit;
        if(audit.directory)giAudit.emplace(d,*audit.directory,in.rendererFrame,audit.worldFrame,
            in.rendererFrame,in.width,in.height,"frame-parity-gi-inputs");
        const auto captureGiInputs=[&]{
            if(!giAudit)return;
            const bool resident=in.probes.gridActive>.5f;
            giAudit->parityGiInputs(giRequested,mergedGI,resident,audit.worldSerial,audit.atlasSerial);
            if(resident&&audit.sunDirection)
                giAudit->parityProbeRadiance(audit.request,audit.generation,audit.sunDirection,audit.sunIrradiance,audit.skyRadiance);
        };

        const auto localLayout=NorthlightGI::LocalProbeVisibility::atlas(
            NorthlightGI::probeLayout().atlas,NorthlightGI::ProbeCascadeCount);
        const float cascadeInfo[]={0,0,0,1};
        d->SetPixelShaderConstantF(83,cascadeInfo,1);
        const float localInfo[]={giRequested&&in.probes.visibility?float(localLayout.columns):0.f,
            1.f/localLayout.width,1.f/localLayout.height,1.f/localLayout.columns};
        d->SetPixelShaderConstantF(82,localInfo,1);
        if(giRequested)d->SetTexture(13,in.probes.visibility);

        d->SetRenderTarget(0,raw_.surface);
        d->SetPixelShader(unshadowedLighting?unshadowed_:in.softResolved?in.resolvedLighting:directional_);
        d->SetTexture(15,in.softResolved?in.directionalVisibility:nullptr);
        if(in.softResolved){
            for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})d->SetSamplerState(15,state,D3DTEXF_POINT);
            d->SetSamplerState(15,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
            d->SetSamplerState(15,D3DSAMP_SRGBTEXTURE,FALSE);
            d->SetSamplerState(15,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
            d->SetSamplerState(15,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
        }
        report(unshadowedLighting);
        d->SetPixelShaderConstantF(74,in.materialLight[0],4);
        d->SetPixelShaderConstantF(73,in.bumpControl,1);
        d->SetTexture(9,in.material);
        for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})d->SetSamplerState(9,state,D3DTEXF_POINT);

        auto* const prelitSurface=in.prelitDaylight?prelit_.surface:nullptr;
        if(!check(d->SetRenderTarget(2,prelitSurface),"prelit daylight target"))return false;
        d->SetRenderState(D3DRS_COLORWRITEENABLE2,15);
        const auto quad=[&]{
            D3DVIEWPORT9 viewport={0,0,in.width/2,in.height/2,0,1};
            d->SetViewport(&viewport);
            return NorthlightScreen::drawQuad(d,in.width/2,in.height/2);
        };
        bool first=true;
        const int lastSource=in.sourceWeights[1]<=0&&in.firstSource!=1?0:1;
        // A strength-zero first source still subtracts its authored share.
        for(int source=0;source<2;++source){
            if(in.sourceWeights[source]<=0&&source!=in.firstSource)continue;
            bindSource(source,first);
            d->SetRenderState(D3DRS_ALPHABLENDENABLE,!first);
            if(mergedGI){
                d->SetTexture(8,in.probes.previous);
                d->SetPixelShader(source==lastSource?unshadowedGi_:unshadowed_);
            }
            if(!check(d->SetRenderTarget(1,first?baseline_.surface:nullptr),"baseline MRT"))return false;
            d->SetRenderState(D3DRS_COLORWRITEENABLE1,15);
            if(mergedGI&&source==lastSource)captureGiInputs();
            if(!check(quad(),"celestial shadows pass"))return false;
            first=false;
        }
        d->SetRenderTarget(1,nullptr);
        d->SetRenderTarget(2,nullptr);
        if(mergedGI)d->SetTexture(8,in.probes.current);
        d->SetRenderState(D3DRS_COLORWRITEENABLE,15);
        d->SetTexture(12,baseline_.texture);
        d->SetTexture(15,nullptr);
        if(profile)profile->mark(mergedGI?"WorldLightingGI":"WorldLighting");
        if(buckets)buckets->mark(NorthlightEffectsBuckets::Lighting);
        if(audit.directory){
            NorthlightWorldDiagnostics::product(d,raw_.surface,*audit.directory,"history-pre-gi-light",
                in.rendererFrame,!mergedGI,in.width/2,in.height/2,D3DFMT_A16B16G16R16F,in.rendererFrame);
            logf("GI_PARITY present=%u requested=%u resident=%u fused=%u worldSerial=%llu atlasSerial=%llu",
                in.rendererFrame,unsigned(giRequested),unsigned(in.probes.gridActive>.5f),unsigned(mergedGI),
                (unsigned long long)audit.worldSerial,(unsigned long long)audit.atlasSerial);
        }
        if(giRequested&&!mergedGI){
            d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);
            d->SetRenderState(D3DRS_COLORWRITEENABLE,7);
            d->SetPixelShader(gi_);
            // RGB carries added bounce; alpha carries resident-probe occlusion.
            if(!check(d->SetRenderTarget(1,prelitSurface),"prelit GI MRT"))return false;
            d->SetRenderState(D3DRS_COLORWRITEENABLE1,15);
            d->SetTexture(8,in.probes.previous);
            if(audit.lighting)audit.lighting->giProbeInputs();
            captureGiInputs();
            if(!check(quad(),"world GI pass"))return false;
            d->SetRenderTarget(1,nullptr);
            d->SetTexture(8,in.probes.current);
        }
        if(profile)profile->mark("GI");
        if(!giRequested)captureGiInputs();
        if(giAudit)giAudit->finish(true);
        if(giRequested)d->SetTexture(13,in.probes.regionalFog);
        return true;
    }

private:
    Image raw_,baseline_,prelit_;
    IDirect3DPixelShader9 *directional_=nullptr,*unshadowed_=nullptr,*unshadowedGi_=nullptr,*gi_=nullptr;
    bool unshadowedRefused_=false,unshadowedGiRefused_=false;
    DWORD prelitRetryAt_=0;

    template<class T> static void drop(T*& pointer){if(pointer){pointer->Release();pointer=nullptr;}}
    static void release(Image& image){drop(image.surface);drop(image.texture);}
    static void createOptional(IDirect3DDevice9* d,const DWORD* code,IDirect3DPixelShader9*& shader,bool& refused){
        if(shader||refused)return;
        IDirect3DPixelShader9* optional=nullptr;
        if(SUCCEEDED(d->CreatePixelShader(code,&optional))&&optional)shader=optional;
        else {drop(optional);refused=true;}
    }
};

