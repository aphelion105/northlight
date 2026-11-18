#pragma once
#include "diagnostic_files.h"
#include <d3d9.h>

namespace NorthlightDiagnosticReadback {
// Large 4K targets cannot depend on a contiguous 32-64 MiB system-memory
// surface in the 32-bit client. Copy exact rows without binding a render target.
inline constexpr unsigned StripBytes=2u*1024*1024,DirectLimit=8u*1024*1024;
inline HRESULT strips(IDirect3DDevice9* device,IDirect3DSurface9* source,const D3DSURFACE_DESC& desc,
                      unsigned stride,const std::string& path){
    struct Surface {IDirect3DSurface9* p=nullptr;~Surface(){if(p)p->Release();}};
    const unsigned rowBytes=desc.Width*stride,rows=std::min(desc.Height,StripBytes/rowBytes);
    if(!rows||desc.MultiSampleType!=D3DMULTISAMPLE_NONE)return D3DERR_INVALIDCALL;
    Surface tile,stage;
    HRESULT hr=device->CreateRenderTarget(desc.Width,rows,desc.Format,D3DMULTISAMPLE_NONE,0,FALSE,&tile.p,nullptr);
    if(FAILED(hr))return hr;
    hr=device->CreateOffscreenPlainSurface(desc.Width,rows,desc.Format,D3DPOOL_SYSTEMMEM,&stage.p,nullptr);
    if(FAILED(hr))return hr;
    const bool saved=NorthlightDiagnosticFiles::streamImage(path,desc.Width,desc.Height,desc.Format,stride,[&](auto put){
        for(unsigned y=0;y<desc.Height;y+=rows){
            const LONG count=LONG(std::min(rows,desc.Height-y));
            RECT from{0,LONG(y),LONG(desc.Width),LONG(y)+count},to{0,0,LONG(desc.Width),count};
            hr=device->StretchRect(source,&from,tile.p,&to,D3DTEXF_NONE);
            if(FAILED(hr))return false;
            hr=device->GetRenderTargetData(tile.p,stage.p);
            if(FAILED(hr))return false;
            D3DLOCKED_RECT lock{};
            hr=stage.p->LockRect(&lock,nullptr,D3DLOCK_READONLY);
            if(FAILED(hr))return false;
            bool ok=lock.pBits&&lock.Pitch>=int(rowBytes);
            for(LONG i=0;ok&&i<count;++i)ok=put(static_cast<const char*>(lock.pBits)+size_t(i)*lock.Pitch,rowBytes);
            hr=stage.p->UnlockRect();
            if(!ok||FAILED(hr))return false;
        }
        return true;
    });
    return saved?S_OK:FAILED(hr)?hr:E_FAIL;
}
}
