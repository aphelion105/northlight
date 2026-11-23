#pragma once
#include <cstdint>
#include <d3d9.h>
#include "saved_state.h"

// The diagnostic replay uses its own cascade-sized targets. The caller
// supplies the recorded draws and policy while this pass owns the device scope.
class NorthlightWorldReplayProbePass {
    IDirect3DTexture9* texture_=nullptr;
    IDirect3DSurface9 *surface_=nullptr,*depth_=nullptr;

    template<class T> static void release(T*& resource){
        if(resource)resource->Release();
        resource=nullptr;
    }

public:
    static constexpr unsigned Size=1024;
    struct Timing {
        bool issued=false;
        int64_t setup=0,issue=0,restore=0;
    };

    NorthlightWorldReplayProbePass()=default;
    NorthlightWorldReplayProbePass(const NorthlightWorldReplayProbePass&)=delete;
    NorthlightWorldReplayProbePass& operator=(const NorthlightWorldReplayProbePass&)=delete;
    ~NorthlightWorldReplayProbePass(){reset();}

    void reset(){release(surface_);release(texture_);release(depth_);}

    template<class Clock,class Issue,class Fail>
    Timing run(IDirect3DDevice9* d,NorthlightStateBlockPool& states,const Issue& issue,const Fail& fail){
        if(!surface_||!depth_){
            reset();
            if(FAILED(d->CreateTexture(Size,Size,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&texture_,nullptr))||
               FAILED(texture_->GetSurfaceLevel(0,&surface_))||
               FAILED(d->CreateDepthStencilSurface(Size,Size,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,&depth_,nullptr))){
                reset();
                fail("probe targets",E_OUTOFMEMORY);
                return {};
            }
        }
        const int64_t start=Clock::now();
        int64_t beforeIssue,afterIssue;
        {
            SavedState save(d,&states);
            if(!save.ok){fail("probe state save",E_FAIL);return {};}
            const D3DVIEWPORT9 viewport={0,0,Size,Size,0,1};
            HRESULT result=d->SetDepthStencilSurface(nullptr);
            if(SUCCEEDED(result))result=d->SetRenderTarget(0,surface_);
            if(SUCCEEDED(result))result=d->SetDepthStencilSurface(depth_);
            if(SUCCEEDED(result))result=d->SetViewport(&viewport);
            if(SUCCEEDED(result))result=d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff,1,0);
            if(FAILED(result)){fail("probe target binding",result);return {};}
            beforeIssue=Clock::now();
            issue();
            afterIssue=Clock::now();
        }
        return {true,beforeIssue-start,afterIssue-beforeIssue,Clock::now()-afterIssue};
    }
};
