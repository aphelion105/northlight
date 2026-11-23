#pragma once
#include "geometry_memory.h"
#include "regional_fog.h"
#include "screen_quad.h"
#include "shadow_transition_compiled_shaders.h"
#include "world_compiled_shaders.h"
#include <d3d9.h>
#include <algorithm>
#include <cstring>
#include <memory>
#include <string>

// The pass borrows the published regional field and draws at the caller's
// fog boundary. Directional shadow validity remains with the frame planner.
class NorthlightWorldMoonFloorFog {
public:
    struct Input {
        IDirect3DPixelShader9* baseline;
        IDirect3DTexture9* scene;
        const float* bounds;
        const std::shared_ptr<const NorthlightRegionalFog::Field>& field;
        const std::string& map;
        bool eligible,actor,tiled,fading;
    };

    NorthlightWorldMoonFloorFog()=default;
    NorthlightWorldMoonFloorFog(const NorthlightWorldMoonFloorFog&)=delete;
    NorthlightWorldMoonFloorFog& operator=(const NorthlightWorldMoonFloorFog&)=delete;
    ~NorthlightWorldMoonFloorFog(){reset();}

    IDirect3DTexture9* texture() const {return texture_;}

    void invalidateField(){
        ready_=false;
        field_.reset();
        map_.clear();
        minimum_=1e20f;
    }

    void reset(){
        invalidateField();
        drop(texture_);
        for(unsigned i=0;i<6;++i){drop(shaders_[i]);tried_[i]=false;}
    }

    template<class Admit>
    HRESULT draw(IDirect3DDevice9* d,const Input& in,UINT width,UINT height,const Admit& admit){
        auto* optional=in.eligible?moonFloorShader(d,in.actor,in.tiled,in.fading):nullptr;
        bool use=optional&&ensureMoonFloor(d,in.field,in.map,admit);
        const float actorInfo[4]={in.actor?1.f:0.f,0,0,0};
        const auto restore=[&]{
            HRESULT result=S_OK;
            for(unsigned attempt=0;attempt<2;++attempt){
                result=S_OK;
                const auto record=[&](HRESULT hr){if(FAILED(hr)&&SUCCEEDED(result))result=hr;};
                record(d->SetTexture(0,in.scene));
                record(d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR));
                record(d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR));
                record(d->SetPixelShaderConstantF(72,actorInfo,1));
                record(d->SetPixelShaderConstantF(65,in.bounds,1));
                record(d->SetPixelShader(in.baseline));
                if(SUCCEEDED(result))break;
            }
            return result;
        };
        if(use){
            const float support[4]={actorInfo[0],1,1,1};
            float extended[4];std::memcpy(extended,in.bounds,sizeof extended);
            extended[0]=std::min(extended[0],minimum_-.25f);
            use=SUCCEEDED(d->SetTexture(0,texture_))&&
                SUCCEEDED(d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT))&&
                SUCCEEDED(d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT))&&
                SUCCEEDED(d->SetPixelShaderConstantF(72,support,1))&&
                SUCCEEDED(d->SetPixelShaderConstantF(65,extended,1))&&SUCCEEDED(d->SetPixelShader(optional));
            if(!use){const HRESULT restored=restore();if(FAILED(restored))return restored;}
        }
        if(!use)d->SetPixelShader(in.baseline);
        const D3DVIEWPORT9 viewport{0,0,width,height,0,1};
        d->SetViewport(&viewport);
        const HRESULT result=NorthlightScreen::drawQuad(d,width,height);
        if(use){const HRESULT restored=restore();if(FAILED(restored)&&SUCCEEDED(result))return restored;}
        return result;
    }

private:
    friend struct WorldScreenEffectsTest;
    IDirect3DTexture9* texture_=nullptr;
    std::shared_ptr<const NorthlightRegionalFog::Field> field_;
    std::string map_;
    bool ready_=false;
    float minimum_=1e20f;
    IDirect3DPixelShader9* shaders_[6]={};
    bool tried_[6]={};

    template<class T> static void drop(T*& object){if(object){object->Release();object=nullptr;}}

    IDirect3DPixelShader9* moonFloorShader(IDirect3DDevice9* d,bool actor,bool tiled,bool fading){
        const unsigned index=(fading?3:0)+(actor?(tiled?2:1):0);
        if(tried_[index])return shaders_[index];
        tried_[index]=true;
        const DWORD* code[]={kMoonFloorFogShader,kMoonFloorFogActorShader,kMoonFloorFogActorTiledShader,kMoonFloorFogTransitionShader,kMoonFloorFogActorTransitionShader,kMoonFloorFogActorTiledTransitionShader};
        const unsigned slots[]={kMoonFloorFogShaderSlots,kMoonFloorFogActorShaderSlots,kMoonFloorFogActorTiledShaderSlots,kMoonFloorFogTransitionShaderSlots,kMoonFloorFogActorTransitionShaderSlots,kMoonFloorFogActorTiledTransitionShaderSlots};
        const unsigned temps[]={24,26,27,24,27,28};D3DCAPS9 caps{};
        if(FAILED(d->GetDeviceCaps(&caps))||caps.MaxPixelShader30InstructionSlots<slots[index]||caps.PS20Caps.NumTemps<temps[index])return nullptr;
        if(FAILED(d->CreatePixelShader(code[index],&shaders_[index])))drop(shaders_[index]);
        return shaders_[index];
    }

    template<class Admit>
    bool ensureMoonFloor(IDirect3DDevice9* d,const std::shared_ptr<const NorthlightRegionalFog::Field>& field,const std::string& map,const Admit& admit){
        if(field_==field&&map_==map)return ready_;
        invalidateField();field_=field;map_=map;
        if(!field_)return false;
        constexpr unsigned n=NorthlightRegionalFog::N;
        for(unsigned y=0;y<n;++y)for(unsigned x=0;x<n;++x){
            const auto corners=NorthlightRegionalFog::directFloorCorners(*field_,x,y);
            for(float z:corners)minimum_=std::min(minimum_,z);
        }
        if(minimum_>=1e19f)return false;
        if(!texture_){
            if(!admit(NorthlightGeometryMemory::dynamicBudget(uint64_t(n)*n*16)))return false;
            if(FAILED(d->CreateTexture(n,n,1,0,D3DFMT_A32B32G32R32F,D3DPOOL_MANAGED,&texture_,nullptr))){drop(texture_);return false;}
        }
        D3DLOCKED_RECT lock{};
        if(FAILED(texture_->LockRect(0,&lock,nullptr,0)))return false;
        if(!lock.pBits||lock.Pitch<INT(n*16)){texture_->UnlockRect(0);return false;}
        for(unsigned y=0;y<n;++y)for(unsigned x=0;x<n;++x){
            const auto corners=NorthlightRegionalFog::directFloorCorners(*field_,x,y);
            std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch+x*16,corners.data(),16);
        }
        ready_=SUCCEEDED(texture_->UnlockRect(0));
        return ready_;
    }
};
