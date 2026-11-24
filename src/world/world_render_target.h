#pragma once
#include <d3d9.h>

namespace NorthlightWorldRenderTarget {

// A texture and its level-zero surface become visible to the caller together.
// Admission, retry deadlines and group readiness belong to the calling pass.
template<class Check>
bool ensure(IDirect3DDevice9* d,UINT w,UINT h,D3DFORMAT format,IDirect3DTexture9*& texture,
            IDirect3DSurface9*& surface,const Check& check,const char* textureStep,const char* surfaceStep){
    if(texture&&surface)return true;
    struct Staged {
        IDirect3DTexture9* texture=nullptr;
        IDirect3DSurface9* surface=nullptr;
        ~Staged(){if(surface)surface->Release();if(texture)texture->Release();}
    } next;
    HRESULT hr=d->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,format,D3DPOOL_DEFAULT,&next.texture,nullptr);
    if(SUCCEEDED(hr)&&!next.texture)hr=E_FAIL;
    if(!check(hr,textureStep)||FAILED(hr))return false;
    hr=next.texture->GetSurfaceLevel(0,&next.surface);
    if(SUCCEEDED(hr)&&!next.surface)hr=E_FAIL;
    if(!check(hr,surfaceStep)||FAILED(hr))return false;
    if(surface)surface->Release();
    if(texture)texture->Release();
    surface=next.surface;texture=next.texture;
    next.surface=nullptr;next.texture=nullptr;
    return true;
}

template<class Check>
bool ensureDepth(IDirect3DDevice9* d,UINT w,UINT h,IDirect3DSurface9*& surface,
                 const Check& check,const char* step){
    if(surface)return true;
    IDirect3DSurface9* next=nullptr;
    HRESULT hr=d->CreateDepthStencilSurface(w,h,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,&next,nullptr);
    if(SUCCEEDED(hr)&&!next)hr=E_FAIL;
    if(FAILED(hr)){
        if(next)next->Release();
        check(hr,step);
        return false;
    }
    if(!check(hr,step)){next->Release();return false;}
    surface=next;
    return true;
}
}
