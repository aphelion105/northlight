#pragma once
#include <d3d9.h>
#include <initializer_list>
#include "world_compiled_shaders.h"
#include "shadow_transition_compiled_shaders.h"

class NorthlightWorldForeverFogShaders {
public:
    struct Selection {
        IDirect3DPixelShader9* shader;
        bool actor,quiet,tiled;
    };

    NorthlightWorldForeverFogShaders()=default;
    NorthlightWorldForeverFogShaders(const NorthlightWorldForeverFogShaders&)=delete;
    NorthlightWorldForeverFogShaders& operator=(const NorthlightWorldForeverFogShaders&)=delete;
    ~NorthlightWorldForeverFogShaders(){reset();}

    bool available()const{return base_!=nullptr;}
    bool createBase(IDirect3DDevice9* d){
        return base_||SUCCEEDED(d->CreatePixelShader(kForeverFogShader,&base_));
    }

    void createRaymarchVariants(IDirect3DDevice9* d){
        if(tried_||!base_)return;
        tried_=true;
        D3DCAPS9 caps={};
        if(SUCCEEDED(d->GetDeviceCaps(&caps))&&caps.MaxPixelShader30InstructionSlots>=kForeverFogActorShaderSlots)
            d->CreatePixelShader(kForeverFogActorShader,&actor_);
        if(caps.MaxPixelShader30InstructionSlots>=kForeverFogTransitionShaderSlots)
            d->CreatePixelShader(kForeverFogTransitionShader,&fade_);
        if(caps.MaxPixelShader30InstructionSlots>=kForeverFogActorTransitionShaderSlots)
            d->CreatePixelShader(kForeverFogActorTransitionShader,&actorFade_);
        if(caps.MaxPixelShader30InstructionSlots>=kForeverFogActorTiledShaderSlots)
            d->CreatePixelShader(kForeverFogActorTiledShader,&actorTiled_);
        if(caps.MaxPixelShader30InstructionSlots>=kForeverFogActorTiledNoNoiseShaderSlots)
            d->CreatePixelShader(kForeverFogActorTiledNoNoiseShader,&actorTiledQuiet_);
        d->CreatePixelShader(kForeverFogNoNoiseShader,&quiet_);
        if(caps.MaxPixelShader30InstructionSlots>=kForeverFogActorNoNoiseShaderSlots)
            d->CreatePixelShader(kForeverFogActorNoNoiseShader,&actorQuiet_);
        if(caps.MaxPixelShader30InstructionSlots>=kForeverFogNoNoiseTransitionShaderSlots)
            d->CreatePixelShader(kForeverFogNoNoiseTransitionShader,&quietFade_);
        if(caps.MaxPixelShader30InstructionSlots>=kForeverFogActorNoNoiseTransitionShaderSlots)
            d->CreatePixelShader(kForeverFogActorNoNoiseTransitionShader,&actorQuietFade_);
    }

    Selection select(bool requestActor,bool requestTiles,bool fading,bool noise)const{
        const bool actor=requestActor&&actor_;
        auto* shader=fading?(actor&&actorFade_?actorFade_:fade_):(actor?actor_:base_);
        if(!fading&&actor&&requestTiles&&actorTiled_)shader=actorTiled_;
        if(!shader)shader=base_;
        bool usesQuiet=false;
        if(!noise){
            auto* quiet=fading?(actor?actorQuietFade_:quietFade_):(actor?actorQuiet_:quiet_);
            if(!fading&&actor&&requestTiles&&actorTiledQuiet_)quiet=actorTiledQuiet_;
            if(quiet){shader=quiet;usesQuiet=true;}
        }
        return {shader,actor,usesQuiet,shader&&(shader==actorTiled_||shader==actorTiledQuiet_)};
    }

    void reset(){
        for(auto** shader:{&actorTiled_,&actorTiledQuiet_,&quiet_,&actorQuiet_,&quietFade_,
                           &actorQuietFade_,&base_,&actor_,&fade_,&actorFade_}){
            if(*shader){(*shader)->Release();*shader=nullptr;}
        }
        tried_=false;
    }

private:
    IDirect3DPixelShader9 *base_=nullptr,*actor_=nullptr,*fade_=nullptr,*actorFade_=nullptr;
    IDirect3DPixelShader9 *quiet_=nullptr,*actorQuiet_=nullptr,*quietFade_=nullptr,*actorQuietFade_=nullptr;
    IDirect3DPixelShader9 *actorTiled_=nullptr,*actorTiledQuiet_=nullptr;
    bool tried_=false;
};
