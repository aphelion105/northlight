#pragma once
#include <d3d9.h>
#include <algorithm>
#include <new>
#include <vector>

namespace NorthlightModelNormalSampleGrid {
// WorldNormals point-samples texels (2x+1, 2y+1) in the original target.
// Initialize its private stencil once. Depth and color clears leave it intact.
inline bool initialize(IDirect3DDevice9* device,UINT width,UINT height,UINT primitiveLimit){
    struct Vertex {float x,y,z,w;DWORD color;};
    if(width<2||height<2||width/2>primitiveLimit/2||(height+1)/2>primitiveLimit/2)return false;
    std::vector<Vertex> vertices;
    try{vertices.reserve(size_t(6)*std::max(width/2,(height+1)/2));}catch(const std::bad_alloc&){return false;}
    const D3DVIEWPORT9 viewport={0,0,width,height,0,1};
    if(FAILED(device->SetViewport(&viewport))||FAILED(device->SetVertexShader(nullptr))||
       FAILED(device->SetPixelShader(nullptr))||FAILED(device->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE))||
       FAILED(device->SetStreamSourceFreq(0,1))||
       FAILED(device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_DISABLE)))return false;
    const struct {D3DRENDERSTATETYPE state;DWORD value;} states[]={
        {D3DRS_ZENABLE,FALSE},{D3DRS_ZWRITEENABLE,FALSE},{D3DRS_COLORWRITEENABLE,0},
        {D3DRS_ALPHATESTENABLE,FALSE},{D3DRS_FOGENABLE,FALSE},{D3DRS_SCISSORTESTENABLE,FALSE},
        {D3DRS_CLIPPLANEENABLE,0},{D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_FILLMODE,D3DFILL_SOLID},
        {D3DRS_MULTISAMPLEMASK,0xffffffffu},{D3DRS_STENCILENABLE,TRUE},{D3DRS_TWOSIDEDSTENCILMODE,FALSE},
        {D3DRS_STENCILFUNC,D3DCMP_ALWAYS},{D3DRS_STENCILFAIL,D3DSTENCILOP_KEEP},
        {D3DRS_STENCILZFAIL,D3DSTENCILOP_KEEP},{D3DRS_STENCILPASS,D3DSTENCILOP_REPLACE},
        {D3DRS_STENCILWRITEMASK,0xff}
    };
    for(const auto& state:states)if(FAILED(device->SetRenderState(state.state,state.value)))return false;
    if(FAILED(device->Clear(0,nullptr,D3DCLEAR_STENCIL,0,1,0)))return false;
    const auto quad=[&](UINT x,UINT y,UINT right,UINT bottom){
        const Vertex a={float(x)-.5f,float(y)-.5f,0,1,0xffffffffu};
        const Vertex b={float(right)-.5f,a.y,0,1,a.color};
        const Vertex c={a.x,float(bottom)-.5f,0,1,a.color};
        const Vertex d={b.x,c.y,0,1,a.color};
        vertices.insert(vertices.end(),{a,b,c,c,b,d});
    };
    for(UINT x=1;x<width;x+=2)quad(x,0,x+1,height);
    if(FAILED(device->SetRenderState(D3DRS_STENCILREF,1))||
       FAILED(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,UINT(vertices.size()/3),vertices.data(),sizeof(Vertex))))return false;
    vertices.clear();
    for(UINT y=0;y<height;y+=2)quad(0,y,width,y+1);
    return SUCCEEDED(device->SetRenderState(D3DRS_STENCILREF,0))&&
           SUCCEEDED(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,UINT(vertices.size()/3),vertices.data(),sizeof(Vertex)));
}

inline bool test(IDirect3DDevice9* device){
    return SUCCEEDED(device->SetRenderState(D3DRS_TWOSIDEDSTENCILMODE,FALSE))&&
           SUCCEEDED(device->SetRenderState(D3DRS_STENCILMASK,1))&&
           SUCCEEDED(device->SetRenderState(D3DRS_STENCILREF,1))&&
           SUCCEEDED(device->SetRenderState(D3DRS_STENCILFUNC,D3DCMP_EQUAL))&&
           SUCCEEDED(device->SetRenderState(D3DRS_STENCILFAIL,D3DSTENCILOP_KEEP))&&
           SUCCEEDED(device->SetRenderState(D3DRS_STENCILZFAIL,D3DSTENCILOP_KEEP))&&
           SUCCEEDED(device->SetRenderState(D3DRS_STENCILPASS,D3DSTENCILOP_KEEP))&&
           SUCCEEDED(device->SetRenderState(D3DRS_STENCILENABLE,TRUE));
}
}
