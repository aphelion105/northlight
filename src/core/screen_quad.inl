#pragma once
namespace NorthlightScreen {
template<class Device,class Shader>
inline HRESULT createPixelShader(Device* device,const DWORD* code,Shader*& shader){
    Shader* candidate=nullptr;
    const HRESULT hr=device->CreatePixelShader(code,&candidate);
    if(FAILED(hr)){
        if(candidate)candidate->Release();
        return hr;
    }
    if(!candidate)return E_FAIL;
    shader=candidate;
    return hr;
}

// Pixel centres and UVs match the D3D9 fullscreen passes, including odd sizes.
// The caller owns viewport, shaders, samplers and coverage state.
template<class Device>
inline HRESULT drawQuad(Device* device,unsigned width,unsigned height){
    struct Vertex{float x,y,z,w,u,v;};
    const Vertex triangle[]={{-.5f,-.5f,0,1,0,0},{2.f*width-.5f,-.5f,0,1,2,0},{-.5f,2.f*height-.5f,0,1,0,2}};
    return device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,triangle,sizeof(Vertex));
}
template<class Device>
inline HRESULT drawViewportQuad(Device* device,unsigned width,unsigned height){
    const D3DVIEWPORT9 viewport{0,0,width,height,0,1};
    device->SetViewport(&viewport);
    return drawQuad(device,width,height);
}
}
