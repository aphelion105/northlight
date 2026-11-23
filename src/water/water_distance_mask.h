#pragma once
#include "hdr_quad.h"
#include "compiled_shaders.h"

// DXVK exposes R32F to D3D9 samplers as (R,1,1,1). All downstream water
// predicates require positive view distance. A cleared scalar is therefore dry.
class NorthlightWaterDistanceMask {
    IDirect3DDevice9* device;
    IDirect3DTexture9 *distance=nullptr,*coverage=nullptr;
    IDirect3DSurface9 *resolved=nullptr,*coverageResolved=nullptr,*distanceMS=nullptr,*coverageMS=nullptr;
    IDirect3DPixelShader9 *correct=nullptr,*mergeMS=nullptr,*mergeSingle=nullptr;
    D3DMULTISAMPLE_TYPE samples=D3DMULTISAMPLE_NONE;
    DWORD quality=0;
    bool allocationFailed=false;
    RECT written{};
    bool full=false;

    bool texture(UINT w,UINT h,D3DFORMAT format,IDirect3DTexture9*& image,IDirect3DSurface9*& level){
        return SUCCEEDED(device->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,format,D3DPOOL_DEFAULT,&image,nullptr))
            &&SUCCEEDED(image->GetSurfaceLevel(0,&level));
    }
public:
    struct NoBindingAudit {
        void operator()(const char*,unsigned,HRESULT,IDirect3DSurface9*)const noexcept{}
    };
    explicit NorthlightWaterDistanceMask(IDirect3DDevice9* d):device(d){}
    ~NorthlightWaterDistanceMask(){reset();drop(correct);drop(mergeMS);drop(mergeSingle);}
    void reset(){
        drop(coverageMS);drop(distanceMS);drop(coverageResolved);drop(resolved);drop(coverage);drop(distance);
        allocationFailed=false;
        beginFrame();
    }
    static bool multisampled(D3DMULTISAMPLE_TYPE type,DWORD quality){
        // DXVK maps NONMASKABLE quality q to 2^q raster samples.
        return type!=D3DMULTISAMPLE_NONE&&(type!=D3DMULTISAMPLE_NONMASKABLE||quality!=0);
    }
    void beginFrame(){written={};full=false;}
    std::uint64_t writtenPixels(UINT w,UINT h)const{
        return full?std::uint64_t(w)*h:std::uint64_t(written.right-written.left)*(written.bottom-written.top);
    }
    void includeDraw(UINT width,UINT height){
        if(full)return;
        DWORD enabled=0;RECT r{};
        if(FAILED(device->GetRenderState(D3DRS_SCISSORTESTENABLE,&enabled))||!enabled||FAILED(device->GetScissorRect(&r))){
            full=true;return;
        }
        r.left=std::max<LONG>(0,r.left);r.top=std::max<LONG>(0,r.top);
        r.right=std::min<LONG>(LONG(width),r.right);r.bottom=std::min<LONG>(LONG(height),r.bottom);
        if(r.left>=r.right||r.top>=r.bottom)return;
        if(written.right<=written.left)written=r;
        else{
            written.left=std::min(written.left,r.left);written.top=std::min(written.top,r.top);
            written.right=std::max(written.right,r.right);written.bottom=std::max(written.bottom,r.bottom);
        }
        full=!written.left&&!written.top&&written.right==LONG(width)&&written.bottom==LONG(height);
    }
    unsigned targets()const{return unsigned(distance!=nullptr)+unsigned(coverage!=nullptr)+unsigned(distanceMS!=nullptr)+unsigned(coverageMS!=nullptr);}
    bool matches(D3DMULTISAMPLE_TYPE type,DWORD q)const{return distance&&type==samples&&q==quality;}
    bool prepare(UINT w,UINT h,D3DMULTISAMPLE_TYPE type,DWORD q){
        if(type==samples&&q==quality&&(distance||allocationFailed))return !allocationFailed;
        reset();samples=type;quality=q;
        const bool ms=multisampled(type,q);
        bool ready=texture(w,h,D3DFMT_R32F,distance,resolved);
        if(ready&&ms){
            ready=texture(w,h,D3DFMT_L8,coverage,coverageResolved)
                &&SUCCEEDED(device->CreateRenderTarget(w,h,D3DFMT_R32F,type,q,FALSE,&distanceMS,nullptr))
                &&SUCCEEDED(device->CreateRenderTarget(w,h,D3DFMT_L8,type,q,FALSE,&coverageMS,nullptr))
                &&(correct||SUCCEEDED(device->CreatePixelShader(kWaterCorrectDistanceShader,&correct)));
        }
        auto& merge=ms?mergeMS:mergeSingle;
        if(ready&&(merge||SUCCEEDED(device->CreatePixelShader(ms?kWaterMergeDistanceMSShader:kWaterMergeDistanceShader,&merge)))){
            logf("WATER distance mask %ux%u R32F ms=%u quality=%lu coverage=%s",w,h,unsigned(type),(unsigned long)q,ms?"L8":"none");
            return true;
        }
        reset();allocationFailed=true;
        logf("WATER compact target unavailable %ux%u ms=%u quality=%lu",w,h,unsigned(type),(unsigned long)q);
        return false;
    }
    IDirect3DSurface9* distanceTarget()const{return distanceMS?distanceMS:resolved;}
    IDirect3DSurface9* coverageTarget()const{return coverageMS;}
    IDirect3DTexture9* result()const{return distance;}
    template<class Audit=NoBindingAudit>
    HRESULT resolve(UINT w,UINT h,IDirect3DSurface9* mixedTarget,NorthlightStateBlockPool& pool,const Audit& audit={}){
        if(!distance)return D3DERR_INVALIDCALL;
        if(!distanceMS&&!mixedTarget)return S_OK;
        const auto area=writtenPixels(w,h);
        if(!area){
            if(mixedTarget||!distanceMS)return S_OK;
            if(SUCCEEDED(device->ColorFill(resolved,nullptr,0)))return S_OK;
            return device->StretchRect(distanceMS,nullptr,resolved,nullptr,D3DTEXF_NONE);
        }
        HRESULT hr=S_OK;
        if(distanceMS){
            // Partial floating-point resolves on DXVK can sum samples in a
            // different order. Preserve the native full-image R32F result.
            const bool regional=!full&&area<=std::uint64_t(w)*h/4;
            hr=device->StretchRect(distanceMS,nullptr,resolved,nullptr,D3DTEXF_NONE);
            if(FAILED(hr))return hr;
            if(!regional||FAILED(device->StretchRect(coverageMS,&written,coverageResolved,&written,D3DTEXF_NONE)))
                hr=device->StretchRect(coverageMS,nullptr,coverageResolved,nullptr,D3DTEXF_NONE);
            if(FAILED(hr))return hr;
        }
        SavedState save(device,&pool);if(!save.ok)return D3DERR_INVALIDCALL;
        for(unsigned i=0;i<16;++i)device->SetTexture(i,nullptr);
        hr=NorthlightHdr::quadState(device,mixedTarget?mixedTarget:resolved,w,h,2);
        if(!full&&SUCCEEDED(hr)){
            hr=device->SetScissorRect(&written);
            if(SUCCEEDED(hr))hr=device->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE);
        }
        if(SUCCEEDED(hr))hr=device->SetRenderState(D3DRS_WRAP0,0);
        if(SUCCEEDED(hr))hr=device->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);
        if(SUCCEEDED(hr))hr=device->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0);
        if(SUCCEEDED(hr))hr=device->SetTexture(0,distanceMS?coverage:distance);
        if(mixedTarget&&distanceMS&&SUCCEEDED(hr))hr=device->SetTexture(1,distance);
        if(!mixedTarget){
            // Floating-point destination color supplies the averaged distance.
            // Multiplication in the blend unit avoids another full-size target.
            if(SUCCEEDED(hr))hr=device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_DESTCOLOR);
            if(SUCCEEDED(hr))hr=device->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ZERO);
            if(SUCCEEDED(hr))hr=device->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);
            if(SUCCEEDED(hr))hr=device->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);
        }
        if(SUCCEEDED(hr))hr=device->SetPixelShader(mixedTarget?(distanceMS?mergeMS:mergeSingle):correct);
        if(SUCCEEDED(hr)){
            hr=NorthlightScreen::drawQuad(device,w,h);
            audit(mixedTarget?(distanceMS?"WaterMergeDistanceMS":"WaterMergeDistance"):"WaterCorrectDistance",
                  mixedTarget&&distanceMS?3u:1u,hr,distanceMS?distanceMS:resolved);
        }
        return hr;
    }
};
