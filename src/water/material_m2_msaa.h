#pragma once
#include "hdr_quad.h"

namespace NorthlightM2Samples {
// RESZ gets scene depth from sample zero on DXVK. Clear the other material
// samples before the ordinary color resolve so it cannot mix two surfaces.
// The merge restores the sample's original FP16 channels by multiplying by N.
inline bool resolve(IDirect3DDevice9* device,IDirect3DSurface9* source,IDirect3DSurface9* destination,
                    IDirect3DPixelShader9* clear,unsigned width,unsigned height){
    return SUCCEEDED(NorthlightHdr::quadState(device,source,width,height,3))&&
        SUCCEEDED(device->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS,TRUE))&&
        SUCCEEDED(device->SetRenderState(D3DRS_MULTISAMPLEMASK,0xfffffffeu))&&
        SUCCEEDED(device->SetPixelShader(clear))&&
        SUCCEEDED(NorthlightScreen::drawQuad(device,width,height))&&
        SUCCEEDED(NorthlightHdr::quadTarget(device,destination,width,height,3))&&
        SUCCEEDED(device->StretchRect(source,nullptr,destination,nullptr,D3DTEXF_NONE));
}

inline bool merge(IDirect3DDevice9* device,IDirect3DSurface9* target,IDirect3DTexture9* firstSample,
                  IDirect3DTexture9* legacy,IDirect3DTexture9* depth,IDirect3DPixelShader9* shader,
                  unsigned width,unsigned height,unsigned samples,float nearZ,float farZ,float minZ,float maxZ,
                  float depthBase=0,float cavityStrength=0){
    const float constants[]={float(samples),minZ,1.f/(maxZ-minZ),0,nearZ,farZ,depthBase,cavityStrength};
    return SUCCEEDED(NorthlightHdr::quadState(device,target,width,height,3))&&
        SUCCEEDED(device->SetTexture(0,firstSample))&&SUCCEEDED(device->SetTexture(1,legacy))&&
        SUCCEEDED(device->SetTexture(2,depth))&&SUCCEEDED(device->SetPixelShader(shader))&&
        SUCCEEDED(device->SetPixelShaderConstantF(0,constants,2))&&
        SUCCEEDED(NorthlightScreen::drawQuad(device,width,height));
}

// After the first resolve we can reuse the multisample image for the merge.
// The quad writes the same FP16 value to every sample. Resolving it back into
// firstSample leaves the finished material ready for the usual consumers.
inline bool mergeAndResolve(IDirect3DDevice9* device,IDirect3DSurface9* samples,IDirect3DSurface9* resolved,
                            IDirect3DTexture9* firstSample,IDirect3DTexture9* legacy,IDirect3DTexture9* depth,
                            IDirect3DPixelShader9* shader,unsigned width,unsigned height,unsigned count,
                            float nearZ,float farZ,float minZ,float maxZ){
    return merge(device,samples,firstSample,legacy,depth,shader,width,height,count,nearZ,farZ,minZ,maxZ)&&
        SUCCEEDED(NorthlightHdr::quadTarget(device,resolved,width,height,3))&&
        SUCCEEDED(device->StretchRect(samples,nullptr,resolved,nullptr,D3DTEXF_NONE));
}
}
