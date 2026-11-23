#pragma once
#include <d3d9.h>

namespace NorthlightBackend {
struct NativeDeviceCaps {
    bool queried=false, intz=false, resz=false, floatRT=false, sm3=false;
    bool supported() const { return queried&&intz&&resz&&floatRT&&sm3; }
};

inline NativeDeviceCaps queryNativeDeviceCaps(IDirect3D9* d, UINT adapter, D3DDEVTYPE type) {
    NativeDeviceCaps result;
    D3DDISPLAYMODE mode{};
    D3DCAPS9 caps{};
    result.queried=SUCCEEDED(d->GetAdapterDisplayMode(adapter,&mode))&&SUCCEEDED(d->GetDeviceCaps(adapter,type,&caps));
    if(!result.queried)return result;
    const auto format=[&](D3DFORMAT f,DWORD usage,D3DRESOURCETYPE resource){
        return d->CheckDeviceFormat(adapter,type,mode.Format,usage,resource,f)==D3D_OK;
    };
    result.intz=format((D3DFORMAT)MAKEFOURCC('I','N','T','Z'),D3DUSAGE_DEPTHSTENCIL,D3DRTYPE_TEXTURE);
    result.resz=format((D3DFORMAT)MAKEFOURCC('R','E','S','Z'),D3DUSAGE_RENDERTARGET,D3DRTYPE_SURFACE);
    result.floatRT=format(D3DFMT_A16B16G16R16F,D3DUSAGE_RENDERTARGET,D3DRTYPE_TEXTURE)&&format(D3DFMT_R32F,D3DUSAGE_RENDERTARGET,D3DRTYPE_TEXTURE);
    result.sm3=caps.VertexShaderVersion>=D3DVS_VERSION(3,0)&&caps.PixelShaderVersion>=D3DPS_VERSION(3,0);
    return result;
}
}
