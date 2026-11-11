#pragma once
#include <d3d9.h>

namespace NorthlightHdr {
// The native framebuffer owns the complete authored sky. This target stores
// only extra linear emission; ordinary foreground layers attenuate it using
// their original material, alpha test, geometry and destination blend factor.
template<class Draw> HRESULT attenuateSky(IDirect3DDevice9* d,Draw submit){
    DWORD enabled=0,src=0,dst=0,op=0,srgb=0,writes=0;
    if(FAILED(d->GetRenderState(D3DRS_ALPHABLENDENABLE,&enabled))||
       FAILED(d->GetRenderState(D3DRS_SRCBLEND,&src))||FAILED(d->GetRenderState(D3DRS_DESTBLEND,&dst))||
       FAILED(d->GetRenderState(D3DRS_BLENDOP,&op))||FAILED(d->GetRenderState(D3DRS_SRGBWRITEENABLE,&srgb))||
       FAILED(d->GetRenderState(D3DRS_COLORWRITEENABLE,&writes)))return E_FAIL;
    if(!(writes&7))return S_OK;
    DWORD factor=D3DBLEND_ZERO;
    if(enabled){
        if(op!=D3DBLENDOP_ADD)return E_FAIL;
        if((src==D3DBLEND_ONE||src==D3DBLEND_SRCALPHA||src==D3DBLEND_ZERO)&&
           (dst==D3DBLEND_ONE||dst==D3DBLEND_ZERO||dst==D3DBLEND_INVSRCALPHA||dst==D3DBLEND_SRCALPHA||
            dst==D3DBLEND_SRCCOLOR||dst==D3DBLEND_INVSRCCOLOR))factor=dst;
        else if(src==D3DBLEND_DESTCOLOR&&dst==D3DBLEND_ZERO)factor=D3DBLEND_SRCCOLOR;
        else if(src==D3DBLEND_INVDESTCOLOR&&dst==D3DBLEND_ONE)factor=D3DBLEND_INVSRCCOLOR;
        else return E_FAIL;
    }
    if(factor==D3DBLEND_ONE)return S_OK; // Additive SDR is already in the native scene.
    if(srgb&&(factor==D3DBLEND_SRCCOLOR||factor==D3DBLEND_INVSRCCOLOR))return E_FAIL;
    struct Restore {
        IDirect3DDevice9* d;DWORD enabled,src,dst,op,srgb,writes;
        ~Restore(){d->SetRenderState(D3DRS_ALPHABLENDENABLE,enabled);d->SetRenderState(D3DRS_SRCBLEND,src);
            d->SetRenderState(D3DRS_DESTBLEND,dst);d->SetRenderState(D3DRS_BLENDOP,op);
            d->SetRenderState(D3DRS_SRGBWRITEENABLE,srgb);d->SetRenderState(D3DRS_COLORWRITEENABLE,writes);}
    } restore{d,enabled,src,dst,op,srgb,writes};
    if(FAILED(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE))||FAILED(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ZERO))||
       FAILED(d->SetRenderState(D3DRS_DESTBLEND,factor))||FAILED(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD))||
       FAILED(d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE))||FAILED(d->SetRenderState(D3DRS_COLORWRITEENABLE,writes&7)))return E_FAIL;
    return submit();
}
}
