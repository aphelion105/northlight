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

    bool texture(UINT w,UINT h,D3DFORMAT format,IDirect3DTexture9*& image,IDirect3DSurface9*& level){
        return SUCCEEDED(device->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,format,D3DPOOL_DEFAULT,&image,nullptr))
            &&SUCCEEDED(image->GetSurfaceLevel(0,&level));
    }
public:
    explicit NorthlightWaterDistanceMask(IDirect3DDevice9* d):device(d){}
    ~NorthlightWaterDistanceMask(){reset();drop(correct);drop(mergeMS);drop(mergeSingle);}
    void reset(){
        drop(coverageMS);drop(distanceMS);drop(coverageResolved);drop(resolved);drop(coverage);drop(distance);
        allocationFailed=false;
    }
    unsigned targets()const{return unsigned(distance!=nullptr)+unsigned(coverage!=nullptr)+unsigned(distanceMS!=nullptr)+unsigned(coverageMS!=nullptr);}
    bool matches(D3DMULTISAMPLE_TYPE type,DWORD q)const{return distance&&type==samples&&q==quality;}
    bool prepare(UINT w,UINT h,D3DMULTISAMPLE_TYPE type,DWORD q){
        if(type==samples&&q==quality&&(distance||allocationFailed))return !allocationFailed;
        reset();samples=type;quality=q;
        const bool ms=type!=D3DMULTISAMPLE_NONE;
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
    HRESULT resolve(UINT w,UINT h,IDirect3DSurface9* mixedTarget,NorthlightStateBlockPool& pool){
        if(!distance)return D3DERR_INVALIDCALL;
        if(!distanceMS&&!mixedTarget)return S_OK;
        HRESULT hr=S_OK;
        if(distanceMS){
            hr=device->StretchRect(distanceMS,nullptr,resolved,nullptr,D3DTEXF_NONE);
            if(SUCCEEDED(hr))hr=device->StretchRect(coverageMS,nullptr,coverageResolved,nullptr,D3DTEXF_NONE);
            if(FAILED(hr))return hr;
        }
        SavedState save(device,&pool);if(!save.ok)return D3DERR_INVALIDCALL;
        for(unsigned i=0;i<16;++i)device->SetTexture(i,nullptr);
        hr=NorthlightHdr::quadState(device,mixedTarget?mixedTarget:resolved,w,h,2);
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
        return SUCCEEDED(hr)?NorthlightScreen::drawQuad(device,w,h):hr;
    }
};
