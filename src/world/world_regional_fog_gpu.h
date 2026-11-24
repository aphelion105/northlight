#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <d3d9.h>
#include "fog_clouds.h"
#include "world_compiled_shaders.h"

class NorthlightRegionalFogGpu {
public:
    NorthlightRegionalFogGpu()=default;
    NorthlightRegionalFogGpu(const NorthlightRegionalFogGpu&)=delete;
    NorthlightRegionalFogGpu& operator=(const NorthlightRegionalFogGpu&)=delete;
    ~NorthlightRegionalFogGpu(){reset();}

    IDirect3DPixelShader9* base() const {return base_;}
    IDirect3DPixelShader9* clouds() const {return clouds_;}
    IDirect3DPixelShader9* actor() const {return actor_;}
    IDirect3DPixelShader9* actorClouds() const {return actorClouds_;}
    IDirect3DPixelShader9* ambientClouds() const {return ambientClouds_;}
    bool actorVariants() const {return actor_&&actorClouds_;}
    IDirect3DVolumeTexture9* noise() const {return noise_;}

    template<class ShaderFactory> bool createBase(const ShaderFactory& pixel){
        return pixel(kWorldFogShader,base_,"volume shader");
    }

    void createOptional(IDirect3DDevice9* d){
        D3DCAPS9 caps={};
        const bool extended=SUCCEEDED(d->GetDeviceCaps(&caps))&&
            caps.MaxPixelShader30InstructionSlots>=std::max(kWorldFogActorShaderSlots,kFogCloudsActorShaderSlots);
        if(extended&&(!create(d,kWorldFogActorShader,actor_)||!create(d,kFogCloudsActorShader,actorClouds_))){
            drop(actor_);
            drop(actorClouds_);
        }
        create(d,kFogCloudsShader,clouds_);
    }

    bool ensureAmbientClouds(IDirect3DDevice9* d){
        if(ambientCloudsRefused_)return false;
        ambientCloudsRefused_=!create(d,kFogCloudsAmbientShader,ambientClouds_);
        return !ambientCloudsRefused_;
    }

    IDirect3DPixelShader9* volumeShader(IDirect3DDevice9* d,bool ambientOnly,bool actor,IDirect3DPixelShader9* tiled=nullptr){
        if(!ambientOnly)return actor?(tiled?tiled:actor_):base_;
        if(!ambientRefused_)ambientRefused_=!create(d,kWorldFogAmbientShader,ambient_);
        return ambient_?ambient_:base_;
    }

    template<class Admit,class Log>
    bool ensureNoise(IDirect3DDevice9* d,const uint8_t* data,bool diagnostics,const Admit& admit,const Log& log){
        if(noise_)return true;
        if(noiseFailed_||!data)return false;
        const DWORD now=GetTickCount();
        if(noiseRetry_&&DWORD(now-noiseRetryAt_)<1000)return false;
        const bool report=!noiseReported_||DWORD(now-noiseReportAt_)>=60000;
        if(!admit(diagnostics&&report)){
            deferNoise(now);
            if(diagnostics&&report)noteNoiseReport(now);
            return false;
        }

        constexpr unsigned N=NorthlightFogClouds::N;
        IDirect3DVolumeTexture9* texture=nullptr;
        bool l8=true;
        HRESULT hr=d->CreateVolumeTexture(N,N,N,1,0,D3DFMT_L8,D3DPOOL_MANAGED,&texture,nullptr);
        if((FAILED(hr)||!texture)&&!outOfMemory(hr)){
            drop(texture);
            l8=false;
            hr=d->CreateVolumeTexture(N,N,N,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,nullptr);
        }
        D3DLOCKED_BOX box={};
        if(SUCCEEDED(hr)&&texture)hr=texture->LockBox(0,&box,nullptr,0);
        if(FAILED(hr)||!texture||!box.pBits){
            drop(texture);
            if(outOfMemory(hr))deferNoise(now);
            else noiseFailed_=true;
            if(noiseFailed_||report){
                noteNoiseReport(now);
                log("WORLD fog clouds %s: noise volume HRESULT=%08lx",noiseFailed_?"disabled":"deferred",(unsigned long)hr);
            }
            return false;
        }
        for(unsigned z=0;z<N;++z)for(unsigned y=0;y<N;++y){
            const uint8_t* src=data+size_t(N)*(y+size_t(N)*z);
            auto* row=static_cast<uint8_t*>(box.pBits)+size_t(z)*box.SlicePitch+size_t(y)*box.RowPitch;
            if(l8)std::memcpy(row,src,N);
            else for(unsigned x=0;x<N;++x)reinterpret_cast<uint32_t*>(row)[x]=0xff000000u|src[x]*0x010101u;
        }
        texture->UnlockBox(0);
        noise_=texture;
        noiseRetry_=false;
        log("WORLD fog clouds noise volume %ux%ux%u %s",N,N,N,l8?"L8":"A8R8G8B8");
        return true;
    }

    void reset(){
        drop(base_);
        drop(ambient_);
        ambientRefused_=false;
        drop(ambientClouds_);
        ambientCloudsRefused_=false;
        drop(actor_);
        drop(actorClouds_);
        drop(clouds_);
        // The device's noise format refusal/backoff survives screen resource resets.
        drop(noise_);
    }

private:
    friend struct WorldScreenEffectsTest;
    template<class T> static void drop(T*& resource){if(resource){resource->Release();resource=nullptr;}}
    static bool create(IDirect3DDevice9* d,const DWORD* code,IDirect3DPixelShader9*& shader){
        if(shader)return true;
        if(FAILED(d->CreatePixelShader(code,&shader)))drop(shader);
        return shader!=nullptr;
    }
    static bool outOfMemory(HRESULT hr){return hr==E_OUTOFMEMORY||hr==D3DERR_OUTOFVIDEOMEMORY;}
    void deferNoise(DWORD now){noiseRetry_=true;noiseRetryAt_=now;}
    void noteNoiseReport(DWORD now){noiseReported_=true;noiseReportAt_=now;}

    IDirect3DPixelShader9 *base_=nullptr,*actor_=nullptr,*actorClouds_=nullptr,*clouds_=nullptr;
    IDirect3DPixelShader9 *ambient_=nullptr,*ambientClouds_=nullptr;
    bool ambientRefused_=false,ambientCloudsRefused_=false;
    IDirect3DVolumeTexture9* noise_=nullptr;
    bool noiseFailed_=false,noiseRetry_=false,noiseReported_=false;
    DWORD noiseRetryAt_=0,noiseReportAt_=0;
};
