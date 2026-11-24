#pragma once
#include "screen_quad.h"
#include "effects_buckets.h"
#include "gpu_profile.h"
#include "local_light_batches.h"
#include "local_light_trace.h"
#include "world_compiled_shaders.h"
#include "world_lighting_capture.h"
#include "world_local_scissor.h"

// Owns the screen-space lamp programs and the profiling split target.
// The frame borrows the direct-light and fog scratch images after their
// previous readers have finished. Point cubes and flash shadows stay separate.
class NorthlightWorldLocalLighting {
    friend struct PipelineOffTest;
    friend struct WorldScreenEffectsTest;
    IDirect3DPixelShader9 *localDirectPS=nullptr,*localDirect16PS=nullptr,*localFlashPS=nullptr;
    IDirect3DPixelShader9 *localFogPS=nullptr,*localFogCombinePS=nullptr,*localDirectSinglePS=nullptr;
    bool localDirectSingleRefused=false;
    IDirect3DPixelShader9* localGlint_[3]={};
    bool localGlintRefused_[3]={};
    IDirect3DSurface9* profileSplitSurface=nullptr;
    bool profileSplitFailed=false;
    NorthlightLocalLightScissor::DrawState scissor_;
    unsigned widePasses_=0;

    static void recordLightBatch(IDirect3DDevice9* d,NorthlightLocalLightTrace::Frame& sourceTrace,bool volume,unsigned first,unsigned count,const NorthlightLocalLightScissor::Rect& rect,bool drawn,HRESULT result){
        if(!sourceTrace.wants(first,count))return;
        NorthlightLocalLightTrace::Row::DrawState state;
        // State getters are CPU-only and run only for a sampled, tracked batch.
        // Refused getters remain unknown; successful submission is not a pixel proof.
        if(drawn){D3DVIEWPORT9 viewport{};DWORD enabled=0;RECT scissor{};
            if(SUCCEEDED(d->GetViewport(&viewport))){state.known|=1;state.viewport[0]=viewport.X;state.viewport[1]=viewport.Y;state.viewport[2]=viewport.Width;state.viewport[3]=viewport.Height;state.minZ=viewport.MinZ;state.maxZ=viewport.MaxZ;}
            if(SUCCEEDED(d->GetRenderState(D3DRS_SCISSORTESTENABLE,&enabled))){state.known|=2;state.scissorEnabled=enabled;}
            if(SUCCEEDED(d->GetScissorRect(&scissor))){state.known|=4;state.scissor={scissor.left,scissor.top,scissor.right,scissor.bottom};}}
        sourceTrace.batch(volume,first,count,rect,drawn,result,state);
    }

    template<class T> static void drop(T*& pointer){if(pointer){pointer->Release();pointer=nullptr;}}

    // End the render pass on timestamp sample frames so its elapsed GPU work
    // is attributed to the lamp passes even on tiled backends.
    void profileSplit(IDirect3DDevice9* d,NorthlightGpuProfile* profile,IDirect3DSurface9* resume){
        if(!profile||!profile->recording()||!resume||profileSplitFailed)return;
        if(!profileSplitSurface&&FAILED(d->CreateRenderTarget(1,1,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&profileSplitSurface,nullptr))){profileSplitSurface=nullptr;profileSplitFailed=true;return;}
        if(SUCCEEDED(d->SetRenderTarget(0,profileSplitSurface)))d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0);
        d->SetRenderTarget(0,resume);
    }

    IDirect3DPixelShader9* localDirectShader(IDirect3DDevice9* d,unsigned count){
        if(count!=1)return localDirectPS;
        if(!localDirectSinglePS&&!localDirectSingleRefused){
            if(FAILED(d->CreatePixelShader(kLocalDirect1Shader,&localDirectSinglePS))||!localDirectSinglePS){drop(localDirectSinglePS);localDirectSingleRefused=true;}
        }
        return localDirectSinglePS?localDirectSinglePS:localDirectPS;
    }

    IDirect3DPixelShader9* localGlintShader(IDirect3DDevice9* d,unsigned count){
        const unsigned slot=count==1?0:count>NorthlightLocalLightSelection::DirectBatchSize?2:1;
        if(!localGlint_[slot]&&!localGlintRefused_[slot]){
            const DWORD* code[]={kLocalDirect1GlintShader,kLocalDirectGlintShader,kLocalDirect16GlintShader};
            if(FAILED(d->CreatePixelShader(code[slot],&localGlint_[slot]))||!localGlint_[slot]){
                drop(localGlint_[slot]);localGlintRefused_[slot]=true;
            }
        }
        return localGlint_[slot];
    }

public:
    static constexpr unsigned LocalDirectInputs=(1u<<1)|(1u<<9)|(1u<<11)|(1u<<12)|(1u<<14);
    struct SurfaceInput {
        IDirect3DSurface9* target;
        IDirect3DTexture9 *material,*normals;
        IDirect3DTexture9* const* frameTextures;
        const float (&constants)[68][4];
        bool accumulate,bindMaterial,positional,fog;
    };

    NorthlightWorldLocalLighting()=default;
    NorthlightWorldLocalLighting(const NorthlightWorldLocalLighting&)=delete;
    NorthlightWorldLocalLighting& operator=(const NorthlightWorldLocalLighting&)=delete;
    ~NorthlightWorldLocalLighting(){reset();}

    NorthlightLocalLightScissor::DrawState& scissor(){return scissor_;}
    unsigned widePasses()const{return widePasses_;}
    void resetWork(){scissor_.resetWork();widePasses_=0;}

    IDirect3DPixelShader9* directShader() const{return localDirectPS;}
    IDirect3DSurface9* profileTarget() const{return profileSplitSurface;}

    template<class Pixel> bool createDirect(const Pixel& pixel){
        return pixel(kLocalDirectShader,localDirectPS,"local direct light shader")&&
               pixel(kLocalDirect16Shader,localDirect16PS,"local direct 16-light shader")&&
               pixel(kLocalFlashShader,localFlashPS,"lightning light shader");
    }

    template<class Pixel> bool createFog(const Pixel& pixel){
        return pixel(kLocalFogShader,localFogPS,"local fog glow shader")&&
               pixel(kLocalFogCombineShader,localFogCombinePS,"local fog combine shader");
    }

    void reset(){
        drop(localDirectPS);
        drop(localDirect16PS);
        drop(localFlashPS);
        drop(localDirectSinglePS);
        localDirectSingleRefused=false;
        for(unsigned i=0;i<3;++i){drop(localGlint_[i]);localGlintRefused_[i]=false;}
        drop(profileSplitSurface);
        profileSplitFailed=false;
        drop(localFogPS);
        drop(localFogCombinePS);
    }

    template<class Device,class Check,class Direct,class Flash,class Points>
    bool drawSurface(Device* d,const SurfaceInput& in,const NorthlightLocalLightSelection::Selection& lights,
                     NorthlightGpuProfile* profile,NorthlightEffectsBuckets::Frame* buckets,
                     NorthlightLightingCapture::Capture* lightingCapture,NorthlightLightingCapture::Capture* pointInputs,
                     const Check& check,const Direct& direct,const Flash& flash,const Points& points){
        const auto& c=in.constants;
        d->SetTexture(8,nullptr);
        if(in.accumulate){
            d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
            if(!check(d->SetRenderTarget(0,in.target),"current local light target")||
               !check(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0),"current local light clear"))return false;
            d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE);
            d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);
            d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_COLORWRITEENABLE,7);
        }
        // Directional, actor and filter passes share a subset of the frame bank.
        d->SetPixelShaderConstantF(7,&c[7][0],21);
        // Fog and composition inherit the s9 POINT contract even with zero lamps.
        for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})d->SetSamplerState(9,state,D3DTEXF_POINT);
        if(in.bindMaterial){
            d->SetTexture(9,in.material);d->SetTexture(14,in.normals);
            for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})d->SetSamplerState(14,state,D3DTEXF_POINT);
        }
        if(lightingCapture){
            lightingCapture->sources(lights,in.fog);
            lightingCapture->image("lighting-before-local",in.target);
        }
        if(!direct())return false;
        if(lightingCapture)lightingCapture->image("lighting-after-local",in.target);
        if(profile)profile->mark("LocalLights");
        if(buckets)buckets->mark(NorthlightEffectsBuckets::GI);
        d->SetRenderState(D3DRS_COLORWRITEENABLE,15);
        if(in.positional)flash();
        if(pointInputs){
            pointInputs->pointInputStage(in.accumulate);
            if(in.accumulate)pointInputs->image("local-before-point",in.target);
        }
        if(in.positional){
            points();
            // Point and flash reuse the lower bank. c53..67 remains current.
            d->SetPixelShaderConstantF(0,&c[0][0],53);
            for(unsigned sampler=0;sampler<6;++sampler){
                d->SetTexture(sampler,in.frameTextures[sampler]);
                d->SetSamplerState(sampler,D3DSAMP_MINFILTER,sampler==0?D3DTEXF_LINEAR:D3DTEXF_POINT);
                d->SetSamplerState(sampler,D3DSAMP_MAGFILTER,sampler==0?D3DTEXF_LINEAR:D3DTEXF_POINT);
            }
            d->SetRenderState(D3DRS_COLORWRITEENABLE,15);
        }
        if(pointInputs&&in.accumulate)pointInputs->image("local-after-point",in.target);
        if(profile)profile->mark("PointLight");
        d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
        return true;
    }

    template<class Check>
    bool drawDirect(IDirect3DDevice9* d,IDirect3DSurface9* target,UINT w,UINT h,
                    const NorthlightLocalLightSelection::Selection& localLights,
                    const NorthlightLocalLightScissor::Rect* rects,const NorthlightLocalLightScissor::View& view,
                    float lampStrength,float lampSunlitCut,const float (&shadowTuning)[4],
                    NorthlightGpuProfile* profile,NorthlightLightingCapture::Capture* lightingCapture,
                    NorthlightLocalLightTrace::Frame& trace,const Check& check,int reflection=-1,float reflectionGain=0){
        const auto batchReflection=[&](unsigned first,unsigned count){
            if(reflection<int(first)||reflection>=int(first+count))return static_cast<IDirect3DPixelShader9*>(nullptr);
            auto* shader=localGlintShader(d,count);
            if(!shader)return shader;
            const std::array<std::array<float,4>,2> lamp={localLights.position[reflection],localLights.color[reflection]};
            return SUCCEEDED(d->SetPixelShaderConstantF(157,lamp[0].data(),2))?shader:nullptr;
        };
        // Negative gain requests the wide stone sheen for the tagged well source.
        if(reflection>=0&&(localLights.sources[reflection].flags&NorthlightLocalLights::Moonwell))
            reflectionGain=-std::min(1.f,reflectionGain*2.f);
        profileSplit(d,profile,target); /* terminate the preceding render pass before timing local batches */
        d->SetPixelShader(localDirectPS);d->SetRenderState(D3DRS_SCISSORTESTENABLE,NorthlightLocalLightScissor::Enabled);
        // s12 still holds current sun visibility from WorldLighting.
        if(lightingCapture)lightingCapture->inputs(0,LocalDirectInputs,LocalDirectInputs);
        // Two neighbouring 8-lamp batches whose joint rect costs less than both (local_light_batches.h wide()) draw as one LocalDirect16
        // pass (its lamps in c84..c115, preserving material c73..c78; c52 as for LocalDirect). Without a proven view every rect is the whole target, so pairs merge.
        constexpr unsigned N=NorthlightLocalLightSelection::DirectBatchSize;IDirect3DPixelShader9* bound=localDirectPS;
        for(unsigned firstLight=0;firstLight<localLights.count;){
            const unsigned index=firstLight/N;
            const auto flashes=[&](unsigned begin,unsigned end){for(unsigned i=begin;i<std::min(end,localLights.count);++i)if(localLights.color[i][3]<0)return true;return false;};
            const bool flashBatch=flashes(firstLight,firstLight+N);
            const bool pair=localDirect16PS&&firstLight+N<localLights.count&&!flashBatch&&!flashes(firstLight+N,firstLight+2*N);
            const NorthlightLocalLightScissor::Rect whole{0,0,long(w/2),long(h/2)};
            const auto& a=view.valid?rects[index]:whole;const auto& b=view.valid&&pair?rects[index+1]:whole;
            if(pair&&NorthlightLocalLightBatches::wide(a,b)){
                const auto batch=localLights.directBatch<2*N>(firstLight);const auto joint=NorthlightLocalLightBatches::unite(a,b);
                auto* glint=batchReflection(firstLight,batch.count);
                auto* shader=glint?glint:localDirect16PS;
                const float info[4]={float(batch.count),lampStrength,lampSunlitCut,glint?reflectionGain:0.f};
                if(bound!=shader){d->SetPixelShader(shader);bound=shader;}
                d->SetPixelShaderConstantF(84,batch.position[0].data(),2*N);d->SetPixelShaderConstantF(100,batch.color[0].data(),2*N);
                d->SetPixelShaderConstantF(52,info,1);++widePasses_;
                const bool drawn=scissor_.apply(d,view,joint);const HRESULT hr=drawn?NorthlightScreen::drawViewportQuad(d,w/2,h/2):S_OK;
                recordLightBatch(d,trace,false,firstLight,batch.count,joint,drawn,hr);
                if(lightingCapture)lightingCapture->pass("LocalDirect16",0,LocalDirectInputs,firstLight,batch.count,drawn,hr);
                if(drawn&&!check(hr,"local direct 16-light batch")){d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);return false;}
                firstLight+=2*N;continue;
            }
            const auto batch=localLights.directBatch<N>(firstLight);
            auto* glint=flashBatch?nullptr:batchReflection(firstLight,batch.count);
            const float info[4]={float(batch.count),lampStrength,lampSunlitCut,glint?reflectionGain:0.f};
            const auto shader=flashBatch?localFlashPS:glint?glint:localDirectShader(d,batch.count);
            if(bound!=shader){d->SetPixelShader(shader);bound=shader;}
            d->SetPixelShaderConstantF(36,batch.position[0].data(),N);
            d->SetPixelShaderConstantF(44,batch.color[0].data(),N);
            d->SetPixelShaderConstantF(52,info,1);
            const bool drawn=scissor_.apply(d,view,rects[index]);const HRESULT hr=drawn?NorthlightScreen::drawViewportQuad(d,w/2,h/2):S_OK;
            recordLightBatch(d,trace,false,firstLight,batch.count,a,drawn,hr);
            if(lightingCapture)lightingCapture->pass("LocalDirect",0,LocalDirectInputs,firstLight,batch.count,drawn,hr);
            if(drawn&&!check(hr,"local direct light batch")){d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);return false;}
            firstLight+=N;
        }
        d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
        // LocalDirect16 borrows c100..115 for lamp colors. FlashShadow and
        // the final compositor need ShadowTuning at c115 again afterwards.
        d->SetPixelShaderConstantF(115,shadowTuning,1);
        profileSplit(d,profile,target);
        return true;
    }

    // Current lamp extinction is accumulated after atmospheric history and blur.
    template<class Check>
    bool drawFog(IDirect3DDevice9* d,UINT w,UINT h,IDirect3DSurface9* currentFogSurface,
                 IDirect3DTexture9* localFogAccum,IDirect3DSurface9* localFogAccumSurface,
                 const NorthlightLocalLightSelection::Selection& localLights,
                 const NorthlightLocalLightScissor::Rect* rects,const NorthlightLocalLightScissor::View& view,const float (&worldFogInfo)[4],
                 NorthlightLocalLightTrace::Frame& trace,NorthlightLightingCapture::Capture* lightingCapture,const Check& check){
        constexpr unsigned fogInputs=(1u<<1)|(1u<<11);
        if(lightingCapture){lightingCapture->image("fog-before-local",currentFogSurface);lightingCapture->inputs(1,fogInputs,fogInputs);}
        // 0.3.205 (gh#20): every batch adds its raw glow into localFogAccum and LocalFogCombine caps the total once into the fog buffer (a cap per batch of four
        // made the glow depend on how the closest-first lights group). A batch adds exactly +0 outside its rect, so only the union of the drawn batches' rects is
        // cleared and combined, and with no batch on screen neither runs. The scissor state fails open (no proven view): then every batch and the combine are unclipped.
        const unsigned fogBatches=(localLights.count+NorthlightLocalLightSelection::FogBatchSize-1)/NorthlightLocalLightSelection::FogBatchSize;
        const bool whole=!NorthlightLocalLightScissor::Enabled||!view.valid;
        NorthlightLocalLightScissor::Rect area{};
        if(!whole)for(unsigned batch=0;batch<fogBatches;++batch)
            area=NorthlightLocalLightBatches::unite(area,rects[batch]);
        if(!whole&&!area.area()){
            scissor_.skipBatches(fogBatches);
            recordLightBatch(d,trace,true,0,localLights.count,area,false,0);
            trace.combine=NorthlightLocalLightTrace::Stage::Clipped;
            if(lightingCapture)lightingCapture->skipped("LocalFog",0,localLights.count,"all-batches-clipped");
            return true;
        }
        const RECT clip={area.left,area.top,area.right,area.bottom};const D3DRECT cleared={area.left,area.top,area.right,area.bottom};
        d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE);d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD); // alpha adds like rgb (ONE,ONE): the sum of the lamps' capped peaks
        if(!check(d->SetRenderTarget(0,localFogAccumSurface),"lamp fog accumulator target")||!check(d->Clear(whole?0:1,whole?nullptr:&cleared,D3DCLEAR_TARGET,0,1,0),"lamp fog accumulator clear"))return false; // scissor is off here
        if(lightingCapture)lightingCapture->image("fog-local-clear",localFogAccumSurface);
        d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);d->SetPixelShader(localFogPS);
        // Analytic lamp glow can reach farther without extending the
        // celestial raymarch or adding samples to it. Restore after use.
        const float lampFogInfo[4]={worldFogInfo[0],worldFogInfo[1],worldFogInfo[2],localLights.fogDistance};
        d->SetPixelShaderConstantF(21,lampFogInfo,1);d->SetRenderState(D3DRS_SCISSORTESTENABLE,NorthlightLocalLightScissor::Enabled);
        for(unsigned firstLight=0;firstLight<localLights.count;firstLight+=NorthlightLocalLightSelection::FogBatchSize){
            const auto batch=localLights.batch<NorthlightLocalLightSelection::FogBatchSize>(firstLight);
            d->SetPixelShaderConstantF(36,batch.position[0].data(),NorthlightLocalLightSelection::FogBatchSize);
            d->SetPixelShaderConstantF(44,batch.color[0].data(),NorthlightLocalLightSelection::FogBatchSize);
            d->SetPixelShaderConstantF(59,batch.fog[0].data(),NorthlightLocalLightSelection::FogBatchSize);
            const auto& rect=rects[firstLight/NorthlightLocalLightSelection::FogBatchSize];
            const bool drawn=scissor_.apply(d,view,rect);const HRESULT hr=drawn?NorthlightScreen::drawViewportQuad(d,w/2,h/2):S_OK;
            recordLightBatch(d,trace,true,firstLight,batch.count,whole?NorthlightLocalLightScissor::Rect{0,0,long(w/2),long(h/2)}:rect,drawn,hr);
            if(lightingCapture)lightingCapture->pass("LocalFog",1,fogInputs,firstLight,batch.count,drawn,hr);
            if(drawn&&!check(hr,"local fog glow batch")){d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);return false;}
        }
        // The fog buffer is the target again (SetRenderTarget resets the scissor rect, so the union is set after it); s9 (idle here, rebound before every
        // reader) holds the accumulator for the capping quad.
        d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);d->SetRenderState(D3DRS_COLORWRITEENABLE,7);d->SetPixelShader(localFogCombinePS);
        d->SetTexture(9,localFogAccum);d->SetSamplerState(9,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(9,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
        const bool restored=check(d->SetRenderTarget(0,currentFogSurface),"lamp fog target restore");
        if(restored&&!whole){d->SetScissorRect(&clip);d->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE);}
        if(lightingCapture)lightingCapture->inputs(2,1u<<9); // The actual complete accumulator, before LocalFogCombine reads it.
        const HRESULT combineResult=restored?NorthlightScreen::drawViewportQuad(d,w/2,h/2):E_FAIL;
        const bool combined=restored&&check(combineResult,"local fog glow combine");
        if(lightingCapture){lightingCapture->pass("LocalFogCombine",2,1u<<9,0,localLights.count,restored,combineResult);lightingCapture->image("fog-after-local",currentFogSurface);}
        trace.combine=combined?NorthlightLocalLightTrace::Stage::Submitted:NorthlightLocalLightTrace::Stage::Failed;
        d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
        d->SetTexture(9,nullptr);d->SetSamplerState(9,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(9,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
        d->SetPixelShaderConstantF(21,worldFogInfo,1);
        return combined;
    }
};
