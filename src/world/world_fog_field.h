#pragma once
#include "regional_fog.h"
#include <d3d9.h>
#include <memory>

// Owns the managed textures and the field whose data they contain. The
// render thread selects publications and shares read-only inputs with jobs.
class NorthlightWorldFogField {
public:
    using Field = NorthlightRegionalFog::Field;

    NorthlightWorldFogField()=default;
    NorthlightWorldFogField(const NorthlightWorldFogField&)=delete;
    NorthlightWorldFogField& operator=(const NorthlightWorldFogField&)=delete;
    ~NorthlightWorldFogField(){reset();}

    const std::shared_ptr<const Field>& field() const {return field_;}
    const std::string& map() const {return map_;}
    const std::array<float,3>& globalHeights() const {return globalHeights_;}
    IDirect3DTexture9* texture() const {return texture_;}
    IDirect3DTexture9* heightAllocation() const {return height_;}
    IDirect3DTexture9* heightTexture() const {return heightReady_?height_:nullptr;}

    void reset(){
        releaseTextures();
        field_.reset();
        retry_=false;
    }

    void bindVolumeInputs(IDirect3DDevice9* d) const {
        d->SetTexture(13,texture_); // Replaces the model-normal capture input.
        d->SetTexture(12,heightTexture());
    }

    template<class Clock,class Check,class Warn,class Invalidate>
    bool upload(IDirect3DDevice9* d,const std::shared_ptr<const Field>& field,const std::string& map,
                const Clock& clock,const Check& check,const Warn& warn,const Invalidate& invalidate){
        if(field_==field&&map_==map)return true;
        invalidate();
        heightReady_=false;
        globalHeights_={};
        if(!field){retry_=false;field_.reset();map_=map;return true;}
        const DWORD now=clock();
        if(retry_&&DWORD(now-retryAt_)<1000)return false;
        const auto required=[&](HRESULT hr,const char* stage){
            if(SUCCEEDED(hr))return true;
            if(hr!=E_OUTOFMEMORY&&hr!=D3DERR_OUTOFVIDEOMEMORY)return check(hr,stage);
            releaseTextures();
            field_.reset();
            retry_=true;
            retryAt_=now;
            if(!reported_||DWORD(now-reportAt_)>=60000){
                reported_=true;
                reportAt_=now;
                warn(hr,stage);
            }
            return false;
        };
        constexpr unsigned n=NorthlightRegionalFog::N;
        constexpr auto rowBytes=n*sizeof(NorthlightRegionalFog::Texel);
        const auto create=[&](IDirect3DTexture9** out){
            return d->CreateTexture(n,n,1,0,D3DFMT_A32B32G32R32F,D3DPOOL_MANAGED,out,nullptr);
        };
        if(!texture_&&!required(create(&texture_),"regional fog field"))return false;
        D3DLOCKED_RECT lock{};
        if(!required(texture_->LockRect(0,&lock,nullptr,0),"regional fog lock"))return false;
        if(!lock.pBits||lock.Pitch<INT(rowBytes)){
            texture_->UnlockRect(0);
            return check(E_FAIL,"regional fog layout");
        }
        for(unsigned y=0;y<n;++y)
            std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch,field->texels.data()+y*n,rowBytes);
        if(!required(texture_->UnlockRect(0),"regional fog unlock"))return false;

        retry_=false;
        field_=field;
        map_=map;
        globalHeights_=NorthlightRegionalFog::globalHeightBounds(*field);
        // The baseline march still works if the optional height metadata fails.
        if(!height_&&FAILED(create(&height_))){drop(height_);return true;}
        D3DLOCKED_RECT bounds{};
        if(!height_||FAILED(height_->LockRect(0,&bounds,nullptr,0)))return true;
        if(!bounds.pBits||bounds.Pitch<INT(rowBytes)){height_->UnlockRect(0);return true;}
        for(unsigned y=0;y<n;++y)for(unsigned x=0;x<n;++x){
            const auto heights=NorthlightRegionalFog::heightBounds(*field,x,y);
            std::memcpy(static_cast<char*>(bounds.pBits)+y*bounds.Pitch+x*sizeof(heights),heights.data(),sizeof(heights));
        }
        heightReady_=SUCCEEDED(height_->UnlockRect(0));
        return true;
    }

private:
    friend struct WorldScreenEffectsTest;
    static void drop(IDirect3DTexture9*& texture){if(texture){texture->Release();texture=nullptr;}}
    void releaseTextures(){drop(texture_);drop(height_);heightReady_=false;}

    IDirect3DTexture9* texture_=nullptr;
    IDirect3DTexture9* height_=nullptr;
    std::shared_ptr<const Field> field_;
    std::string map_;
    std::array<float,3> globalHeights_{};
    bool heightReady_=false,retry_=false,reported_=false;
    DWORD retryAt_=0,reportAt_=0;
};
