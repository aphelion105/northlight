#pragma once
#include <d3d9.h>

// LocalShadowPS emits the same R32F depth to COLOR0 and COLOR1 for static
// geometry. Keep the working cube bound while writing its reusable static face.
class PointShadowFaceTargets {
    IDirect3DDevice9* device_;
    bool attached_=false;
public:
    explicit PointShadowFaceTargets(IDirect3DDevice9* device):device_(device){}
    PointShadowFaceTargets(const PointShadowFaceTargets&)=delete;
    ~PointShadowFaceTargets(){if(attached_)device_->SetRenderTarget(1,nullptr);}

    bool attach(IDirect3DSurface9* working,IDirect3DSurface9* cached){
        attached_=SUCCEEDED(device_->SetRenderTarget(0,working)) &&
                  SUCCEEDED(device_->SetRenderState(D3DRS_COLORWRITEENABLE1,15)) &&
                  SUCCEEDED(device_->SetRenderTarget(1,cached));
        return attached_;
    }
    HRESULT finish(){
        if(!attached_)return D3D_OK;
        const HRESULT hr=device_->SetRenderTarget(1,nullptr);
        if(SUCCEEDED(hr))attached_=false;
        return hr;
    }
};
