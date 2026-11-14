#pragma once
#include "world_compiled_shaders.h"
#include "screen_quad.h"

// Shared WorldNormals consumer for the complete capture-path benchmark. Both
// variants resolve exactly the same native scene color and RESZ depth first.
struct WorldNormalsMsaaFixture {
    IDirect3DDevice9* device;unsigned width,height;
    IDirect3DTexture9* depth=nullptr;IDirect3DPixelShader9* shader=nullptr;
    Target normal,distance;float bank[80][4]{},constants[8][4]{};
    WorldNormalsMsaaFixture(IDirect3DDevice9* dev,unsigned w,unsigned h):device(dev),width(w),height(h){
        ok(device->CreateTexture(w,h,1,D3DUSAGE_DEPTHSTENCIL,D3DFORMAT(MAKEFOURCC('I','N','T','Z')),D3DPOOL_DEFAULT,&depth,nullptr));
        ok(device->CreatePixelShader(kWorldNormalsShader,&shader));normal=target(w/2,h/2,D3DFMT_A16B16G16R16F,8);distance=target(w/2,h/2,D3DFMT_R32F,4);
        bank[0][0]=1.f/w;bank[0][1]=1.f/h;bank[0][2]=.5f;bank[0][3]=500;
        bank[1][0]=bank[1][1]=bank[1][2]=bank[2][0]=1;
        for(unsigned i=0;i<3;++i)bank[3+i][i]=1;
        bank[33][0]=float(w);bank[33][1]=float(h);bank[33][2]=float(w/2);bank[33][3]=float(h/2);bank[79][0]=1;
        std::memcpy(constants,bank,6*16);std::memcpy(constants[6],bank[33],16);std::memcpy(constants[7],bank[79],16);constants[7][1]=1;
    }
    ~WorldNormalsMsaaFixture(){for(auto* p:{depth,normal.t,distance.t})p->Release();for(auto* p:{normal.s,normal.read,distance.s,distance.read})p->Release();shader->Release();}
    void render(NorthlightModelNormalMsaa* fused,IDirect3DTexture9* model,IDirect3DSurface9* msColor,const Target& scene){
        // Called with the native MSAA color/depth still bound, after EndScene.
        ok(device->SetTexture(0,depth));ok(device->SetRenderState(D3DRS_POINTSIZE,0x3f800000));ok(device->SetRenderState(D3DRS_POINTSIZE,0x7fa05000));ok(device->SetTexture(0,nullptr));
        ok(device->SetDepthStencilSurface(nullptr));ok(device->SetRenderTarget(0,scene.s));ok(device->StretchRect(msColor,nullptr,scene.s,nullptr,D3DTEXF_NONE));
        if(fused){assert(fused->composeWorld(scene.t,depth,&constants[0][0]));return;}
        ok(device->SetRenderTarget(0,normal.s));ok(device->SetRenderTarget(1,distance.s));
        D3DVIEWPORT9 vp{0,0,width/2,height/2,0,1};ok(device->SetViewport(&vp));
        for(auto key:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_STENCILENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_FOGENABLE})ok(device->SetRenderState(key,FALSE));
        ok(device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(device->SetRenderState(D3DRS_COLORWRITEENABLE,15));ok(device->SetRenderState(D3DRS_COLORWRITEENABLE1,15));
        ok(device->SetVertexShader(nullptr));ok(device->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));ok(device->SetPixelShader(shader));ok(device->SetPixelShaderConstantF(0,bank[0],80));
        for(unsigned i:{0u,1u,13u}){for(auto key:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(device->SetSamplerState(i,key,i?D3DTEXF_POINT:D3DTEXF_LINEAR));ok(device->SetSamplerState(i,D3DSAMP_MIPFILTER,D3DTEXF_NONE));for(auto key:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})ok(device->SetSamplerState(i,key,D3DTADDRESS_CLAMP));ok(device->SetSamplerState(i,D3DSAMP_SRGBTEXTURE,FALSE));}
        ok(device->SetTexture(0,scene.t));ok(device->SetTexture(1,depth));ok(device->SetTexture(13,model));ok(device->BeginScene());ok(NorthlightScreen::drawQuad(device,width/2,height/2));ok(device->EndScene());
        for(unsigned i:{0u,1u,13u})ok(device->SetTexture(i,nullptr));ok(device->SetRenderTarget(1,nullptr));
    }
};
