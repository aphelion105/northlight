#pragma once
#include "screen_quad.h"
#include "world_render_target.h"
#include "world_compiled_shaders.h"
#include "name_stencil.h"
#include "unlit_receiver.h"
#include "legacy_fog.h"
#include "material_profile.h"
#include "gpu_profile.h"
#include "effect_coverage.h"
#include "world_lighting_capture.h"

// Stencil is scoped to the quad. The caller owns the rest of the render state.
struct NameMaskCompositeBind {
    IDirect3DDevice9* device;IDirect3DSurface9* previousDepth=nullptr;DWORD previous[3]={};bool bound=false;
    static constexpr D3DRENDERSTATETYPE kRestored[3]={D3DRS_STENCILENABLE,D3DRS_ZENABLE,D3DRS_ZWRITEENABLE};
    NameMaskCompositeBind(IDirect3DDevice9* d,IDirect3DSurface9* maskDepth,DWORD bits=NorthlightNameStencil::kBit,DWORD reference=0):device(d){
        if(!maskDepth)return;
        const HRESULT got=device->GetDepthStencilSurface(&previousDepth);
        if((SUCCEEDED(got)||got==D3DERR_NOTFOUND)&&SUCCEEDED(device->SetDepthStencilSurface(maskDepth))){
            bound=true;for(unsigned i=0;i<3;++i)device->GetRenderState(kRestored[i],&previous[i]);
            for(const auto& set:NorthlightNameStencil::kComposite)device->SetRenderState(D3DRENDERSTATETYPE(set.state),set.value);
            device->SetRenderState(D3DRS_STENCILFUNC,D3DCMP_EQUAL);device->SetRenderState(D3DRS_STENCILREF,reference);device->SetRenderState(D3DRS_STENCILMASK,bits);
        }
    }
    ~NameMaskCompositeBind(){
        if(bound){for(unsigned i=0;i<3;++i)device->SetRenderState(kRestored[i],previous[i]);device->SetDepthStencilSurface(previousDepth);}
        if(previousDepth)previousDepth->Release();
    }
    NameMaskCompositeBind(const NameMaskCompositeBind&)=delete;
    NameMaskCompositeBind& operator=(const NameMaskCompositeBind&)=delete;
};

class NorthlightWorldComposite {
public:
    struct Layers {bool unlit,prelit,authored;};
    struct MaterialRoute {bool full,combined,solar=false;};
    struct Input {
        IDirect3DSurface9* target;
        UINT width,height;
        D3DFORMAT format;
        bool hdr,hdrBaseEncoded,lighting,fog,localLighting,effectEmission,prelitDaylightCurrent;
        Layers layers;
        MaterialRoute material;
        const float* haze;
        const NorthlightLegacyFog::Constants& nativeFog;
        IDirect3DTexture9 *foldScene,*hdrBase,*dither,*neutralZero,*temporalLight,*fogColor,*localLight;
        IDirect3DTexture9 *effectCoverage,*materialOcclusion,*materialDetail,*normalBuffer,*prelitDaylight;
        IDirect3DTexture9 *restoreFarShadow,*restoreVisibility;
        NorthlightGpuProfile* profile;
        NorthlightMaterialProfile* materialProfile;
        NorthlightLightingCapture::Capture* lightingCapture;
        unsigned directInputs;
        IDirect3DTexture9* restoreRegionalFog=nullptr;
    };
    struct Result {bool ok,base,aoBase,rainAbsent,nativeSurfaceShared;};

    void permitBase(bool enabled){baseAllowed_=enabled;}
    void setNameMask(IDirect3DSurface9* depth){nameMaskDepth_=depth;}
    void setUnlitMask(IDirect3DSurface9* depth){unlitMaskDepth_=depth;}
    void setPrelitMask(IDirect3DSurface9* depth){prelitMaskDepth_=depth;}
    void setAuthoredMask(IDirect3DSurface9* depth,unsigned levels){authoredMaskDepth_=depth;authoredRoomLevels_=levels;}
    void clearMasks(){nameMaskDepth_=unlitMaskDepth_=prelitMaskDepth_=authoredMaskDepth_=nullptr;}
    IDirect3DTexture9* scratchTexture() const {return color_;}
    IDirect3DSurface9* scratchSurface() const {return colorSurface_;}
    IDirect3DPixelShader9* standardShader() const {return finalPS_;}
    template<class Pixel> bool createStandard(const Pixel& pixel){
        return pixel(kWorldCompositeShader,finalPS_,"world composite shader");
    }
    template<class Pixel> bool createMaterial(const Pixel& pixel){
        return pixel(kMaterialCompositeShader,materialPS_,"full-resolution material shader");
    }

    template<class Log,class Warn>
    bool prepareShaders(IDirect3DDevice9* d,bool hdr,bool lighting,bool combineAllowed,bool materialDetail,
                        const Log& log,const Warn& warn){
        if(hdr){
            if((!hdrFinalPS_&&FAILED(d->CreatePixelShader(kWorldCompositeHdrShader,&hdrFinalPS_)))||
               (!hdrUnlitPS_&&FAILED(d->CreatePixelShader(kWorldUnlitCompositeHdrShader,&hdrUnlitPS_)))||
               (!hdrMaterialPS_&&FAILED(d->CreatePixelShader(kMaterialCompositeHdrShader,&hdrMaterialPS_))))return false;
            if(baseAllowed_&&!lighting&&!aoBaseTried_){
                aoBaseTried_=true;
                if(FAILED(d->CreatePixelShader(kWorldAOCompositeHdrShader,&hdrAOBasePS_)))
                    warn("WORLD AO HDR compositor unavailable; ordinary composition retained");
            }
            if(baseAllowed_&&!baseTried_){
                baseTried_=true;
                if(FAILED(d->CreatePixelShader(kWorldBaseCompositeHdrShader,&hdrBasePS_)))
                    warn("WORLD base HDR compositor unavailable; ordinary composition retained");
            }
        }
        if(hdr&&combineAllowed&&materialDetail&&!combineTried_){
            combineTried_=true;D3DCAPS9 caps{};
            if(SUCCEEDED(d->GetDeviceCaps(&caps))&&caps.MaxPixelShader30InstructionSlots>=kWorldMaterialCompositeHdrShaderSlots)
                d->CreatePixelShader(kWorldMaterialCompositeHdrShader,&hdrCombinedPS_);
            log(unsigned(hdrCombinedPS_!=nullptr),(unsigned long)caps.MaxPixelShader30InstructionSlots,kWorldMaterialCompositeHdrShaderSlots);
        }
        if(!hdr&&unlitMaskDepth_&&!unlitPS_&&!unlitShaderRefused_&&
           FAILED(d->CreatePixelShader(kWorldUnlitCompositeShader,&unlitPS_))){
            unlitShaderRefused_=true;
            warn("UNLIT receiver shader unavailable; ordinary composition retained");
        }
        auto*& prelitShader=hdr?hdrPrelitPS_:prelitPS_;
        if((prelitMaskDepth_||authoredMaskDepth_)&&!prelitShader&&!prelitShaderRefused_&&
           FAILED(d->CreatePixelShader(hdr?kWorldPrelitCompositeHdrShader:kWorldPrelitCompositeShader,&prelitShader)))
            prelitShaderRefused_=true;
        return true;
    }

    Layers selectLayers(bool hdr,bool normalView) const {
        const auto* prelitShader=hdr?hdrPrelitPS_:prelitPS_;
        return {normalView&&unlitMaskDepth_&&(hdr?hdrUnlitPS_:unlitPS_),
                normalView&&prelitMaskDepth_&&prelitShader,normalView&&authoredMaskDepth_&&prelitShader};
    }
    template<class Warn>
    bool prepareSolar(IDirect3DDevice9* d,bool hdr,const Warn& warn){
        auto*& shader=solarMaterialPS_[hdr];
        auto& refused=solarMaterialRefused_[hdr];
        if(!shader&&!refused&&FAILED(d->CreatePixelShader(
            hdr?kMaterialSolarCompositeHdrShader:kMaterialSolarCompositeShader,&shader))){
            drop(shader);
            refused=true;
            warn("WORLD solar material shader unavailable; conventional material lighting retained");
        }
        return shader!=nullptr;
    }
    MaterialRoute selectMaterial(bool wanted,bool hdr,bool combineAllowed,bool diagnostic,bool solar=false) const {
        const bool full=wanted&&materialPS_;
        return {full,hdr&&combineAllowed&&full&&hdrCombinedPS_&&!diagnostic&&!solar,full&&solar};
    }

    template<class Check,class Invalidate>
    bool prepareScratch(IDirect3DDevice9* d,UINT w,UINT h,D3DFORMAT wanted,bool needed,bool retained,
                        const Check& check,const Invalidate& invalidate){
        if(needed){
            if(color_&&colorFormat_!=wanted){invalidate();releaseScratch();}
            if(!color_||!colorSurface_){
                if(!NorthlightWorldRenderTarget::ensure(d,w,h,wanted,color_,colorSurface_,check,
                        "world render texture","world render surface"))return false;
                colorFormat_=wanted;
            }
        }else if(color_&&!retained){invalidate();releaseScratch();}
        return true;
    }

    // Transition from shadow rendering to the shared screen quad state.
    // A caller-supplied scene texture already contains the pre-AO color.
    template<class Device,class Check>
    bool prepareScreenInput(Device* d,IDirect3DSurface9* target,bool foldedScene,const Check& check){
        d->SetDepthStencilSurface(nullptr);
        d->SetTexture(0,nullptr);
        if(!foldedScene&&!check(d->StretchRect(target,nullptr,colorSurface_,nullptr,D3DTEXF_NONE),"world color copy"))return false;
        d->SetVertexShader(nullptr);
        d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);
        d->SetStreamSourceFreq(0,1);
        d->SetRenderState(D3DRS_ZENABLE,FALSE);
        d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
        d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0);
        d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);
        d->SetRenderState(D3DRS_WRAP0,0);
        return true;
    }

    template<class Device>
    static bool clearDiagnostic(Device* d,IDirect3DSurface9* target,UINT width,UINT height){
        if(FAILED(d->SetDepthStencilSurface(nullptr)))return false;
        for(UINT slot=1;slot<4;++slot)if(FAILED(d->SetRenderTarget(slot,nullptr)))return false;
        const D3DVIEWPORT9 viewport{0,0,width,height,0,1};
        if(FAILED(d->SetRenderTarget(0,target))||FAILED(d->SetViewport(&viewport))||
           FAILED(d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE)))return false;
        return SUCCEEDED(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));
    }

    void reset(){
        releaseScratch();
        drop(finalPS_);drop(materialPS_);drop(unlitPS_);drop(prelitPS_);
        drop(hdrFinalPS_);drop(hdrUnlitPS_);drop(hdrMaterialPS_);drop(hdrCombinedPS_);
        drop(hdrBasePS_);drop(hdrAOBasePS_);drop(hdrPrelitPS_);
        for(auto*& shader:solarMaterialPS_)drop(shader);
        solarMaterialRefused_[0]=solarMaterialRefused_[1]=false;
        baseTried_=aoBaseTried_=combineTried_=unlitShaderRefused_=prelitShaderRefused_=false;
        colorFormat_=D3DFMT_UNKNOWN;
    }

    template<class Check,class AfterMaterial>
    Result draw(IDirect3DDevice9* d,const Input& in,IDirect3DTexture9*& rainMask,const Check& check,
                const AfterMaterial& afterMaterial){
        const auto quad=[&]{
            const D3DVIEWPORT9 vp={0,0,in.width,in.height,0,1};
            d->SetViewport(&vp);
            return NorthlightScreen::drawQuad(d,in.width,in.height);
        };
        const bool separateMaterial=in.material.full&&!in.material.combined;
        // Fog blur also writes c34. Upload the haze for both fog-on and fog-off frames.
        d->SetPixelShaderConstantF(34,in.haze,1);
        const bool baseComposite=baseAllowed_&&in.hdr&&!in.material.combined&&!in.fog&&!in.localLighting&&hdrBasePS_;
        const bool aoBaseComposite=baseComposite&&!in.lighting&&hdrAOBasePS_;
        const bool nativeSurfaceShared=baseComposite&&in.foldScene&&NorthlightLegacyFog::surfaceFogIsZero(
            in.nativeFog,in.format==D3DFMT_A8R8G8B8||in.format==D3DFMT_X8R8G8B8);
        const bool rainAbsent=!rainMask;
        const float compositeWork[]={in.fog?0.f:1.f,in.localLighting?0.f:1.f,rainAbsent?1.f:0.f,nativeSurfaceShared?1.f:0.f};
        d->SetPixelShaderConstantF(80,compositeWork,1);
        // Borrow the regional fog slot for this draw batch.
        d->SetTexture(13,rainMask?rainMask:in.neutralZero);
        rainMask=nullptr;
        d->SetTexture(2,in.hdr?in.hdrBase:in.dither?in.dither:in.neutralZero);
        d->SetSamplerState(2,D3DSAMP_ADDRESSU,in.hdr?D3DTADDRESS_CLAMP:D3DTADDRESS_WRAP);
        d->SetSamplerState(2,D3DSAMP_ADDRESSV,in.hdr?D3DTADDRESS_CLAMP:D3DTADDRESS_WRAP);
        d->SetSamplerState(2,D3DSAMP_SRGBTEXTURE,in.hdr&&in.hdrBaseEncoded?TRUE:FALSE);
        d->SetTexture(3,in.localLighting?in.localLight:in.neutralZero);
        d->SetRenderTarget(0,separateMaterial?colorSurface_:in.target);
        d->SetTexture(8,in.lighting?in.temporalLight:in.neutralZero);
        d->SetTexture(9,in.fogColor);
        d->SetPixelShader(aoBaseComposite?hdrAOBasePS_:baseComposite?hdrBasePS_:in.hdr?hdrFinalPS_:finalPS_);
        if(in.material.combined){bindMaterial(d,in);d->SetPixelShader(hdrCombinedPS_);}
        NorthlightEffectCoverage::Capture::bind(d,in.effectCoverage,in.effectEmission);
        if(in.lightingCapture)in.lightingCapture->inputs(3,(1u<<7)|(1u<<13),(1u<<7)|(1u<<13));

        const auto& layers=in.layers;
        auto* maskDepth=layers.authored?authoredMaskDepth_:layers.prelit?prelitMaskDepth_:layers.unlit?unlitMaskDepth_:nameMaskDepth_;
        const unsigned roomPasses=(layers.prelit?8u:0u)|(layers.authored?authoredRoomLevels_:0u);
        const DWORD maskBits=(nameMaskDepth_?NorthlightNameStencil::kBit:0)|(layers.unlit?NorthlightUnlitReceiver::Bit:0)|
                             NorthlightUnlitReceiver::roomStencilMask(roomPasses);
        clearMasks();
        bool ok;
        {NameMaskCompositeBind mask(d,separateMaterial?nullptr:maskDepth,maskBits);ok=check(quad(),"world composite");}
        if(ok&&layers.unlit){
            if(in.material.combined)bindFog(d,in.fogColor);
            d->SetRenderTarget(0,in.target);
            d->SetPixelShader(in.hdr?hdrUnlitPS_:unlitPS_);
            NameMaskCompositeBind mask(d,maskDepth,maskBits,NorthlightUnlitReceiver::Bit);
            ok=check(quad(),"unlit atmosphere composite");
        }
        if(ok&&roomPasses){
            if(in.material.combined)bindFog(d,in.fogColor);
            d->SetTexture(14,in.prelitDaylightCurrent?in.prelitDaylight:in.neutralZero);
            d->SetSamplerState(14,D3DSAMP_MINFILTER,D3DTEXF_POINT);
            d->SetSamplerState(14,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
            d->SetRenderTarget(0,in.target);
            d->SetPixelShader(in.hdr?hdrPrelitPS_:prelitPS_);
            for(unsigned level=0;level<4&&ok;++level)if(roomPasses&(1u<<level)){
                const float daylightInfo[]={in.prelitDaylightCurrent?1.f:0.f,(3-level)/3.f,0,0};
                d->SetPixelShaderConstantF(81,daylightInfo,1);
                NameMaskCompositeBind mask(d,maskDepth,maskBits,NorthlightUnlitReceiver::RoomRef[level]);
                ok=check(quad(),"prelit WMO composite");
            }
        }
        if(in.profile)in.profile->mark("WorldComposite");
        if(ok&&separateMaterial){
            NorthlightMaterialProfile::Scope timing(in.materialProfile,NorthlightMaterialProfile::FinalApply,0,true);
            timing.work(uint64_t(in.width)*in.height);
            d->SetRenderTarget(0,in.target);
            d->SetTexture(0,color_);
            bindMaterial(d,in);
            d->SetPixelShader(in.material.solar?solarMaterialPS_[in.hdr]:in.hdr?hdrMaterialPS_:materialPS_);
            constexpr unsigned materialInputs=(1u<<0)|(1u<<1)|(1u<<4)|(1u<<5)|(1u<<6)|(1u<<7)|(1u<<8)|(1u<<9)|(1u<<10)|(1u<<11)|(1u<<12)|(1u<<13)|(1u<<14);
            if(in.lightingCapture)in.lightingCapture->inputs(3,materialInputs,in.directInputs|(1u<<7)|(1u<<13));
            NameMaskCompositeBind mask(d,maskDepth,maskBits);
            const HRESULT materialResult=quad();
            ok=check(materialResult,"full-resolution material light");
            if(in.lightingCapture)in.lightingCapture->pass(
                in.material.solar?(in.hdr?"MaterialSolarCompositeHdr":"MaterialSolarComposite"):"MaterialComposite",
                3,materialInputs,0,0,true,materialResult);
        }
        if(in.profile)in.profile->mark("MaterialDetail");
        if(ok&&in.material.full){
            if(in.material.combined&&(layers.unlit||layers.prelit||layers.authored)){
                d->SetTexture(9,in.materialDetail);
                d->SetSamplerState(9,D3DSAMP_MINFILTER,D3DTEXF_POINT);
                d->SetSamplerState(9,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
            }
            afterMaterial();
        }
        d->SetTexture(3,in.restoreFarShadow);
        d->SetTexture(7,in.restoreVisibility);
        d->SetTexture(15,nullptr);
        d->SetTexture(2,nullptr);
        d->SetSamplerState(2,D3DSAMP_SRGBTEXTURE,FALSE);
        d->SetSamplerState(2,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
        d->SetSamplerState(2,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
        d->SetTexture(13,in.restoreRegionalFog);
        return {ok,baseComposite,aoBaseComposite,rainAbsent,nativeSurfaceShared};
    }

private:
    friend struct WorldScreenEffectsTest;
    IDirect3DTexture9* color_=nullptr;
    IDirect3DSurface9* colorSurface_=nullptr;
    D3DFORMAT colorFormat_=D3DFMT_UNKNOWN;
    IDirect3DPixelShader9 *finalPS_=nullptr,*materialPS_=nullptr,*unlitPS_=nullptr,*prelitPS_=nullptr;
    IDirect3DPixelShader9 *hdrFinalPS_=nullptr,*hdrUnlitPS_=nullptr,*hdrMaterialPS_=nullptr,*hdrCombinedPS_=nullptr;
    IDirect3DPixelShader9 *hdrBasePS_=nullptr,*hdrAOBasePS_=nullptr,*hdrPrelitPS_=nullptr;
    IDirect3DPixelShader9* solarMaterialPS_[2]={};
    bool solarMaterialRefused_[2]={};
    bool baseAllowed_=false,baseTried_=false,aoBaseTried_=false,combineTried_=false;
    bool unlitShaderRefused_=false,prelitShaderRefused_=false;
    IDirect3DSurface9 *nameMaskDepth_=nullptr,*unlitMaskDepth_=nullptr,*prelitMaskDepth_=nullptr,*authoredMaskDepth_=nullptr;
    unsigned authoredRoomLevels_=1;

    template<class T> static void drop(T*& pointer){if(pointer){pointer->Release();pointer=nullptr;}}
    void releaseScratch(){drop(colorSurface_);drop(color_);}
    static void bindFog(IDirect3DDevice9* d,IDirect3DTexture9* fog){
        d->SetTexture(9,fog);
        d->SetSamplerState(9,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);
        d->SetSamplerState(9,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
    }
    static void bindMaterial(IDirect3DDevice9* d,const Input& in){
        d->SetTexture(4,in.fogColor);
        d->SetTexture(5,in.foldScene);
        d->SetTexture(6,in.materialOcclusion);
        d->SetTexture(9,in.materialDetail);
        d->SetTexture(14,in.normalBuffer);
        for(unsigned sampler:{4u,5u,6u,9u,12u,14u}){
            const DWORD filter=(sampler==6||sampler==9||sampler==14)?D3DTEXF_POINT:D3DTEXF_LINEAR;
            d->SetSamplerState(sampler,D3DSAMP_MINFILTER,filter);
            d->SetSamplerState(sampler,D3DSAMP_MAGFILTER,filter);
            d->SetSamplerState(sampler,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
            d->SetSamplerState(sampler,D3DSAMP_SRGBTEXTURE,FALSE);
            d->SetSamplerState(sampler,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
            d->SetSamplerState(sampler,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
        }
    }
};
