#pragma once
#include <d3d9.h>
#include "screen_quad.h"

// GPU state shared by the direct raymarch and volume atlas paths. Their
// coordinator chooses sources and history. FogScreen owns the screen images.
class NorthlightAtmosphereCommands {
    IDirect3DDevice9* d;

    void restoreNoiseSlot(){
        d->SetTexture(14,nullptr);
        d->SetSamplerState(14,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
        d->SetSamplerState(14,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
        d->SetSamplerState(14,D3DSAMP_ADDRESSW,D3DTADDRESS_CLAMP);
        d->SetSamplerState(14,D3DSAMP_MINFILTER,D3DTEXF_POINT);
        d->SetSamplerState(14,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
    }

public:
    explicit NorthlightAtmosphereCommands(IDirect3DDevice9* device):d(device){}

    void bindActor(IDirect3DTexture9* depth,const float* matrix,const float* bounds,bool enabled){
        const float info[4]={enabled?1.f:0.f,0,0,0};
        d->SetTexture(4,depth);
        d->SetPixelShaderConstantF(68,matrix,4);
        d->SetPixelShaderConstantF(72,info,1);
        d->SetPixelShaderConstantF(66,bounds,1);
    }
    void bindProbeOrTiles(IDirect3DTexture9* texture){d->SetTexture(5,texture);}

    void begin(IDirect3DSurface9* target,IDirect3DPixelShader9* shader,const float* ambient){
        // Temporal fog may reuse s15 without running its earlier setup.
        d->SetSamplerState(15,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
        d->SetSamplerState(15,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
        d->SetSamplerState(15,D3DSAMP_MINFILTER,D3DTEXF_POINT);
        d->SetSamplerState(15,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
        d->SetSamplerState(15,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
        d->SetRenderTarget(0,target);
        d->SetPixelShader(shader);
        d->SetPixelShaderConstantF(18,ambient,1);
    }

    void sourceMode(bool first){
        d->SetRenderState(D3DRS_ALPHABLENDENABLE,!first);
        d->SetRenderState(D3DRS_COLORWRITEENABLE,first?15:7);
    }

    void bindNoise(IDirect3DVolumeTexture9* noise){
        d->SetTexture(14,noise);
        d->SetSamplerState(14,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP);
        d->SetSamplerState(14,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP);
        d->SetSamplerState(14,D3DSAMP_ADDRESSW,D3DTADDRESS_WRAP);
        d->SetSamplerState(14,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);
        d->SetSamplerState(14,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
        d->SetSamplerState(14,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
        d->SetSamplerState(14,D3DSAMP_SRGBTEXTURE,FALSE);
    }

    void bindImportedVolume(IDirect3DVolumeTexture9* noise,const float* constants){
        bindNoise(noise);
        d->SetPixelShaderConstantF(116,constants,38);
    }

    void beforeAtlasResolve(const float* previousView){
        sourceMode(true);
        d->SetPixelShaderConstantF(53,previousView,4);
    }
    void afterAtlasResolve(IDirect3DTexture9* scene,IDirect3DTexture9* nearShadow){
        d->SetTexture(0,scene);
        d->SetTexture(2,nearShadow);
        d->SetSamplerState(2,D3DSAMP_MINFILTER,D3DTEXF_POINT);
        d->SetSamplerState(2,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
        restoreNoiseSlot();
    }

    void beginClouds(IDirect3DVolumeTexture9* noise,const float* fog,const float* colour){
        d->SetPixelShaderConstantF(25,fog,1);
        d->SetPixelShaderConstantF(26,colour,1);
        bindNoise(noise);
        d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);
        d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE);
        d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_SRCALPHA);
        d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);
        d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,TRUE);
        d->SetRenderState(D3DRS_SRCBLENDALPHA,D3DBLEND_ZERO);
        d->SetRenderState(D3DRS_DESTBLENDALPHA,D3DBLEND_SRCALPHA);
        d->SetRenderState(D3DRS_BLENDOPALPHA,D3DBLENDOP_ADD);
    }
    HRESULT drawClouds(IDirect3DPixelShader9* shader,UINT width,UINT height){
        d->SetRenderState(D3DRS_COLORWRITEENABLE,15);
        d->SetPixelShader(shader);
        const D3DVIEWPORT9 viewport{0,0,width,height,0,1};
        d->SetViewport(&viewport);
        return NorthlightScreen::drawQuad(d,width,height);
    }
    void endClouds(const float* fogParameters,bool first,IDirect3DPixelShader9* shader){
        d->SetPixelShaderConstantF(25,fogParameters,2);
        d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE);
        d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE);
        d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);
        d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE);
        sourceMode(first);
        d->SetPixelShader(shader);
        restoreNoiseSlot();
    }

    void restoreLighting(IDirect3DTexture9* const* textures,IDirect3DTexture9* baseline,bool transition,const float* light){
        d->SetTexture(4,textures[4]);
        d->SetTexture(5,textures[5]);
        d->SetTexture(12,baseline);
        if(transition)for(unsigned sampler=6;sampler<10;++sampler){
            d->SetTexture(sampler,textures[sampler]);
            const DWORD filter=sampler==9?D3DTEXF_LINEAR:D3DTEXF_POINT;
            d->SetSamplerState(sampler,D3DSAMP_MINFILTER,filter);
            d->SetSamplerState(sampler,D3DSAMP_MAGFILTER,filter);
        }
        d->SetPixelShaderConstantF(17,light,2);
    }
};
