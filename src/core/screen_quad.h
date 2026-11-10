#pragma once
#include <d3d9.h>
namespace NorthlightScreen {
// Pixel centres and UVs match the D3D9 fullscreen passes, including odd sizes.
// The caller owns viewport, shaders, samplers and coverage state.
inline HRESULT drawQuad(IDirect3DDevice9* device,unsigned width,unsigned height){
    struct Vertex{float x,y,z,w,u,v;};
    const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{float(width)-.5f,-.5f,0,1,1,0},{-.5f,float(height)-.5f,0,1,0,1},{float(width)-.5f,float(height)-.5f,0,1,1,1}};
    return device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex));
}
}
