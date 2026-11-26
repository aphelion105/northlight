#pragma once
#include <d3d9.h>
#include "world_local_scissor.h"
#include "screen_quad.h"
#include "world_compiled_shaders.h"

// Applies the shadow correction for one selected cube to the existing light
// target. The frame owns the target, cube and screen inputs.
class NorthlightWorldPointLighting {
    friend struct WorldScreenEffectsTest;
    friend struct PipelineOffTest;
    IDirect3DPixelShader9* hard_=nullptr;
    IDirect3DPixelShader9* soft_=nullptr;
    IDirect3DPixelShader9* glint_[2]={};
    bool glintTried_[2]={};
    bool softTried_=false;

    static void drop(IDirect3DPixelShader9*& shader){if(shader){shader->Release();shader=nullptr;}}
public:
    struct Inputs {
        IDirect3DTexture9 *depth,*water,*normals,*sunVisibility,*material;
        IDirect3DCubeTexture9* cube;
        UINT width,height;
        float constants[12][4]={};
        float bump[4]={};
        float cubeOffset[4]={};
    };

    NorthlightWorldPointLighting()=default;
    NorthlightWorldPointLighting(const NorthlightWorldPointLighting&)=delete;
    NorthlightWorldPointLighting& operator=(const NorthlightWorldPointLighting&)=delete;
    ~NorthlightWorldPointLighting(){reset(false);}

    IDirect3DPixelShader9* hardShader()const{return hard_;}
    IDirect3DPixelShader9* softShader()const{return soft_;}
    HRESULT createHard(IDirect3DDevice9* d){return d->CreatePixelShader(kLocalLightingShader,&hard_);}
    template<class Unavailable> bool ensureSoft(IDirect3DDevice9* d,const Unavailable& unavailable){
        if(!softTried_){
            softTried_=true;
            if(FAILED(d->CreatePixelShader(kLocalLightingPcssShader,&soft_))){
                drop(soft_);unavailable();
            }
        }
        return soft_!=nullptr;
    }
    void reset(bool preserveShaders){
        if(!preserveShaders){drop(hard_);drop(soft_);for(auto*& shader:glint_)drop(shader);}
        for(unsigned i=0;i<2;++i)glintTried_[i]=glint_[i]!=nullptr;
        softTried_=soft_!=nullptr;
    }

    template<class Check,class Unavailable,class Record>
    bool draw(IDirect3DDevice9* d,const Inputs& in,bool enableSoft,bool* sharedState,
              NorthlightLocalLightScissor::DrawState& scissor,const NorthlightLocalLightScissor::View& view,
              const Check& check,const Unavailable& unavailable,const Record& record){
        const bool soft=enableSoft&&ensureSoft(d,unavailable)&&SUCCEEDED(d->SetPixelShader(soft_));
        if(!soft&&!check(d->SetPixelShader(hard_),"local lighting PS"))return false;
        if(in.cubeOffset[3]>0){
            if(!glintTried_[soft]){
                glintTried_[soft]=true;
                D3DCAPS9 caps{};
                if(!soft||(SUCCEEDED(d->GetDeviceCaps(&caps))&&caps.MaxPixelShader30InstructionSlots>=kLocalLightingGlintPcssShaderSlots)){
                    if(FAILED(d->CreatePixelShader(soft?kLocalLightingGlintPcssShader:kLocalLightingGlintShader,&glint_[soft])))drop(glint_[soft]);
                }
            }
            if(glint_[soft]&&!check(d->SetPixelShader(glint_[soft]),"local reflection PS"))return false;
        }
        if(!check(d->SetPixelShaderConstantF(0,&in.constants[0][0],12),"local constants")||
           !check(d->SetPixelShaderConstantF(13,in.cubeOffset,1),"local cube origin")||
           !check(d->SetTexture(1,in.cube),"local cube"))return false;
        if(!sharedState||!*sharedState){
            if(!check(d->SetPixelShaderConstantF(12,in.bump,1),"local material constants")||
               !check(d->SetTexture(5,in.material),"local material texture"))return false;
            const struct{D3DSAMPLERSTATETYPE state;DWORD value;} materialStates[]={{D3DSAMP_MINFILTER,D3DTEXF_POINT},{D3DSAMP_MAGFILTER,D3DTEXF_POINT},{D3DSAMP_MIPFILTER,D3DTEXF_NONE},{D3DSAMP_SRGBTEXTURE,FALSE},{D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP},{D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP}};
            for(const auto& state:materialStates)if(!check(d->SetSamplerState(5,state.state,state.value),"local material sampler"))return false;
            if(!check(d->SetTexture(0,in.depth),"local depth")||
               !check(d->SetTexture(2,in.water),"local water")||
               !check(d->SetTexture(3,in.normals),"local normals")||
               !check(d->SetTexture(4,in.sunVisibility),"local sun visibility")||
               !check(d->SetRenderTarget(1,nullptr),"local baseline unbind")||
               !check(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE),"local additive blend")||
               !check(d->SetRenderState(D3DRS_COLORWRITEENABLE,7),"local RGB mask"))return false;
            for(unsigned sampler=0;sampler<5;++sampler){
                const struct{D3DSAMPLERSTATETYPE state;DWORD value;} states[]={{D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP},{D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP},{D3DSAMP_ADDRESSW,D3DTADDRESS_CLAMP},{D3DSAMP_MINFILTER,D3DTEXF_POINT},{D3DSAMP_MAGFILTER,D3DTEXF_POINT},{D3DSAMP_MIPFILTER,D3DTEXF_NONE},{D3DSAMP_SRGBTEXTURE,FALSE}};
                for(const auto& state:states)if(!check(d->SetSamplerState(sampler,state.state,state.value),"local sampler"))return false;
            }
            if(sharedState)*sharedState=true;
        }
        // An unavailable scissor rectangle may leave the pass unscissored.
        d->SetRenderState(D3DRS_SCISSORTESTENABLE,NorthlightLocalLightScissor::Enabled);
        // The correction is exactly zero beyond attenuationEnd (LocalColor.w).
        const float sphere[]={in.constants[7][0],in.constants[7][1],in.constants[7][2],in.constants[8][3]};
        if(!scissor.applySphere(d,view,sphere)){d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);return true;}
        const D3DVIEWPORT9 viewport={0,0,in.width,in.height,0,1};
        d->SetViewport(&viewport);
        const HRESULT result=NorthlightScreen::drawQuad(d,in.width,in.height);
        const bool drawn=check(result,"local lighting pass");
        record(soft,result);
        d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
        return drawn;
    }
};
