#pragma once
#include "grade_lut_worker.h"
#include "hdr_compiled_shaders.h"
#include "world_composite.h"

class NorthlightWorldAppearance {
    NorthlightGradeLut::Worker worker;
    NorthlightTone::BakeKeys requested{};
    uint64_t attempted=0;
    std::array<IDirect3DVolumeTexture9*,NorthlightTone::StopCount> tables{};
    IDirect3DPixelShader9* shader=nullptr;
    IDirect3DTexture9* colour=nullptr;
    IDirect3DSurface9* surface=nullptr;
    D3DSURFACE_DESC layout{};

    template<class T> static void release(T*& pointer){
        if(pointer)pointer->Release();
        pointer=nullptr;
    }

    static IDirect3DVolumeTexture9* upload(IDirect3DDevice9* d,const NorthlightGradeLut::Volume& volume){
        constexpr UINT n=NorthlightTone::CubeSize;
        constexpr UINT rowBytes=n*4*sizeof(uint16_t);
        IDirect3DVolumeTexture9 *texture=nullptr,*staging=nullptr;
        if(FAILED(d->CreateVolumeTexture(n,n,n,1,0,D3DFMT_A16B16G16R16,D3DPOOL_SYSTEMMEM,&staging,nullptr)))
            return nullptr;
        D3DLOCKED_BOX box{};
        bool ready=false;
        if(SUCCEEDED(staging->LockBox(0,&box,nullptr,0))){
            ready=box.pBits&&box.RowPitch>=INT(rowBytes)&&box.SlicePitch>=box.RowPitch*INT(n);
            if(ready){
                const auto* source=reinterpret_cast<const uint8_t*>(volume.rgba.data());
                auto* destination=static_cast<uint8_t*>(box.pBits);
                for(UINT slice=0;slice<n;++slice)for(UINT y=0;y<n;++y)
                    std::memcpy(destination+slice*box.SlicePitch+y*box.RowPitch,source+(slice*n+y)*rowBytes,rowBytes);
            }
            ready=SUCCEEDED(staging->UnlockBox(0))&&ready;
        }
        if(ready&&SUCCEEDED(d->CreateVolumeTexture(n,n,n,1,0,D3DFMT_A16B16G16R16,D3DPOOL_DEFAULT,&texture,nullptr)))
            if(FAILED(d->UpdateTexture(staging,texture)))release(texture);
        staging->Release();
        return texture;
    }

public:
    ~NorthlightWorldAppearance(){reset();}
    void reset(){
        for(auto& table:tables)release(table);
        release(shader);release(surface);release(colour);
        attempted=0;layout={};
    }

    template<class State,class Quad>
    bool draw(IDirect3DDevice9* d,IDirect3DSurface9* target,IDirect3DSurface9* nameDepth,
              const NorthlightTone::BakeKeys& keys,const NorthlightTone::TimeWeights& time,bool hdr,State state,Quad quad){
        if(requested!=keys){worker.request(keys);requested=keys;}
        if(std::none_of(keys.begin(),keys.end(),[](const auto& key){return key.strength!=0;}))return false;
        const auto result=worker.ready(keys);
        if(!result)return false;
        if(result->serial!=attempted){
            attempted=result->serial;
            for(unsigned i=0;i<tables.size();++i){
                release(tables[i]);
                if(result->volumes[i])tables[i]=upload(d,*result->volumes[i]);
            }
        }
        float weights[4]={tables[0]?time.day:0.f,tables[1]?time.dusk:0.f,tables[2]?time.night:0.f,hdr?1.f:0.f};
        if(weights[0]+weights[1]+weights[2]<=0.f)return false;
        if(!shader&&FAILED(d->CreatePixelShader(reinterpret_cast<const DWORD*>(kRegionalAppearance),&shader)))return false;
        D3DSURFACE_DESC desc{};
        if(!target||FAILED(target->GetDesc(&desc)))return false;
        if(desc.Width!=layout.Width||desc.Height!=layout.Height||desc.Format!=layout.Format){
            release(surface);release(colour);layout=desc;
        }
        const auto check=[](HRESULT hr,const char*){return SUCCEEDED(hr);};
        if(!NorthlightWorldRenderTarget::ensure(d,desc.Width,desc.Height,desc.Format,colour,surface,check,
                                                "appearance scene copy","appearance scene surface"))return false;
        state(); // The enclosing frame state owns restoration.
        if(FAILED(d->StretchRect(target,nullptr,surface,nullptr,D3DTEXF_NONE))||
           FAILED(d->SetRenderTarget(0,target))||FAILED(d->SetTexture(0,colour))||
           FAILED(d->SetPixelShader(shader))||FAILED(d->SetPixelShaderConstantF(1,weights,1)))return false;
        for(unsigned i=0;i<tables.size();++i){
            const auto slot=i+1;
            if(FAILED(d->SetTexture(slot,tables[i]))||FAILED(d->SetSamplerState(slot,D3DSAMP_ADDRESSW,D3DTADDRESS_CLAMP))||
               FAILED(d->SetSamplerState(slot,D3DSAMP_MINFILTER,D3DTEXF_LINEAR))||
               FAILED(d->SetSamplerState(slot,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR)))return false;
        }
        NameMaskCompositeBind mask(d,nameDepth);
        return SUCCEEDED(quad());
    }
};
