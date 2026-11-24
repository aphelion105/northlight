#pragma once
#include "directional_shadow_batches.h"
#include "world_directional_targets.h"
#include <climits>

namespace NorthlightDirectionalShadow {

template<class Device> void prepareDevice(Device* d){
    d->SetDepthStencilSurface(nullptr);
    for(unsigned i=1;i<4;++i)d->SetRenderTarget(i,nullptr);
    for(unsigned i=0;i<14;++i)d->SetTexture(i,nullptr);
    const struct{D3DRENDERSTATETYPE state;DWORD value;} states[]={
        {D3DRS_ZENABLE,TRUE},{D3DRS_ZWRITEENABLE,TRUE},{D3DRS_ZFUNC,D3DCMP_LESSEQUAL},
        {D3DRS_ALPHATESTENABLE,FALSE},{D3DRS_ALPHABLENDENABLE,FALSE},{D3DRS_SEPARATEALPHABLENDENABLE,FALSE},
        {D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_COLORWRITEENABLE,15},{D3DRS_FOGENABLE,FALSE},
        {D3DRS_STENCILENABLE,FALSE},{D3DRS_SCISSORTESTENABLE,FALSE},{D3DRS_CLIPPLANEENABLE,0},
        {D3DRS_SRGBWRITEENABLE,FALSE},{D3DRS_DEPTHBIAS,0},{D3DRS_SLOPESCALEDEPTHBIAS,0},
        {D3DRS_FILLMODE,D3DFILL_SOLID},{D3DRS_MULTISAMPLEMASK,0xffffffff}
    };
    for(const auto& binding:states)d->SetRenderState(binding.state,binding.value);
}

struct TerrainWork {unsigned batches=0,draws=0,materialBinds=0;};

// The coordinator owns cascade policies and publishes completed maps. This
// scope borrows their attachments and owns the directional raster commands.
template<class Device,class Check>
class GpuPass {
    using Batch=NorthlightWorldMeshPages::Batch;
    static constexpr UINT Size=NorthlightWorldDirectionalTargets::Size;
    Device* device_;
    Check check_;
    IDirect3DSurface9* const depth_;
    const D3DVIEWPORT9 viewport_{0,0,Size,Size,0,1};

    bool bindTarget(IDirect3DSurface9* target,IDirect3DSurface9* depth,const D3DVIEWPORT9& viewport,const char* operation){
        device_->SetDepthStencilSurface(nullptr);
        if(!check_(device_->SetRenderTarget(0,target),operation))return false;
        device_->SetDepthStencilSurface(depth);
        device_->SetViewport(&viewport);
        return true;
    }

    bool bind(IDirect3DSurface9* target,const char* operation){
        return bindTarget(target,depth_,viewport_,operation);
    }

    bool clear(const char* operation){
        return check_(device_->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff,1,0),operation);
    }

public:
    struct Geometry {
        IDirect3DVertexDeclaration9* declaration;
        IDirect3DVertexBuffer9* vertices;
        IDirect3DIndexBuffer9* indices;
        UINT stride;
    };
    struct CachePrograms {
        IDirect3DVertexShader9* vertex;
        IDirect3DPixelShader9* base;
        IDirect3DPixelShader9* opaque;
        IDirect3DPixelShader9* opaqueFast;
    };

private:
    void bindCasters(const Geometry& geometry,const float* matrix,IDirect3DVertexShader9* vertex,IDirect3DPixelShader9* pixel){
        device_->SetVertexDeclaration(geometry.declaration);
        device_->SetStreamSource(0,geometry.vertices,0,geometry.stride);
        for(unsigned i=0;i<4;++i)device_->SetStreamSourceFreq(i,1);
        device_->SetIndices(geometry.indices);
        device_->SetVertexShader(vertex);
        device_->SetVertexShaderConstantF(0,matrix,4);
        device_->SetPixelShader(pixel);
    }

public:
    // One cache redraw, including all dirty rectangles in a slice. The
    // caller supplies already ordered batches and publishes the cache key.
    class CachePass {
        GpuPass& gpu_;
        const bool partial_;
        const CachePrograms programs_;
        IDirect3DPixelShader9* shader_;
        UINT page_=UINT_MAX,cutout_=UINT_MAX;
        const NorthlightShadowBounds::TexelRect* scissor_=nullptr;

    public:
        CachePass(GpuPass& gpu,bool partial,CachePrograms programs):
            gpu_(gpu),partial_(partial),programs_(programs),shader_(programs.base){}
        CachePass(const CachePass&)=delete;
        CachePass& operator=(const CachePass&)=delete;
        ~CachePass(){if(partial_)gpu_.device_->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);}

        bool bind(IDirect3DSurface9* target,IDirect3DSurface9* cacheDepth){
            constexpr UINT cacheSize=NorthlightWorldDirectionalTargets::CacheSize;
            const D3DVIEWPORT9 viewport{0,0,cacheSize,cacheSize,0,1};
            return gpu_.bindTarget(target,cacheDepth,viewport,"shadow cache target");
        }

        bool clear(DWORD rectCount=0,const D3DRECT* rects=nullptr){
            if(!partial_)return gpu_.clear("clear shadow cache");
            auto* d=gpu_.device_;
            // Explicit rectangles clear both cache color and shared depth.
            d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
            if(rectCount&&!gpu_.check_(d->Clear(rectCount,rects,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff,1,0),
                                      "clear shadow cache rects"))return false;
            d->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE);
            return true;
        }

        void bindCasters(const Geometry& geometry,const float* matrix){
            gpu_.bindCasters(geometry,matrix,programs_.vertex,programs_.base);
        }

        template<class BindPage>
        bool draw(const Batch& batch,IDirect3DPixelShader9* shader,const NorthlightShadowBounds::TexelRect* rect,
                  const float* cutoffs,IDirect3DTexture9* const* materials,const BindPage& bindPage){
            auto* d=gpu_.device_;
            const auto& check=gpu_.check_;
            if(shader!=shader_){
                if(!check(d->SetPixelShader(shader),"static cache depth variant"))return false;
                shader_=shader;
            }
            if(shader!=programs_.opaque&&shader!=programs_.opaqueFast&&batch.material!=cutout_){
                const float value[]={1,1,1,cutoffs[batch.material]};
                if(!check(d->SetPixelShaderConstantF(0,value,1),"static cache cutoff")||
                   !check(d->SetTexture(0,materials[batch.material]),"static cache texture"))return false;
                cutout_=batch.material;
            }
            if(!bindPage(batch,page_))return false;
            if(rect&&rect!=scissor_){
                const RECT window{LONG(rect->left),LONG(rect->top),LONG(rect->right),LONG(rect->bottom)};
                if(!check(d->SetScissorRect(&window),"static cache scissor"))return false;
                scissor_=rect;
            }
            return check(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,batch.minVertex,batch.vertexCount,batch.start,batch.count),
                         "static shadow cache draw");
        }

        bool scissorDisabled() const {
            DWORD enabled=FALSE;
            return SUCCEEDED(gpu_.device_->GetRenderState(D3DRS_SCISSORTESTENABLE,&enabled))&&!enabled;
        }
    };

    GpuPass(Device* device,Check check,IDirect3DSurface9* depth):device_(device),check_(check),depth_(depth){}
    CachePass cache(bool partial,CachePrograms programs){return CachePass(*this,partial,programs);}

    void wrapCasterSampler(){
        device_->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP);
        device_->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP);
    }
    void finishCacheBand(){
        device_->SetTexture(0,nullptr);
        wrapCasterSampler();
    }
    void configureCasterSampler(){
        wrapCasterSampler();
        device_->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);
        device_->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
        device_->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
        device_->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);
    }

    bool beginCascade(IDirect3DSurface9* target,const Geometry& geometry,const float* matrix,
                      IDirect3DVertexShader9* vertex,IDirect3DPixelShader9* pixel){
        if(!bind(target,"shadow scratch target")||!clear("clear shadow"))return false;
        bindCasters(geometry,matrix,vertex,pixel);
        return true;
    }

    template<class Visit,class BindPage>
    bool terrain(IDirect3DPixelShader9* shader,UINT primitiveLimit,bool legacy,const float* cutoffs,
                 IDirect3DTexture9* const* materials,TerrainWork& work,Visit visit,BindPage bindPage){
        UINT page=UINT_MAX,material=UINT_MAX;
        NorthlightDirectionalShadowBatches pending(primitiveLimit);
        const auto draw=[&](const Batch& batch,IDirect3DPixelShader9*,const NorthlightShadowBounds::TexelRect*){
            if(legacy||material!=batch.material){
                const float value[]={1,1,1,cutoffs[batch.material]};
                if(!check_(device_->SetPixelShaderConstantF(0,value,1),"directional terrain cutoff")||
                   !check_(device_->SetTexture(0,materials[batch.material]),"directional terrain texture"))return false;
                material=batch.material;
                ++work.materialBinds;
            }
            ++work.draws;
            return bindPage(batch,page)&&check_(device_->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,
                batch.minVertex,batch.vertexCount,batch.start,batch.count),"static shadow cache draw");
        };
        return visit([&](const Batch& batch){
            ++work.batches;
            if(legacy)return draw(batch,shader,nullptr);
            return pending.add(batch,shader,nullptr,false,draw);
        })&&pending.flush(draw);
    }

    template<class Touch>
    bool liveTerrain(const Geometry& geometry,IDirect3DVertexDeclaration9* restore,UINT vertices,UINT start,UINT count,Touch touch){
        const float opaque[]={1,1,1,-1};
        device_->SetPixelShaderConstantF(0,opaque,1);
        device_->SetTexture(0,nullptr);
        if(!check_(device_->SetVertexDeclaration(geometry.declaration),"live terrain declaration")||
           !check_(device_->SetStreamSource(0,geometry.vertices,0,geometry.stride),"live terrain vertices"))return false;
        device_->SetIndices(geometry.indices);
        touch();
        return check_(device_->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,vertices,start,count),"live terrain shadow")&&
            check_(device_->SetVertexDeclaration(restore),"restore terrain declaration");
    }

    bool beginNearReplay(IDirect3DSurface9* target,bool keepTerrain){
        if(!bind(target,"near replay target")||(!keepTerrain&&!clear("clear near replays")))return false;
        device_->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP);
        device_->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP);
        return true;
    }

    void bindReplayShader(IDirect3DPixelShader9* shader){device_->SetPixelShader(shader);}

    bool beginActor(IDirect3DSurface9* target,bool clearDepth=true){
        return bind(target,"actor shadow target")&&(!clearDepth||clear("clear actor shadow"));
    }

    void bindActorProjection(const float* matrix,IDirect3DPixelShader9* shader){
        device_->SetVertexShaderConstantF(0,matrix,4);
        bindReplayShader(shader);
    }
};

} // namespace NorthlightDirectionalShadow
