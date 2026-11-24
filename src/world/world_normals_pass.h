#pragma once
#include "screen_quad.h"
#include "world_compiled_shaders.h"

// Owns the reconstructed normal image. The current distance target remains
// owned by lighting history and is borrowed only while it is bound as RT1.
class NorthlightWorldNormalsPass {
    friend struct WorldScreenEffectsTest;
    friend struct PointTerrainCoverageTest;
    IDirect3DTexture9* texture_=nullptr;
    IDirect3DSurface9* surface_=nullptr;
    IDirect3DPixelShader9* shader_=nullptr;
    IDirect3DPixelShader9* normalsOnly_=nullptr;
    bool normalsOnlyRefused_=false;

    template<class T> static void drop(T*& pointer){if(pointer){pointer->Release();pointer=nullptr;}}
public:
    NorthlightWorldNormalsPass()=default;
    NorthlightWorldNormalsPass(const NorthlightWorldNormalsPass&)=delete;
    NorthlightWorldNormalsPass& operator=(const NorthlightWorldNormalsPass&)=delete;
    ~NorthlightWorldNormalsPass(){reset();}

    IDirect3DTexture9* texture() const {return texture_;}
    IDirect3DSurface9* surface() const {return surface_;}

    template<class Pixel> bool createShader(const Pixel& pixel){
        return pixel(kWorldNormalsShader,shader_,"world normals shader");
    }
    template<class Image> bool createTarget(const Image& image){
        return image(D3DFMT_A16B16G16R16F,texture_,surface_);
    }
    void reset(){
        drop(shader_);
        drop(normalsOnly_);
        normalsOnlyRefused_=false;
        drop(texture_);
        drop(surface_);
    }

    template<class Check>
    bool draw(IDirect3DDevice9* d,UINT width,UINT height,IDirect3DTexture9* modelNormals,
              IDirect3DSurface9* distance,const Check& check){
        d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
        d->SetRenderState(D3DRS_COLORWRITEENABLE,15);
        d->SetTexture(14,nullptr);
        d->SetTexture(15,nullptr);
        d->SetRenderState(D3DRS_COLORWRITEENABLE1,15);
        if(!check(d->SetRenderTarget(0,surface_),"world normals target")||
           !check(d->SetRenderTarget(1,distance),"current distance target"))return false;
        if(!distance&&!normalsOnly_&&!normalsOnlyRefused_){
            IDirect3DPixelShader9* optional=nullptr;
            if(SUCCEEDED(d->CreatePixelShader(kWorldNormalsNoDistanceShader,&optional)))normalsOnly_=optional;
            else {drop(optional);normalsOnlyRefused_=true;}
        }
        const float modelNormalInfo[]={modelNormals?1.f:0.f,0,0,0};
        d->SetPixelShaderConstantF(79,modelNormalInfo,1);
        d->SetTexture(13,modelNormals); // s13 retains POINT/CLAMP, no mip and no sRGB.
        d->SetPixelShader(!distance&&normalsOnly_?normalsOnly_:shader_);
        if(!check(NorthlightScreen::drawQuad(d,width,height),"world normals pass"))return false;
        d->SetTexture(13,nullptr);
        if(distance&&!check(d->SetRenderTarget(1,nullptr),"current distance unbind"))return false;
        d->SetTexture(14,texture_);
        d->SetSamplerState(14,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
        d->SetSamplerState(14,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
        d->SetSamplerState(14,D3DSAMP_MINFILTER,D3DTEXF_POINT);
        d->SetSamplerState(14,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
        d->SetSamplerState(14,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
        d->SetSamplerState(14,D3DSAMP_SRGBTEXTURE,FALSE);
        return true;
    }
};
