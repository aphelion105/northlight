#pragma once
#include "screen_quad.h"
#include "actor_shadow_map.h"
#include "world_compiled_shaders.h"
#include <d3d9.h>
#include <algorithm>

class NorthlightWorldActorShadows {
public:
    struct Target {
        IDirect3DTexture9* texture=nullptr;
        IDirect3DSurface9* surface=nullptr;
    };
    static constexpr float Info[]={1.f,1.f/float(NorthlightActorShadowMap::Size),NorthlightActorShadowMap::Radius,0.f};

    const Target& depth(unsigned source) const {return depth_[source];}
    const Target& tiles(unsigned source) const {return tiles_[source];}
    const Target& visibility() const {return visibility_;}
    template<class Device>
    void bindLightingVisibility(Device* d,bool useMask,IDirect3DTexture9* neutral) const {
        d->SetTexture(12,useMask?visibility_.texture:neutral);
    }
    IDirect3DPixelShader9* fogTiled() const {return fogTiled_;}
    IDirect3DPixelShader9* cloudsTiled() const {return cloudsTiled_;}
    bool pending() const {return pending_;}
    void completeGeometry(){pending_=false;}
    bool ready(unsigned source) const {
        return filter_&&tilesShader_&&depth(source).texture&&tiles(source).texture&&tileScratch_.texture&&visibility_.texture;
    }
    template<class Visit> void forEachTexture(const Visit& visit) const {
        for(const auto& target:depth_)visit(target.texture);
        for(const auto& target:tiles_)visit(target.texture);
        visit(tileScratch_.texture);
        visit(visibility_.texture);
    }

    bool createShaders(IDirect3DDevice9* d,const D3DCAPS9& caps,bool enabled,bool hasActorFog){
        if(!enabled)return true;
        if(!filter_&&(FAILED(d->CreatePixelShader(kActorShadowShader,&filter_))||
                      FAILED(d->CreatePixelShader(kActorTilesShader,&tilesShader_))||
                      FAILED(d->CreatePixelShader(kActorShadowUniformShader,&uniform_)))){
            drop(filter_);drop(uniform_);drop(tilesShader_);
            return false;
        }
        if(!tilesWide_&&tilesShader_&&hasActorFog&&
           caps.MaxPixelShader30InstructionSlots>=std::max(kWorldFogActorTiledShaderSlots,kFogCloudsActorTiledShaderSlots)){
            if(FAILED(d->CreatePixelShader(kActorTilesWideShader,&tilesWide_))||
               FAILED(d->CreatePixelShader(kWorldFogActorTiledShader,&fogTiled_))||
               FAILED(d->CreatePixelShader(kFogCloudsActorTiledShader,&cloudsTiled_))){
                drop(tilesWide_);drop(fogTiled_);drop(cloudsTiled_);
            }
        }
        return true;
    }

    template<class Allocate> bool createTargets(UINT w,UINT h,const Allocate& allocate){
        if(!filter_)return true;
        const auto square=[&](UINT size,Target& target){
            return allocate(size,size,D3DFMT_R32F,&target.texture,&target.surface);
        };
        // Preserve staged allocation order across prewarm frames.
        return square(Size,depth_[0])&&square(Size,depth_[1])&&square(Mid,tileScratch_)&&
               square(Tiles,tiles_[0])&&square(Tiles,tiles_[1])&&
               allocate(w/2,h/2,D3DFMT_A8R8G8B8,&visibility_.texture,&visibility_.surface);
    }
    void releaseTextures(){
        for(auto& target:depth_)drop(target.texture);
        for(auto& target:depth_)drop(target.surface);
        for(auto& target:tiles_)drop(target.texture);
        for(auto& target:tiles_)drop(target.surface);
        drop(tileScratch_.texture);drop(tileScratch_.surface);
        drop(visibility_.texture);drop(visibility_.surface);
    }
    void dropResources(){
        releaseTextures();
        drop(filter_);drop(uniform_);drop(tilesShader_);
        drop(tilesWide_);drop(fogTiled_);drop(cloudsTiled_);
    }
    void reset(){dropResources();pending_=false;}
    void settingsChanged(){dropResources();pending_=true;}

    template<class Check>
    bool buildTiles(IDirect3DDevice9* d,const bool (&active)[2],bool fog,const float (&matrices)[2][16],
                    const float* camera,float inverseDepth,bool (&wideCurrent)[2],const Check& check) const {
        d->SetPixelShader(tilesShader_);
        const float midInfo[]={1.f,1.f/float(Mid),NorthlightActorShadowMap::Radius,0.f};
        for(int source=0;source<2;++source){
            if(!active[source])continue;
            if(!check(d->SetRenderTarget(0,tileScratch_.surface),"actor tile target"))return false;
            d->SetTexture(12,depth(source).texture);d->SetPixelShaderConstantF(72,Info,1);
            if(!check(NorthlightScreen::drawViewportQuad(d,Mid,Mid),"actor shadow tile pass"))return false;
            if(!check(d->SetRenderTarget(0,tiles(source).surface),"actor tile target"))return false;
            d->SetTexture(12,tileScratch_.texture);d->SetPixelShaderConstantF(72,midInfo,1);
            const bool wide=fog&&tilesWide_&&fogTiled_&&cloudsTiled_&&
                NorthlightActorShadowMap::tiledFogRange(matrices[source],camera,inverseDepth);
            if(wide)d->SetPixelShader(tilesWide_);
            if(!check(NorthlightScreen::drawViewportQuad(d,Tiles,Tiles),"actor shadow tile pass"))return false;
            wideCurrent[source]=wide;
            if(wide)d->SetPixelShader(tilesShader_);
        }
        return true;
    }

    template<class Check,class SetSource>
    bool resolveVisibility(IDirect3DDevice9* d,UINT w,UINT h,const bool (&active)[2],bool soft,
                           const float (&matrices)[2][16],IDirect3DTexture9* restoreMetadata,
                           const Check& check,const SetSource& setSource) const {
        if(!check(d->SetTexture(12,nullptr),"actor visibility unbind")||
           !check(d->SetRenderTarget(0,visibility_.surface),"actor visibility target")||
           !check(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xffffffff,1,0),"clear actor visibility"))return false;
        for(int source=0;source<2;++source){
            if(!active[source])continue;
            setSource(source);
            d->SetPixelShaderConstantF(68,matrices[source],4);d->SetPixelShaderConstantF(72,Info,1);
            d->SetTexture(12,depth(source).texture);d->SetTexture(10,tiles(source).texture);
            d->SetRenderState(D3DRS_COLORWRITEENABLE,source?D3DCOLORWRITEENABLE_GREEN:D3DCOLORWRITEENABLE_RED);
            d->SetPixelShader(soft?filter_:uniform_);
            if(!check(NorthlightScreen::drawViewportQuad(d,w,h),"actor shadow visibility pass"))return false;
        }
        d->SetRenderState(D3DRS_COLORWRITEENABLE,15);
        d->SetTexture(10,restoreMetadata);
        return true;
    }

private:
    friend struct WorldScreenEffectsTest;
    static constexpr UINT Size=NorthlightActorShadowMap::Size;
    static constexpr UINT Mid=Size/NorthlightActorShadowMap::TileStep;
    static constexpr UINT Tiles=Size/NorthlightActorShadowMap::TileTexels;
    Target depth_[2],tiles_[2],tileScratch_,visibility_;
    IDirect3DPixelShader9 *filter_=nullptr,*uniform_=nullptr,*tilesShader_=nullptr;
    IDirect3DPixelShader9 *tilesWide_=nullptr,*fogTiled_=nullptr,*cloudsTiled_=nullptr;
    bool pending_=false;

    template<class T> static void drop(T*& pointer){if(pointer){pointer->Release();pointer=nullptr;}}
};
