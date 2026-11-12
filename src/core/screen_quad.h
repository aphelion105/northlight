#pragma once
#include <d3d9.h>
namespace NorthlightScreen {
// Pixel centres and UVs match the D3D9 fullscreen passes, including odd sizes.
// The caller owns viewport, shaders, samplers and coverage state.
inline HRESULT drawQuad(IDirect3DDevice9* device,unsigned width,unsigned height){
    struct Vertex{float x,y,z,w,u,v;};
    const Vertex triangle[]={{-.5f,-.5f,0,1,0,0},{2.f*width-.5f,-.5f,0,1,2,0},{-.5f,2.f*height-.5f,0,1,0,2}};
    return device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,triangle,sizeof(Vertex));
}
}
