#pragma once
#include <d3d9.h>
#include "shadow_alpha.h"

namespace NorthlightShadowAlpha {
// Alpha-test precision belongs to RT0's format, even when RT0 stores no alpha.
// Keep that native conversion while writing the actual shadow depth through RT1.
template<class Device,class Acquire,class Draw> HRESULT draw(Device* d,unsigned mode,Acquire acquire,Draw submit){
    if(!fixed(mode))return submit();
    IDirect3DSurface9 *target=nullptr,*second=nullptr;
    HRESULT hr=d->GetRenderTarget(0,&target);if(FAILED(hr)||!target)return FAILED(hr)?hr:D3DERR_NOTFOUND;
    hr=d->GetRenderTarget(1,&second);
    if(FAILED(hr)&&hr!=D3DERR_NOTFOUND){target->Release();return hr;}
    D3DVIEWPORT9 viewport{};RECT scissor{};D3DSURFACE_DESC desc{};
    if(SUCCEEDED(hr)||hr==D3DERR_NOTFOUND)hr=d->GetViewport(&viewport);
    if(SUCCEEDED(hr))hr=target->GetDesc(&desc);
    if(SUCCEEDED(hr))hr=d->GetScissorRect(&scissor);
    const D3DRENDERSTATETYPE keys[]={D3DRS_ALPHATESTENABLE,D3DRS_ALPHAFUNC,D3DRS_ALPHAREF,D3DRS_COLORWRITEENABLE,D3DRS_COLORWRITEENABLE1};
    DWORD old[5]={};
    for(unsigned i=0;SUCCEEDED(hr)&&i<5;++i)hr=d->GetRenderState(keys[i],old+i);
    auto* scratch=SUCCEEDED(hr)?acquire(D3DFORMAT(sourceFormat(mode)),desc.Width,desc.Height):nullptr;
    if(SUCCEEDED(hr)&&!scratch)hr=D3DERR_NOTAVAILABLE;
    if(FAILED(hr)){if(second)second->Release();target->Release();return hr;}
    const DWORD values[]={TRUE,(mode&GreaterEqual)?DWORD(D3DCMP_GREATEREQUAL):DWORD(D3DCMP_GREATER),reference(mode),0,old[3]};
    hr=d->SetRenderTarget(1,nullptr);
    if(SUCCEEDED(hr))hr=d->SetRenderTarget(0,scratch);
    if(SUCCEEDED(hr))hr=d->SetRenderTarget(1,target);
    if(SUCCEEDED(hr))hr=d->SetViewport(&viewport);
    if(SUCCEEDED(hr))hr=d->SetScissorRect(&scissor);
    for(unsigned i=0;SUCCEEDED(hr)&&i<5;++i)hr=d->SetRenderState(keys[i],values[i]);
    if(SUCCEEDED(hr))hr=submit();
    bool restored=true;
    auto restore=[&](HRESULT result){if(FAILED(result))restored=false;};
    restore(d->SetRenderTarget(1,nullptr));restore(d->SetRenderTarget(0,target));restore(d->SetRenderTarget(1,second));restore(d->SetViewport(&viewport));restore(d->SetScissorRect(&scissor));
    for(unsigned i=0;i<5;++i)restore(d->SetRenderState(keys[i],old[i]));
    if(second)second->Release();target->Release();
    return FAILED(hr)?hr:restored?hr:E_FAIL;
}
}
