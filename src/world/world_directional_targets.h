#pragma once
#include "screen_quad.h"
#include "world_render_target.h"
#include <d3d9.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

class NorthlightWorldDirectionalTargets {
public:
    static constexpr UINT Size=1024;
    static constexpr UINT CacheMargin=128;
    static constexpr UINT CacheSize=Size+2*CacheMargin;
    struct Target {
        IDirect3DTexture9* texture=nullptr;
        IDirect3DSurface9* surface=nullptr;
    };

    const Target& current(unsigned slot) const {return current_[slot];}
    const Target& cache(unsigned slot) const {return cache_[slot];}
    const Target& scratch() const {return scratch_;}
    const Target& nearEnvironment(unsigned source) const {return near_[source];}
    IDirect3DSurface9* depth() const {return depth_;}
    IDirect3DSurface9* cacheDepth() const {return cacheDepth_;}

    template<class AllocateTarget,class AllocateDepth>
    bool create(const AllocateTarget& target,const AllocateDepth& depth){
        for(auto& map:current_)if(!target(Size,Size,D3DFMT_R32F,&map.texture,&map.surface))return false;
        if(!depth(Size,Size,&depth_,"shadow depth"))return false;
        for(auto& map:cache_)if(!target(CacheSize,CacheSize,D3DFMT_R32F,&map.texture,&map.surface))return false;
        return depth(CacheSize,CacheSize,&cacheDepth_,"shadow cache depth")&&
               target(Size,Size,D3DFMT_R32F,&scratch_.texture,&scratch_.surface);
    }

    void releaseMaps(){
        for(auto& map:current_)release(map);
        for(auto& map:cache_)release(map);
        drop(depth_);
        drop(cacheDepth_);
        release(scratch_);
        release(verification_);
        for(auto*& read:readback_)drop(read);
    }

    void releaseNear(){
        for(auto& map:near_)release(map);
        nearRetry_[0]=nearRetry_[1]=0;
    }

    template<class Admit,class Invalidate>
    bool prepareNear(IDirect3DDevice9* d,unsigned source,const Admit& admit,const Invalidate& invalidate){
        auto& map=near_[source];
        auto& retry=nearRetry_[source];
        if(map.surface)return true;
        if(retry&&LONG(GetTickCount()-retry)<0)return false;
        if(!admit()){retry=GetTickCount()+1000;return false;}
        HRESULT hr=d->CreateTexture(Size,Size,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&map.texture,nullptr);
        if(SUCCEEDED(hr))hr=map.texture->GetSurfaceLevel(0,&map.surface);
        invalidate();
        if(SUCCEEDED(hr))return true;
        release(map);
        retry=GetTickCount()+1000;
        return false;
    }

    template<class Texture,class Surface>
    void visit(const Texture& texture,const Surface& surface) const {
        for(const auto& map:current_)texture(map.texture);
        for(const auto& map:cache_)texture(map.texture);
        for(const auto& map:near_)texture(map.texture);
        texture(scratch_.texture);
        texture(verification_.texture);
        surface(depth_);
        surface(cacheDepth_);
        for(auto* read:readback_)surface(read);
    }

    template<class Check,class Render>
    bool verify(IDirect3DDevice9* d,unsigned slot,const Check& check,const Render& render,
                unsigned long long& differing){
        if(!NorthlightWorldRenderTarget::ensure(d,CacheSize,CacheSize,D3DFMT_R32F,verification_.texture,
                verification_.surface,check,"world render texture","world render surface"))return false;
        for(auto*& read:readback_)
            if(!read&&!check(d->CreateOffscreenPlainSurface(CacheSize,CacheSize,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&read,nullptr),"shadow verify readback"))return false;
        if(render(verification_.surface)<0)return false;
        if(!check(d->GetRenderTargetData(cache(slot).surface,readback_[0]),"shadow verify read")||
           !check(d->GetRenderTargetData(verification_.surface,readback_[1]),"shadow verify read"))return false;
        D3DLOCKED_RECT a{},b{};
        if(SUCCEEDED(readback_[0]->LockRect(&a,nullptr,D3DLOCK_READONLY))){
            if(SUCCEEDED(readback_[1]->LockRect(&b,nullptr,D3DLOCK_READONLY))){
                for(UINT y=0;y<CacheSize;++y){
                    const auto* x=static_cast<const uint32_t*>(a.pBits)+size_t(y)*(a.Pitch/4);
                    const auto* z=static_cast<const uint32_t*>(b.pBits)+size_t(y)*(b.Pitch/4);
                    if(std::memcmp(x,z,CacheSize*4))for(UINT i=0;i<CacheSize;++i)differing+=x[i]!=z[i];
                }
                readback_[1]->UnlockRect();
            }
            readback_[0]->UnlockRect();
        }
        return true;
    }

    template<class Check>
    static bool merge(IDirect3DDevice9* d,IDirect3DPixelShader9* shader,IDirect3DSurface9* target,
                      IDirect3DTexture9* live,IDirect3DTexture9* cached,float x,float y,float dz,UINT size,
                      const Check& check){
        d->SetDepthStencilSurface(nullptr);
        if(!check(d->SetRenderTarget(0,target),"shadow target"))return false;
        d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
        d->SetVertexShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetStreamSourceFreq(0,1);
        d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0);d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);
        d->SetTexture(0,live);d->SetTexture(1,cached);
        for(unsigned sampler=0;sampler<2;++sampler){
            d->SetSamplerState(sampler,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(sampler,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
            d->SetSamplerState(sampler,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(sampler,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
            d->SetSamplerState(sampler,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(sampler,D3DSAMP_SRGBTEXTURE,FALSE);
        }
        const float constants[2][4]={{x,y,dz,float(size)},{1.f/Size,1.f/float(size),0,0}};
        d->SetPixelShaderConstantF(0,constants[0],2);d->SetPixelShader(shader);
        if(!check(NorthlightScreen::drawViewportQuad(d,Size,Size),"shadow union"))return false;
        d->SetTexture(0,nullptr);d->SetTexture(1,nullptr);
        d->SetRenderState(D3DRS_ZENABLE,TRUE);d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);
        d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
        return true;
    }

private:
    friend struct WorldScreenEffectsTest;
    friend struct PipelineOffTest;
    friend struct FlashShadowTest;
    Target current_[4],cache_[4],scratch_,near_[2],verification_;
    IDirect3DSurface9 *depth_=nullptr,*cacheDepth_=nullptr,*readback_[2]={};
    DWORD nearRetry_[2]={};

    template<class T> static void drop(T*& pointer){if(pointer){pointer->Release();pointer=nullptr;}}
    static void release(Target& target){drop(target.surface);drop(target.texture);}
};
