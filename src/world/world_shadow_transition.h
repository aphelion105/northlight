#pragma once
#include "screen_quad.inl"
#include <algorithm>
#include "shadow_transition.h"
#include "shadow_transition_math.h"

// Directional static history and the live caster target used during a fade.
// Included after the device and compiled shader declarations.
class NorthlightWorldShadowTransition {
public:
    struct Slot {
        NorthlightShadowTransition::Slot state;
        NorthlightWorldMath::ShadowFrame working,previous;
        float previousMatrix[16]={};
        bool workingValid=false;
        IDirect3DTexture9 *history=nullptr,*dynamic=nullptr;
        IDirect3DSurface9 *historySurface=nullptr,*dynamicSurface=nullptr;
    };
    struct Crop {
        IDirect3DTexture9 *white,*cache;
        IDirect3DPixelShader9* shader;
        unsigned margin,size;
        long x,y;
        float depthOffset;
    };

private:
    Slot slots_[4];
    IDirect3DPixelShader9 *transitionCurrentPS=nullptr,*transitionPreviousPS=nullptr,*fogFadePS=nullptr,*cloudFadePS=nullptr;
    IDirect3DPixelShader9 *fogActorFadePS=nullptr,*fogActorTiledFadePS=nullptr,*cloudActorFadePS=nullptr,*cloudActorTiledFadePS=nullptr;
    bool failed_=false;
    unsigned failures_=0;

    template<class T> static void drop(T*& pointer){if(pointer){pointer->Release();pointer=nullptr;}}

    void releaseResources(){
        for(auto& f:slots_){drop(f.historySurface);drop(f.history);drop(f.dynamicSurface);drop(f.dynamic);}
        drop(transitionCurrentPS);drop(transitionPreviousPS);drop(fogFadePS);drop(cloudFadePS);
        drop(fogActorFadePS);drop(fogActorTiledFadePS);drop(cloudActorFadePS);drop(cloudActorTiledFadePS);
    }
    template<class Device>
    static bool optionalShadowTarget(Device* d,IDirect3DTexture9*& texture,IDirect3DSurface9*& surface){
        if(texture&&surface)return true;
        IDirect3DTexture9* t=nullptr;IDirect3DSurface9* s=nullptr;
        if(FAILED(d->CreateTexture(1024,1024,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&t,nullptr))||
           !t||FAILED(t->GetSurfaceLevel(0,&s))||!s){drop(s);drop(t);return false;}
        texture=t;surface=s;return true;
    }

public:
    NorthlightWorldShadowTransition()=default;
    NorthlightWorldShadowTransition(const NorthlightWorldShadowTransition&)=delete;
    NorthlightWorldShadowTransition& operator=(const NorthlightWorldShadowTransition&)=delete;
    ~NorthlightWorldShadowTransition(){releaseResources();}

    Slot& operator[](unsigned slot){return slots_[slot];}
    const Slot& operator[](unsigned slot)const{return slots_[slot];}
    const auto& slots()const{return slots_;}
    bool failed()const{return failed_;}
    unsigned failures()const{return failures_;}
    bool active(int source)const{return !failed_&&(slots_[source*2].state.fading()||slots_[source*2+1].state.fading());}
    IDirect3DPixelShader9* directionalShader(bool previous=false)const{return previous?transitionPreviousPS:transitionCurrentPS;}
    IDirect3DPixelShader9* fogShader(bool actor,bool tiled)const{return tiled?fogActorTiledFadePS:actor?fogActorFadePS:fogFadePS;}
    IDirect3DPixelShader9* cloudsShader(bool actor,bool tiled)const{return actor?(tiled?cloudActorTiledFadePS:cloudActorFadePS):cloudFadePS;}
    void reset(){for(auto& f:slots_){f.state.reset();f.workingValid=false;}}
    void release(){reset();releaseResources();failed_=false;}

    template<class Device,class Admit,class AllocationFailed>
    bool prepare(Device* d,int slot,bool pressured,const Admit& admit,const AllocationFailed& onFailure){
        if(failed_||pressured)return false;
        if(slots_[slot].history&&slots_[slot].dynamic&&transitionCurrentPS&&transitionPreviousPS)return true;
        D3DCAPS9 caps{};
        const unsigned required=std::max({kDirectionalShadowTransitionCurrentShaderSlots,kDirectionalShadowTransitionPreviousShaderSlots,kWorldFogTransitionShaderSlots,
            kWorldFogActorTransitionShaderSlots,kWorldFogActorTiledTransitionShaderSlots,kFogCloudsTransitionShaderSlots,
            kFogCloudsActorTransitionShaderSlots,kFogCloudsActorTiledTransitionShaderSlots});
        if(FAILED(d->GetDeviceCaps(&caps))||caps.MaxPixelShader30InstructionSlots<required)return false;
        if(!admit())return false;
        const bool shaders=(transitionCurrentPS||SUCCEEDED(d->CreatePixelShader(kDirectionalShadowTransitionCurrentShader,&transitionCurrentPS)))&&
            (transitionPreviousPS||SUCCEEDED(d->CreatePixelShader(kDirectionalShadowTransitionPreviousShader,&transitionPreviousPS)))&&
            (fogFadePS||SUCCEEDED(d->CreatePixelShader(kWorldFogTransitionShader,&fogFadePS)))&&
            (cloudFadePS||SUCCEEDED(d->CreatePixelShader(kFogCloudsTransitionShader,&cloudFadePS)))&&
            (fogActorFadePS||SUCCEEDED(d->CreatePixelShader(kWorldFogActorTransitionShader,&fogActorFadePS)))&&
            (fogActorTiledFadePS||SUCCEEDED(d->CreatePixelShader(kWorldFogActorTiledTransitionShader,&fogActorTiledFadePS)))&&
            (cloudActorFadePS||SUCCEEDED(d->CreatePixelShader(kFogCloudsActorTransitionShader,&cloudActorFadePS)))&&
            (cloudActorTiledFadePS||SUCCEEDED(d->CreatePixelShader(kFogCloudsActorTiledTransitionShader,&cloudActorTiledFadePS)));
        auto& f=slots_[slot];
        if(shaders&&optionalShadowTarget(d,f.history,f.historySurface)&&optionalShadowTarget(d,f.dynamic,f.dynamicSurface))return true;
        // Allocation failure must retain all active effects and baseline shadows.
        for(auto& item:slots_)if(item.state.fading())item.state.cancel();
        releaseResources();failed_=true;++failures_;
        onFailure();return false;
    }
    // Save the exact old 1024 working crop, excluding its old dynamic casters.
    // The existing union is used with a white dynamic input: no new caster draw.
    template<class Device>
    bool preserveStatic(Device* d,int slot,const Crop& crop){
        auto& f=slots_[slot];
        bool ok=SUCCEEDED(d->SetDepthStencilSurface(nullptr));
        auto set=[&](HRESULT result){ok=SUCCEEDED(result)&&ok;};
        for(unsigned s=0;s<16;++s)set(d->SetTexture(s,nullptr));
        set(d->SetRenderTarget(0,f.historySurface));
        set(d->SetRenderState(D3DRS_ZENABLE,FALSE));set(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));
        set(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));set(d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE));
        set(d->SetVertexShader(nullptr));set(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));set(d->SetStreamSourceFreq(0,1));
        set(d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0));set(d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE));
        set(d->SetTexture(0,crop.white));set(d->SetTexture(1,crop.cache));
        for(unsigned s=0;s<2;++s){set(d->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));set(d->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));
            set(d->SetSamplerState(s,D3DSAMP_MINFILTER,D3DTEXF_POINT));set(d->SetSamplerState(s,D3DSAMP_MAGFILTER,D3DTEXF_POINT));
            set(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));set(d->SetSamplerState(s,D3DSAMP_SRGBTEXTURE,FALSE));}
        float c[2][4]={{float(long(crop.margin)+crop.x),float(long(crop.margin)-crop.y),crop.depthOffset,float(crop.size)},
                       {1.f/1024,1.f/float(crop.size),0,0}};
        ok=ok&&SUCCEEDED(d->SetPixelShaderConstantF(0,&c[0][0],2))&&SUCCEEDED(d->SetPixelShader(crop.shader))&&SUCCEEDED(NorthlightScreen::drawViewportQuad(d,1024,1024));
        set(d->SetTexture(0,nullptr));set(d->SetTexture(1,nullptr));
        set(d->SetRenderState(D3DRS_ZENABLE,TRUE));set(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));
        set(d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR));set(d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR));
        // The caller now renders the new static cache. Its alpha-tested caster
        // textures repeat; only the history crop above uses clamped addressing.
        set(d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP));set(d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP));
        if(ok){f.previous=f.working;NorthlightWorldMath::shadowMatrixFrom(f.previous,f.previous.texel*512.f,f.previousMatrix);}
        return ok;
    }
    // c80..100 and s6..9 belong only to the transition variants. Other passes
    // keep their original sampler bindings, including the GI probe atlas.
    template<class Device>
    void bind(Device* d,int source,bool previousEpoch,const float (&matrices)[2][16],
              const NorthlightShadowTransition::Direction& fallback,IDirect3DTexture9* white,bool soft){
        const int first=source*2;float info[4]={1,1,0,0};
        for(int cascade=0;cascade<2;++cascade){
            const auto& f=slots_[first+cascade];const bool fade=f.state.fading();
            const float* previous=fade?f.previousMatrix:matrices[cascade];
            d->SetPixelShaderConstantF(80+cascade*4,previous,4);
            const auto oldDirection=fade?f.state.previous():f.state.valid()?f.state.current():fallback;
            float direction[4]={oldDirection.x,oldDirection.y,oldDirection.z,0};d->SetPixelShaderConstantF(cascade?98:88,direction,1);
            float reproject[16]={};
            if(fade)ShadowTransitionMath::reproject(f.previous,matrices[cascade],f.previous.texel*512.f,reproject);
            else for(unsigned i=0;i<4;++i)reproject[i*5]=1;
            d->SetPixelShaderConstantF(90+cascade*4,reproject,4);
            d->SetTexture(6+cascade,fade?f.history:white);d->SetTexture(8+cascade,fade?f.dynamic:white);
            info[cascade]=f.state.weight();
        }
        info[2]=soft?1.f:0.f;info[3]=previousEpoch?1.f:0.f;
        d->SetPixelShaderConstantF(89,info,1);
        d->SetPixelShaderConstantF(105,matrices[0],4);
        d->SetPixelShaderConstantF(109,matrices[1],4);
        const auto nearDirection=slots_[first].state.valid()?slots_[first].state.current():fallback;
        const float currentNear[4]={nearDirection.x,nearDirection.y,nearDirection.z,0};d->SetPixelShaderConstantF(113,currentNear,1);
        if(previousEpoch){
            d->SetPixelShaderConstantF(7,slots_[first].state.fading()?slots_[first].previousMatrix:matrices[0],4);
            d->SetPixelShaderConstantF(11,slots_[first+1].state.fading()?slots_[first+1].previousMatrix:matrices[1],4);
            const auto oldNear=slots_[first].state.fading()?slots_[first].state.previous():nearDirection;
            const float old[4]={oldNear.x,oldNear.y,oldNear.z,source==1?1.f:0.f};d->SetPixelShaderConstantF(16,old,1);
        }
        const auto currentFarDirection=slots_[first+1].state.valid()?slots_[first+1].state.current():fallback;
        const float currentFar[4]={currentFarDirection.x,currentFarDirection.y,currentFarDirection.z,0};d->SetPixelShaderConstantF(114,currentFar,1);
        const auto farDirection=previousEpoch&&slots_[first+1].state.fading()?slots_[first+1].state.previous():slots_[first+1].state.valid()?slots_[first+1].state.current():fallback;
        float direction[4]={farDirection.x,farDirection.y,farDirection.z,0};d->SetPixelShaderConstantF(99,direction,1);
        float oldInfo[4]={NorthlightWorldMath::ShadowBiasWorld*NorthlightWorldMath::InverseShadowDepth,
            NorthlightWorldMath::ShadowBiasWorld*NorthlightWorldMath::InverseShadowDepth,
            NorthlightWorldMath::InverseShadowDepth,NorthlightWorldMath::InverseShadowDepth};
        d->SetPixelShaderConstantF(100,oldInfo,1);
        const auto& farSlot=slots_[first+1];float inverse[16]={};
        if(farSlot.state.fading())ShadowTransitionMath::reproject(farSlot.working,farSlot.previousMatrix,farSlot.working.texel*512.f,inverse);
        else for(unsigned i=0;i<4;++i)inverse[i*5]=1;
        d->SetPixelShaderConstantF(101,inverse,4);
        for(unsigned s=6;s<10;++s){d->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
            d->SetSamplerState(s,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(s,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
            d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(s,D3DSAMP_SRGBTEXTURE,FALSE);}
    }
};
