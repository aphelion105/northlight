#pragma once
#include "screen_quad.h"
#include "world_compiled_shaders.h"
#include <d3d9.h>

// Owns the optional half-resolution visibility pass and its producer/consumer
// shader pairs. Actor tiles and cascade history are borrowed for the draw.
class NorthlightWorldDirectionalVisibility {
    friend struct WorldScreenEffectsTest;
    IDirect3DTexture9* texture_=nullptr;
    IDirect3DSurface9* surface_=nullptr;
    IDirect3DPixelShader9 *filter_=nullptr,*lighting_=nullptr;
    IDirect3DPixelShader9 *sharedFilter_=nullptr,*sharedLighting_=nullptr;
    bool sharedTried_=false;
    DWORD retryAt_=0;
    unsigned failures_=0;

    template<class T> static void drop(T*& pointer){if(pointer){pointer->Release();pointer=nullptr;}}

public:
    struct Source {
        bool enabled=false,actor=false,fading=false;
        const float* actorMatrix=nullptr;
        IDirect3DTexture9* actorDepth=nullptr;
        IDirect3DTexture9* actorTiles=nullptr;
    };
    struct Resolve {
        UINT width,height;
        bool shared,transition;
        const float* actorInfo;
        IDirect3DPixelShader9* current;
        IDirect3DPixelShader9* previous;
        Source sources[2];
    };

    NorthlightWorldDirectionalVisibility()=default;
    NorthlightWorldDirectionalVisibility(const NorthlightWorldDirectionalVisibility&)=delete;
    NorthlightWorldDirectionalVisibility& operator=(const NorthlightWorldDirectionalVisibility&)=delete;
    ~NorthlightWorldDirectionalVisibility(){reset();}

    IDirect3DTexture9* texture()const{return texture_;}
    IDirect3DSurface9* surface()const{return surface_;}
    IDirect3DPixelShader9* filter(bool shared)const{return shared?sharedFilter_:filter_;}
    IDirect3DPixelShader9* lighting(bool shared)const{return shared?sharedLighting_:lighting_;}
    template<class Device>
    static void restoreReceiverMetadata(Device* d,IDirect3DTexture9* metadata){
        d->SetTexture(10,metadata);
    }

    void reset(){
        drop(surface_);drop(texture_);drop(filter_);drop(lighting_);retryAt_=0;
        drop(sharedFilter_);drop(sharedLighting_);sharedTried_=false;
    }

    bool prepareShared(IDirect3DDevice9* d){
        if(!sharedTried_){
            sharedTried_=true;D3DCAPS9 caps={};
            if(SUCCEEDED(d->GetDeviceCaps(&caps))&&caps.MaxPixelShader30InstructionSlots>=kUnifiedDirectionalShadowShaderSlots){
                if(FAILED(d->CreatePixelShader(kUnifiedDirectionalShadowShader,&sharedFilter_))||
                   FAILED(d->CreatePixelShader(kWorldLightingSharedShader,&sharedLighting_))){drop(sharedFilter_);drop(sharedLighting_);}
            }
        }
        return sharedFilter_&&sharedLighting_;
    }

    template<class Admit,class InvalidateTransitions,class Report>
    bool prepare(IDirect3DDevice9* d,UINT w,UINT h,const Admit& admit,
                 const InvalidateTransitions& invalidateTransitions,const Report& report){
        if(surface_&&filter_&&lighting_)return true;
        if(retryAt_&&LONG(GetTickCount()-retryAt_)<0)return false;
        if(!admit()){retryAt_=GetTickCount()+1000;return false;}
        HRESULT hr=d->CreatePixelShader(kDirectionalShadowShader,&filter_);
        if(SUCCEEDED(hr))hr=d->CreatePixelShader(kWorldLightingSoftShader,&lighting_);
        if(SUCCEEDED(hr))hr=d->CreateTexture(w/2,h/2,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&texture_,nullptr);
        if(SUCCEEDED(hr)&&texture_)hr=texture_->GetSurfaceLevel(0,&surface_);
        if(SUCCEEDED(hr)&&surface_)return true;
        invalidateTransitions();
        reset();retryAt_=GetTickCount()+1000;
        if(failures_++<4)report(hr);
        return false;
    }

    template<class Device,class BindSource,class BindTransition>
    bool resolve(Device* d,const Resolve& input,IDirect3DTexture9* const* textures,
                 const BindSource& bindSource,const BindTransition& bindTransition){
        // Unbind the previous input before writing the same texture. Restore
        // the transition's borrowed samplers even after a failed optional draw.
        const bool resolved=SUCCEEDED(d->SetTexture(15,nullptr))&&SUCCEEDED(d->SetRenderTarget(0,surface_))&&
            SUCCEEDED(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xffffffff,1,0))&&SUCCEEDED(d->SetPixelShader(filter(input.shared)))&&
            drawSources(d,input,bindSource,bindTransition);
        if(input.transition)for(unsigned sampler=6;sampler<10;++sampler){
            d->SetTexture(sampler,textures[sampler]);const DWORD filter=sampler==9?D3DTEXF_LINEAR:D3DTEXF_POINT;
            d->SetSamplerState(sampler,D3DSAMP_MINFILTER,filter);d->SetSamplerState(sampler,D3DSAMP_MAGFILTER,filter);
        }
        if(input.transition)d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
        d->SetRenderState(D3DRS_COLORWRITEENABLE,15);
        return resolved;
    }

private:
    template<class Device,class BindSource,class BindTransition>
    static bool drawSources(Device* d,const Resolve& input,const BindSource& bindSource,
                            const BindTransition& bindTransition){
        for(int source=0;source<2;++source){
            const auto& item=input.sources[source];
            if(!item.enabled)continue;
            bindSource(source);
            if(input.shared){
                const float info[4]={item.actor?1.f:0.f,input.actorInfo[1],input.actorInfo[2],0.f};
                if(FAILED(d->SetPixelShaderConstantF(68,item.actorMatrix,4))||FAILED(d->SetPixelShaderConstantF(72,info,1))||
                   FAILED(d->SetTexture(12,item.actorDepth))||FAILED(d->SetTexture(10,item.actorTiles)))return false;
            }
            if(input.transition){
                bindTransition(source,false);
                d->SetPixelShader(input.current);
                d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
            }
            if(FAILED(d->SetRenderState(D3DRS_COLORWRITEENABLE,source?D3DCOLORWRITEENABLE_GREEN:D3DCOLORWRITEENABLE_RED))||
               FAILED(NorthlightScreen::drawViewportQuad(d,input.width,input.height)))return false;
            if(item.fading){
                bindTransition(source,true);
                d->SetPixelShader(input.previous);
                d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);
                d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE);
                d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE);
                if(FAILED(NorthlightScreen::drawViewportQuad(d,input.width,input.height)))return false;
            }
        }
        return true;
    }
};
