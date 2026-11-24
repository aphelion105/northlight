#pragma once
#include "gpu_profile.h"
#include "screen_quad.h"
#include "world_compiled_shaders.h"
#include "world_gi.h"
#include <cstring>
#include <string>

class NorthlightWorldLightHistory {
public:
    struct Target {
        IDirect3DTexture9* texture=nullptr;
        IDirect3DSurface9* surface=nullptr;
    };
    struct Slot {Target light,distance;};
    struct View {
        const std::string& map;
        NorthlightGI::Vec3 camera;
        const float* matrix;
    };
    struct LightingInput {
        IDirect3DTexture9 *rawLight,*ao,*smoothLight,*regionalFog;
        IDirect3DSurface9* smoothSurface;
        bool distanceFromNormals,diagnostic;
    };

    // Producers write pending before resolve. After publishing, produced is
    // current and pending still holds the previous frame until its last read.
    const Slot& pending() const {return slots_[next_];}
    const Slot& produced() const {return slots_[1-next_];}
    bool valid() const {return valid_;}
    void invalidate(){valid_=false;}
    bool sameMap(const View& view) const {return map_==view.map;}
    bool continuousCamera(const View& view) const {
        const auto delta=view.camera-camera_;
        return !(NorthlightGI::dot(delta,delta)>40.f*40.f);
    }
    bool accepts(const View& view,bool lighting,int debug) const {
        return valid_&&lighting_==lighting&&sameMap(view)&&continuousCamera(view)&&debug==0;
    }
    const float* previousView() const {return view_;}

    template<class Pixel> bool createLightingShaders(const Pixel& pixel,bool directionalShadows){
        return (!directionalShadows||pixel(kRemovalSmoothShader,removalShader_,"removal smoothing shader"))&&
               pixel(kTemporalLightShader,lightingShader_,"temporal light shader");
    }
    template<class Image> bool createLightTargets(const Image& image){
        for(auto& slot:slots_)if(!image(D3DFMT_A16B16G16R16F,slot.light.texture,slot.light.surface))return false;
        return true;
    }
    template<class Pixel,class Image> bool createDistance(const Pixel& pixel,const Image& image){
        if(!pixel(kWorldDistanceShader,distanceShader_,"world distance shader"))return false;
        for(auto& slot:slots_)if(!image(D3DFMT_R32F,slot.distance.texture,slot.distance.surface))return false;
        return true;
    }
    void permitZeroWeight(bool enabled){
        if(zeroWeightAllowed_==enabled)return;
        zeroWeightAllowed_=enabled;
        resetMerged();
    }
    void reset(){
        drop(distanceShader_);
        drop(removalShader_);
        drop(lightingShader_);
        resetMerged();
        for(auto& slot:slots_){
            drop(slot.light.texture);drop(slot.light.surface);
            drop(slot.distance.texture);drop(slot.distance.surface);
        }
        invalidate();
    }

    template<class Check,class Report>
    bool resolveLighting(IDirect3DDevice9* d,const View& view,const LightingInput& in,UINT width,UINT height,
                         NorthlightGpuProfile* profile,const Check& check,const Report& report){
        d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);
        // s10 switches from GI metadata to AO/bloom for smoothing and composition.
        d->SetTexture(10,in.ao);d->SetSamplerState(10,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(10,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
        // Fuse smoothing into temporal accumulation when current distance
        // exists. Captures retain the intermediate image and old pass.
        d->SetTexture(8,in.rawLight); // WorldLighting already bound baselineLight at s12.
        const bool removalDrawn=in.distanceFromNormals;
        const bool mergedRemoval=removalDrawn&&!in.diagnostic&&ensureMerged(d);
        if(removalDrawn){
            d->SetTexture(15,pending().distance.texture);
            bindHistorySampler(d,15,D3DTEXF_POINT);
        }
        if(removalDrawn&&!mergedRemoval){
            if(!check(d->SetRenderTarget(0,in.smoothSurface),"removal smoothing target"))return false;
            d->SetPixelShader(removalShader_);
            if(!check(NorthlightScreen::drawQuad(d,width,height),"removal smoothing pass"))return false;
        }
        if(profile)profile->mark("RemovalSmooth");
        if(removalDrawn&&!mergedRemoval)d->SetTexture(15,nullptr);
        // WorldNormals already produced distance when smoothing needs it.
        if(!check(d->SetRenderTarget(0,pending().light.surface),"temporal light target")||
           !check(d->SetRenderTarget(1,in.distanceFromNormals?nullptr:pending().distance.surface),"temporal depth target"))return false;
        d->SetRenderState(D3DRS_COLORWRITEENABLE1,15);
        // Light history uses unrounded bilinear reprojection. Depth stays POINT.
        // s9 carries smoothed lighting while s8 still supplies the neighbourhood clamp.
        if(mergedRemoval){
            // s14/s15 retain current normals/distance; move histories into
            // idle slots. Regional fog resumes ownership of s13 below.
            d->SetTexture(9,produced().light.texture);
            d->SetTexture(13,produced().distance.texture);
            bindHistorySampler(d,9,D3DTEXF_LINEAR);
            bindHistorySampler(d,13,D3DTEXF_POINT);
        }else{
            d->SetTexture(9,removalDrawn?in.smoothLight:in.rawLight); // WorldLighting set POINT filters.
            d->SetTexture(14,produced().light.texture);
            d->SetTexture(15,produced().distance.texture);
            bindHistorySampler(d,14,D3DTEXF_LINEAR);
            if(!removalDrawn)bindHistorySampler(d,15,D3DTEXF_POINT);
        }
        d->SetPixelShader(mergedRemoval?mergedShader_:lightingShader_);
        if(!check(NorthlightScreen::drawQuad(d,width,height),"temporal light pass"))return false;
        if(mergedRemoval)d->SetTexture(13,in.regionalFog);
        report(mergedRemoval,removalDrawn,mergedRemoval&&zeroWeightActive_);
        d->SetRenderTarget(1,nullptr);d->SetTexture(14,nullptr);d->SetTexture(15,nullptr);d->SetTexture(9,nullptr);d->SetSamplerState(9,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(9,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
        publish(view,true);
        return true;
    }

    template<class Check>
    bool resolveDistance(IDirect3DDevice9* d,const View& view,bool needed,UINT width,UINT height,const Check& check){
        invalidate();
        d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);
        d->SetRenderTarget(1,nullptr);d->SetTexture(14,nullptr);d->SetTexture(15,nullptr);
        if(needed){
            if(!check(d->SetRenderTarget(0,pending().distance.surface),"fog distance target"))return false;
            d->SetPixelShader(distanceShader_);
            if(!check(NorthlightScreen::drawQuad(d,width,height),"fog distance pass"))return false;
            publish(view,false);
        }
        return true;
    }

private:
    friend struct WorldScreenEffectsTest;
    Slot slots_[2]={};
    unsigned next_=0;
    bool valid_=false,lighting_=false;
    float view_[16]={};
    NorthlightGI::Vec3 camera_;
    std::string map_;
    IDirect3DPixelShader9 *distanceShader_=nullptr,*removalShader_=nullptr,*lightingShader_=nullptr,*mergedShader_=nullptr;
    bool mergeTried_=false,zeroWeightAllowed_=false,zeroWeightActive_=false;

    template<class T> static void drop(T*& pointer){if(pointer){pointer->Release();pointer=nullptr;}}
    static void bindHistorySampler(IDirect3DDevice9* d,unsigned slot,D3DTEXTUREFILTERTYPE filter){
        d->SetSamplerState(slot,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
        d->SetSamplerState(slot,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
        d->SetSamplerState(slot,D3DSAMP_MINFILTER,filter);
        d->SetSamplerState(slot,D3DSAMP_MAGFILTER,filter);
        d->SetSamplerState(slot,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
        d->SetSamplerState(slot,D3DSAMP_SRGBTEXTURE,FALSE);
    }
    void resetMerged(){drop(mergedShader_);mergeTried_=zeroWeightActive_=false;}
    void publish(const View& view,bool lighting){
        next_=1-next_;
        valid_=true;
        lighting_=lighting;
        std::memcpy(view_,view.matrix,sizeof view_);
        camera_=view.camera;
        map_=view.map;
    }
    bool ensureMerged(IDirect3DDevice9* d){
        if(!mergeTried_){
            mergeTried_=true;
            D3DCAPS9 caps{};
            if(FAILED(d->GetDeviceCaps(&caps)))return false;
            // DXVK 3.1.1 benefits from skipping zero-weight reads. Other backends
            // retain the measured predicated shader; only one variant is executed.
            if(zeroWeightAllowed_&&caps.MaxPixelShader30InstructionSlots>=kTemporalLightMergedZeroWeightShaderSlots){
                if(SUCCEEDED(d->CreatePixelShader(kTemporalLightMergedZeroWeightShader,&mergedShader_)))zeroWeightActive_=true;
                else drop(mergedShader_);
            }
            if(!mergedShader_&&caps.MaxPixelShader30InstructionSlots>=kTemporalLightMergedShaderSlots&&
               FAILED(d->CreatePixelShader(kTemporalLightMergedShader,&mergedShader_)))drop(mergedShader_);
        }
        return mergedShader_!=nullptr;
    }
};
