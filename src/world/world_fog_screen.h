#pragma once
#include <utility>
#include "screen_quad.h"
#include "world_compiled_shaders.h"

// Owns the three half-resolution screen targets. RemovalSmooth borrows
// scratch before atmospheric filtering. The volume atlas has its own owner.
class NorthlightFogScreen {
public:
    struct Image {
        IDirect3DTexture9* texture=nullptr;
        IDirect3DSurface9* surface=nullptr;
    };
    struct HistoryInput {
        UINT width, height;
        bool enabled, active, volumeAtlas, useHistory, clouds;
        const float* previousView;
        IDirect3DTexture9* previousDistance;
    };
    struct HistoryResult {
        bool resolved=false;
        bool neutralClearSkipped=false;
    };

    const Image& raw() const { return raw_; }
    const Image& scratch() const { return scratch_; }
    const Image& history() const { return history_; }
    const Image& final(bool resolved) const { return resolved?scratch_:raw_; }
    bool rawNeutral() const { return rawNeutral_; }
    void beginRawWrite() { rawNeutral_=false; }
    void invalidateHistory() { historyValid_=false; }

    template<class ImageFactory> bool createScratch(const ImageFactory& image){
        return image(D3DFMT_A16B16G16R16F,scratch_.texture,scratch_.surface);
    }

    template<class ShaderFactory,class ImageFactory>
    bool create(const ShaderFactory& pixel,const ImageFactory& image){
        return pixel(kFogBlurShader,blurShader_,"volume blur shader")&&
               image(D3DFMT_A16B16G16R16F,raw_.texture,raw_.surface)&&
               createScratch(image)&&image(D3DFMT_A16B16G16R16F,history_.texture,history_.surface);
    }

    void createOptionalHistory(IDirect3DDevice9* d){
        if(!temporalShader_&&FAILED(d->CreatePixelShader(kFogTemporalShader,&temporalShader_)))
            temporalShader_=nullptr;
    }

    void reset(){
        for(auto* image:{&raw_,&scratch_,&history_})drop(image->surface);
        for(auto* image:{&raw_,&scratch_,&history_})drop(image->texture);
        drop(temporalShader_);
        drop(blurShader_);
        historyValid_=rawNeutral_=historyNeutral_=false;
    }

    template<class Check> bool clearNeutral(IDirect3DDevice9* d,UINT width,UINT height,const Check& check){
        if(!raw_.texture||rawNeutral_)return true;
        // The neutral composite operand is (0,0,0,1).
        d->SetTexture(9,nullptr);
        viewport(d,width,height);
        if(!check(d->SetRenderTarget(0,raw_.surface),"neutral fog target")||
           !check(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0),"neutral fog clear"))return false;
        rawNeutral_=true;
        return true;
    }

    template<class Check>
    bool resolveHistory(IDirect3DDevice9* d,const HistoryInput& in,HistoryResult& result,const Check& check){
        result={};
        if(!in.enabled||!temporalShader_||!history_.texture){
            invalidateHistory();
            return true;
        }
        if(!in.active){
            d->SetTexture(14,nullptr);
            result.neutralClearSkipped=historyNeutral_&&historyValid_;
            if(!result.neutralClearSkipped){
                if(!check(d->SetRenderTarget(0,history_.surface),"neutral fog history target")||
                   !check(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0),"neutral fog history clear"))return false;
                historyNeutral_=true;
            }
            historyValid_=true;
            return true;
        }
        if(in.volumeAtlas){
            invalidateHistory();
            return true;
        }

        historyNeutral_=false;
        const float weight=in.clouds?.5f:.85f;
        const float info[4]={0,historyValid_&&in.useHistory?weight:0.f,0,0};
        d->SetPixelShaderConstantF(53,in.previousView,4);
        d->SetPixelShaderConstantF(64,info,1);
        d->SetRenderTarget(0,scratch_.surface);
        d->SetTexture(9,raw_.texture);
        d->SetTexture(14,history_.texture);
        d->SetTexture(15,in.previousDistance);
        for(unsigned sampler=14;sampler<16;++sampler){
            d->SetSamplerState(sampler,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
            d->SetSamplerState(sampler,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
            d->SetSamplerState(sampler,D3DSAMP_MINFILTER,sampler==14?D3DTEXF_LINEAR:D3DTEXF_POINT);
            d->SetSamplerState(sampler,D3DSAMP_MAGFILTER,sampler==14?D3DTEXF_LINEAR:D3DTEXF_POINT);
            d->SetSamplerState(sampler,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
            d->SetSamplerState(sampler,D3DSAMP_SRGBTEXTURE,FALSE);
        }
        d->SetPixelShader(temporalShader_);
        const bool resolved=check(quad(d,in.width,in.height),"fog temporal pass");
        d->SetTexture(14,nullptr);
        d->SetTexture(15,nullptr);
        d->SetSamplerState(14,D3DSAMP_MINFILTER,D3DTEXF_POINT);
        d->SetSamplerState(14,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
        if(!resolved){invalidateHistory();return false;}

        // Rotate texture and surface together after both input slots are free.
        std::swap(scratch_,history_);
        historyValid_=result.resolved=true;
        return true;
    }

    template<class Check>
    bool blur(IDirect3DDevice9* d,UINT width,UINT height,IDirect3DTexture9* currentDistance,
              bool active,bool resolved,const Check& check){
        d->SetTexture(15,currentDistance);
        d->SetSamplerState(15,D3DSAMP_MINFILTER,D3DTEXF_POINT);
        d->SetSamplerState(15,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
        d->SetSamplerState(15,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
        d->SetSamplerState(15,D3DSAMP_SRGBTEXTURE,FALSE);
        d->SetSamplerState(15,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
        d->SetSamplerState(15,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
        if(!active)return true;

        const Image& horizontal=resolved?raw_:scratch_;
        float step[4]={2,0,0,0};
        d->SetPixelShader(blurShader_);
        d->SetRenderTarget(0,horizontal.surface);
        d->SetTexture(9,(resolved?history_:raw_).texture);
        d->SetPixelShaderConstantF(34,step,1);
        if(!check(quad(d,width,height),"volume blur horizontal"))return false;
        step[0]=0;step[1]=2;
        d->SetRenderTarget(0,final(resolved).surface);
        d->SetTexture(9,horizontal.texture);
        d->SetPixelShaderConstantF(34,step,1);
        return check(quad(d,width,height),"volume blur vertical");
    }

private:
    friend struct WorldScreenEffectsTest;
    Image raw_,scratch_,history_;
    IDirect3DPixelShader9 *blurShader_=nullptr,*temporalShader_=nullptr;
    bool historyValid_=false,rawNeutral_=false,historyNeutral_=false;

    template<class T> static void drop(T*& pointer){
        if(pointer){pointer->Release();pointer=nullptr;}
    }
    static void viewport(IDirect3DDevice9* d,UINT width,UINT height){
        const D3DVIEWPORT9 vp={0,0,width,height,0,1};
        d->SetViewport(&vp);
    }
    static HRESULT quad(IDirect3DDevice9* d,UINT width,UINT height){
        viewport(d,width,height);
        return NorthlightScreen::drawQuad(d,width,height);
    }
};
