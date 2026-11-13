#pragma once
#include "fog_volume_compiled_shaders.h"
#include "screen_quad.h"

// Three RGBA16F atlases: coefficients/history and two prefix-scan scratch images.
// The screen depth only participates in resolve, so receiver motion cannot drag history.
struct NorthlightFogVolume {
    IDirect3DTexture9* texture[3]={};IDirect3DSurface9* surface[3]={};
    IDirect3DPixelShader9* inject[8]={},*temporal=nullptr,*prefix=nullptr,*resolve=nullptr;
    unsigned cellWidth=0,cellHeight=0,history=2,result=1;
    bool valid=false,refused=false,cacheReady=false;
    float previous[4]={};
    unsigned updates=0,reuses=0;
    template<class T>static void drop(T*& p){if(p)p->Release();p=nullptr;}
    void reset(){
        for(auto*& p:surface)drop(p);for(auto*& p:texture)drop(p);for(auto*& p:inject)drop(p);
        drop(temporal);drop(prefix);drop(resolve);cellWidth=cellHeight=0;valid=refused=cacheReady=false;
    }
    bool resources(IDirect3DDevice9* d,unsigned w,unsigned h){
        if(refused)return false;if(texture[0])return true;
        cellWidth=std::max(1u,(w+15)/16);cellHeight=std::max(1u,(h+15)/16);
        D3DCAPS9 caps={};
        bool ok=SUCCEEDED(d->GetDeviceCaps(&caps))&&cellWidth*8<=caps.MaxTextureWidth&&cellHeight*8<=caps.MaxTextureHeight&&caps.MaxPixelShader30InstructionSlots>=kVolumeInjectActorTransitionShaderSlots;
        const DWORD* code[]={kVolumeInjectShader,kVolumeInjectQuietShader,kVolumeInjectActorShader,kVolumeInjectActorQuietShader,
            kVolumeInjectTransitionShader,kVolumeInjectQuietTransitionShader,kVolumeInjectActorTransitionShader,kVolumeInjectActorQuietTransitionShader};
        for(unsigned i=0;i<8&&ok;++i)ok=SUCCEEDED(d->CreatePixelShader(code[i],&inject[i]));
        if(ok)ok=SUCCEEDED(d->CreatePixelShader(kVolumeTemporalShader,&temporal))&&SUCCEEDED(d->CreatePixelShader(kVolumePrefixShader,&prefix))&&SUCCEEDED(d->CreatePixelShader(kVolumeResolveShader,&resolve));
        for(unsigned i=0;i<3&&ok;++i)ok=SUCCEEDED(d->CreateTexture(cellWidth*8,cellHeight*8,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&texture[i],nullptr))&&SUCCEEDED(texture[i]->GetSurfaceLevel(0,&surface[i]));
        if(!ok){reset();refused=true;}return ok;
    }
    void grid(IDirect3DDevice9* d,float range){
        const float size[]={float(cellWidth),float(cellHeight),1.f/(cellWidth*8),1.f/(cellHeight*8)};
        const float pass[]={range,0,0,0};d->SetPixelShaderConstantF(154,size,1);d->SetPixelShaderConstantF(155,pass,1);
    }
    static void sampler(IDirect3DDevice9* d,unsigned s){
        d->SetSamplerState(s,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(s,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
        d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);d->SetSamplerState(s,D3DSAMP_SRGBTEXTURE,FALSE);
    }
    bool target(IDirect3DDevice9* d,unsigned i){
        // Unbind both atlas readers before rotating render targets (native D3D9).
        d->SetTexture(0,nullptr);d->SetTexture(2,nullptr);
        return SUCCEEDED(d->SetRenderTarget(0,surface[i]));
    }
    HRESULT draw(IDirect3DDevice9* d){D3DVIEWPORT9 vp={0,0,cellWidth*8,cellHeight*8,0,1};d->SetViewport(&vp);return NorthlightScreen::drawQuad(d,vp.Width,vp.Height);}
    unsigned raw()const{return (history+1)%3;}
    bool injection(IDirect3DDevice9* d,IDirect3DPixelShader9* shader,float range,bool noisy,bool first){
        d->SetPixelShader(shader);d->SetRenderState(D3DRS_COLORWRITEENABLE,first?15:7);
        d->SetRenderState(D3DRS_ALPHABLENDENABLE,!first);d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE);d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE);
        float pass[]={range,0,0,.211324865f};d->SetPixelShaderConstantF(155,pass,1);
        if(FAILED(draw(d)))return false;
        if(noisy){pass[3]=.788675135f;d->SetPixelShaderConstantF(155,pass,1);d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);
            if(FAILED(draw(d)))return false;d->SetRenderState(D3DRS_ALPHABLENDENABLE,!first);}
        return true;
    }
    // Caller has finished additive source injection into raw().
    bool integrate(IDirect3DDevice9* d,float range,const float projection[4],bool useHistory){
        const unsigned input=raw(),next=(history+2)%3;
        float pass[]={range,0,valid&&useHistory?.85f:0.f,0};
        valid=false; // Never cache a partially written temporal/prefix sequence.
        cacheReady=pass[2]==0;
        if(!target(d,next))return false;
        sampler(d,0);sampler(d,2);d->SetTexture(0,texture[input]);d->SetTexture(2,texture[history]);
        d->SetPixelShaderConstantF(155,pass,1);d->SetPixelShaderConstantF(156,previous,1);d->SetPixelShader(temporal);
        if(FAILED(draw(d)))return false;
        history=next;previous[0]=projection[0];previous[1]=projection[1];previous[2]=projection[2];previous[3]=range;
        unsigned source=history;
        for(unsigned step=0;step<7;++step){
            const unsigned dest=source==input?(history+1)%3:input;
            if(!target(d,dest))return false;
            d->SetTexture(0,texture[source]);pass[1]=step?float(1u<<(step-1)):0;pass[2]=0;
            d->SetPixelShaderConstantF(155,pass,1);d->SetPixelShader(prefix);
            if(FAILED(draw(d)))return false;source=dest;
        }
        result=source;valid=true;++updates;return true;
    }
    bool composite(IDirect3DDevice9* d,IDirect3DSurface9* output,unsigned w,unsigned h){
        d->SetTexture(0,nullptr);d->SetTexture(2,nullptr);
        if(FAILED(d->SetRenderTarget(0,output)))return false;
        sampler(d,0);d->SetTexture(0,texture[result]);d->SetPixelShader(resolve);
        D3DVIEWPORT9 vp={0,0,w,h,0,1};d->SetViewport(&vp);
        const bool ok=SUCCEEDED(NorthlightScreen::drawQuad(d,w,h));d->SetTexture(0,nullptr);return ok;
    }
};
