#pragma once
#include "saved_state.h"
#if defined(_WIN64) || defined(NORTHLIGHT_X86_D3D9ON12)
#include <wrl/client.h>
#include "hdr_quad.h"
#endif

namespace NorthlightSceneDepth {
#if defined(_WIN64) || defined(NORTHLIGHT_X86_D3D9ON12)
inline constexpr auto Intz=static_cast<D3DFORMAT>(MAKEFOURCC('I','N','T','Z'));
inline HRESULT bindSampleableDepth(IDirect3DDevice9* device,IDirect3DTexture9** depth){
    using Microsoft::WRL::ComPtr;
    ComPtr<IDirect3DSurface9> current;
    D3DSURFACE_DESC desc{};
    HRESULT hr=device->GetDepthStencilSurface(&current);
    if(FAILED(hr))return hr;
    if(FAILED(hr=current->GetDesc(&desc)))return hr;
    if(desc.MultiSampleType!=D3DMULTISAMPLE_NONE)return D3DERR_NOTAVAILABLE;
    if(desc.Format==Intz)return current->GetContainer(IID_PPV_ARGS(depth));
    if(desc.Format!=D3DFMT_D24S8&&desc.Format!=D3DFMT_D24X8)return D3DERR_NOTAVAILABLE;
    ComPtr<IDirect3DTexture9> texture;
    ComPtr<IDirect3DSurface9> surface;
    if(FAILED(hr=device->CreateTexture(desc.Width,desc.Height,1,D3DUSAGE_DEPTHSTENCIL,
        Intz,D3DPOOL_DEFAULT,&texture,nullptr)))return hr;
    if(FAILED(hr=texture->GetSurfaceLevel(0,&surface)))return hr;
    if(FAILED(hr=device->SetDepthStencilSurface(surface.Get())))return hr;
    *depth=texture.Detach();
    return S_OK;
}

inline HRESULT sample(IDirect3DDevice9* device,NorthlightStateBlockPool& states,
                      IDirect3DSurface9* source,IDirect3DTexture9* destination,IDirect3DPixelShader9* shader){
    using Microsoft::WRL::ComPtr;
    ComPtr<IDirect3DTexture9> depth;
    ComPtr<IDirect3DSurface9> target;
    D3DSURFACE_DESC desc{};
    HRESULT hr=source->GetContainer(IID_PPV_ARGS(&depth));
    if(FAILED(hr)||!depth)return D3DERR_NOTAVAILABLE;
    if(FAILED(hr=destination->GetSurfaceLevel(0,&target))||FAILED(hr=target->GetDesc(&desc)))return hr;
    SavedState saved(device,&states);
    if(!saved.ok)return E_FAIL;
    if(FAILED(hr=NorthlightHdr::quadState(device,target.Get(),desc.Width,desc.Height))||
       FAILED(hr=device->SetPixelShader(shader))||
       FAILED(hr=device->SetTexture(0,depth.Get())))return hr;
    hr=NorthlightHdr::drawQuad(device,desc.Width,desc.Height);
    device->SetTexture(0,nullptr);
    const bool restored=saved.restore();
    return !restored&&SUCCEEDED(hr)?E_FAIL:hr;
}
#endif
// Borrow a source and a destination from the caller's frame. The caller
// finishes any bound MRT writes before the state block captures the device.
template<class Device,class Error>
bool copy(Device* ext,NorthlightStateBlockPool& stateBlocks,
          IDirect3DSurface9* source,IDirect3DTexture9* target,bool nativeRuntime,bool fatal,const Error& error){
    SavedState saved(ext,&stateBlocks);
    if (!saved.ok) return false;
    auto step=[&](HRESULT hr,const char* stage){return fatal?error(hr,stage):FAILED(hr);};
    // DXVK implements AMD RESZ: POINTSIZE magic 0x7fa05000 copies/resolves
    // the bound depth surface into texture slot 0, including MSAA sample zero.
    if (step(ext->SetDepthStencilSurface(source), "bind scene depth")) return false;
    if (step(ext->SetTexture(0, target), "bind depth resolve target")) return false;
    if(nativeRuntime){
        // AMD's RESZ protocol requires a dummy draw to flush sampler binding
        // through the native D3D9 runtime. Disable ALL writes, then SavedState
        // restores the game state (including stream 0 changed by DrawPrimitiveUP).
        if(step(ext->SetVertexShader(nullptr),"RESZ dummy VS")||step(ext->SetPixelShader(nullptr),"RESZ dummy PS")||
           step(ext->SetFVF(D3DFVF_XYZ),"RESZ dummy FVF"))return false;
        const D3DRENDERSTATETYPE states[]={D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_STENCILENABLE,D3DRS_ALPHATESTENABLE,D3DRS_COLORWRITEENABLE,D3DRS_COLORWRITEENABLE1,D3DRS_COLORWRITEENABLE2,D3DRS_COLORWRITEENABLE3};
        for(auto state:states)if(step(ext->SetRenderState(state,0),"RESZ disable writes"))return false;
        if(step(ext->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1),"RESZ sampler op")||
           step(ext->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE),"RESZ sampler arg")||
           step(ext->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE),"RESZ stage1"))return false;
        const float dummy[3]={0,0,0};
        if(step(ext->DrawPrimitiveUP(D3DPT_POINTLIST,1,dummy,sizeof dummy),"RESZ dummy draw"))return false;
    }
    ext->SetRenderState(D3DRS_POINTSIZE, 0x3f800000); // trigger on a state change
    if (step(ext->SetRenderState(D3DRS_POINTSIZE, 0x7fa05000), "RESZ")) return false;
    return true;
}
}
