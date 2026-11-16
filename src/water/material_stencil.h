#pragma once
#include <d3d9.h>

// Receiver tags write stencil with ALWAYS; they do not restrict coverage.
// A private material replay can disable those writes. Actual stencil tests
// still need their original depth/stencil contents and remain unsupported.
inline bool northlightMaterialStencilAllows(IDirect3DDevice9* device,DWORD enabled){
    if(!enabled)return true;
    DWORD function=0,twoSided=TRUE;
    return SUCCEEDED(device->GetRenderState(D3DRS_STENCILFUNC,&function))&&function==D3DCMP_ALWAYS&&
        SUCCEEDED(device->GetRenderState(D3DRS_TWOSIDEDSTENCILMODE,&twoSided))&&!twoSided;
}
