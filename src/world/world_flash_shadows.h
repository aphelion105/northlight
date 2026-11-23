#pragma once
#include "screen_quad.h"
#include <array>
#include <climits>
#include <d3d9.h>
#include "local_light_selection.h"
#include "world_compiled_shaders.h"
#include "world_math.h"

// Included after logf. Flash maps are current-frame inputs to local lighting.
// Directional depth and caster programs are borrowed for the duration of a draw.
class NorthlightWorldFlashShadows {
public:
    static constexpr unsigned SlotCount=2;
    struct CasterPipeline {
        IDirect3DSurface9* depth;
        IDirect3DVertexDeclaration9* declaration;
        IDirect3DVertexShader9* vertex;
        IDirect3DPixelShader9 *opaque,*replay;
    };
    struct TerrainDraw {
        IDirect3DVertexDeclaration9* declaration;
        IDirect3DVertexBuffer9* vertices;
        IDirect3DIndexBuffer9* indices;
        UINT stride,vertexCount,start,count;
    };
    class CasterCommands {
        NorthlightWorldFlashShadows& owner_;
        IDirect3DDevice9* device_;
        IDirect3DPixelShader9* replay_;
        UINT boundPage_=UINT_MAX;

    public:
        CasterCommands(NorthlightWorldFlashShadows& owner,IDirect3DDevice9* device,IDirect3DPixelShader9* replay):
            owner_(owner),device_(device),replay_(replay){}

        bool check(HRESULT hr,const char* stage){return owner_.flashCheck(hr,stage);}

        template<class Batch,class BindPage>
        bool drawWorld(const Batch& batch,IDirect3DTexture9* texture,float cutoff,const BindPage& bindPage) {
            const float material[]={0,0,0,cutoff};
            return check(device_->SetPixelShaderConstantF(0,material,1),"material")&&
                check(device_->SetTexture(0,texture),"alpha texture")&&bindPage(batch,boundPage_)&&
                check(device_->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,batch.minVertex,batch.vertexCount,batch.start,batch.count),"world caster");
        }

        template<class Touch>
        bool drawTerrain(const TerrainDraw& terrain,const Touch& touch){
            const float opaque[]={0,0,0,-1};
            device_->SetPixelShaderConstantF(0,opaque,1);
            device_->SetTexture(0,nullptr);
            device_->SetVertexDeclaration(terrain.declaration);
            device_->SetStreamSource(0,terrain.vertices,0,terrain.stride);
            device_->SetIndices(terrain.indices);
            touch();
            return check(device_->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,terrain.vertexCount,terrain.start,terrain.count),"live terrain");
        }

        void bindReplay(){device_->SetPixelShader(replay_);}
    };

private:
    friend struct FlashShadowTest;
    friend struct WorldScreenEffectsTest;
    using V=NorthlightGI::Vec3;
    template<class T> static void drop(T*& pointer){if(pointer){pointer->Release();pointer=nullptr;}}

    struct FlashSlot {
        IDirect3DTexture9* map[2]={};IDirect3DSurface9* surface[2]={};
        float matrix[2][16]={};V direction{};
        NorthlightLocalLights::Light source{};bool ready=false;
    };
    std::array<FlashSlot,SlotCount> slots_;
    IDirect3DPixelShader9* flashShadowPS=nullptr;
    unsigned flashFailures=0;DWORD flashRetryAt=0;
    bool flashCheck(HRESULT hr,const char* operation){
        if(SUCCEEDED(hr))return true;
        if(flashFailures++<8)logf("FLASH shadow skipped: %s hr=%08lx",operation,(unsigned long)hr);
        flashRetryAt=GetTickCount()+1000;return false;
    }
    bool flashResources(IDirect3DDevice9* d,FlashSlot& s,const CasterPipeline& pipeline){
        if(flashRetryAt&&LONG(GetTickCount()-flashRetryAt)<0)return false;
        if(!flashShadowPS&&!flashCheck(d->CreatePixelShader(kFlashShadowShader,&flashShadowPS),"lighting shader"))return false;
        for(unsigned c=0;c<2;++c){
            if(!s.map[c]&&!flashCheck(d->CreateTexture(1024,1024,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&s.map[c],nullptr),"map"))return false;
            if(!s.surface[c]&&!flashCheck(s.map[c]->GetSurfaceLevel(0,&s.surface[c]),"map surface"))return false;
        }
        return flashShadowPS&&pipeline.depth&&pipeline.vertex&&pipeline.opaque&&pipeline.replay;
    }
    template<class DrawStatic,class DrawScene>
    bool renderFlashMap(IDirect3DDevice9* d,const FlashSlot& s,unsigned cascade,const CasterPipeline& pipeline,
                        const DrawStatic& drawStatic,const DrawScene& drawScene){
        const float* matrix=s.matrix[cascade];
        for(unsigned i=0;i<16;++i)if(!flashCheck(d->SetTexture(i,nullptr),"unbind sampler"))return false;
        for(unsigned i=1;i<4;++i)if(!flashCheck(d->SetRenderTarget(i,nullptr),"unbind MRT"))return false;
        D3DVIEWPORT9 vp={0,0,1024,1024,0,1};
        if(!flashCheck(d->SetDepthStencilSurface(nullptr),"unbind depth")||!flashCheck(d->SetRenderTarget(0,s.surface[cascade]),"target")||
           !flashCheck(d->SetDepthStencilSurface(pipeline.depth),"depth target")||!flashCheck(d->SetViewport(&vp),"viewport")||
           !flashCheck(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff,1,0),"clear"))return false;
        // Loaded offscreen WMO/M2 placements participate, as they do for the sun.
        if(!drawStatic(matrix))return false;
        for(unsigned i=0;i<4;++i)d->SetStreamSourceFreq(i,1);
        d->SetVertexDeclaration(pipeline.declaration);d->SetVertexShader(pipeline.vertex);d->SetVertexShaderConstantF(0,matrix,4);d->SetPixelShader(pipeline.opaque);
        d->SetRenderState(D3DRS_ZENABLE,TRUE);d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);
        d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
        d->SetRenderState(D3DRS_COLORWRITEENABLE,15);d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
        d->SetRenderState(D3DRS_DEPTHBIAS,0);d->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS,0);d->SetRenderState(D3DRS_CLIPPLANEENABLE,0);
        d->SetRenderState(D3DRS_FOGENABLE,FALSE);d->SetRenderState(D3DRS_STENCILENABLE,FALSE);d->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID);
        for(auto k:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})d->SetSamplerState(0,k,D3DTEXF_LINEAR);
        d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);
        d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP);d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP);
        CasterCommands casters(*this,d,pipeline.replay);
        return drawScene(matrix,casters);
    }

public:
    NorthlightWorldFlashShadows()=default;
    NorthlightWorldFlashShadows(const NorthlightWorldFlashShadows&)=delete;
    NorthlightWorldFlashShadows& operator=(const NorthlightWorldFlashShadows&)=delete;
    ~NorthlightWorldFlashShadows(){reset();}

    void reset(){
        for(auto& s:slots_){for(auto& p:s.surface)drop(p);for(auto& p:s.map)drop(p);s=FlashSlot{};}
        drop(flashShadowPS);flashRetryAt=0;
    }
    void invalidate(){for(auto& slot:slots_)slot.ready=false;}
    bool ready()const{for(const auto& slot:slots_)if(slot.ready)return true;return false;}
    template<class Visit> void visitTextures(const Visit& visit)const{
        for(const auto& slot:slots_)for(auto* texture:slot.map)visit(texture);
    }

    template<class Prepare,class DrawStatic,class DrawScene>
    void render(IDirect3DDevice9* d,unsigned slot,const CasterPipeline& pipeline,const NorthlightLocalLights::Light& light,
                V pivot,V direction,const Prepare& prepare,const DrawStatic& drawStatic,const DrawScene& drawScene){
        auto& s=slots_[slot];s.ready=false;s.source=light;s.direction=direction;
        if(!flashResources(d,s,pipeline))return;
        prepare();
        // The sky model gives the orthographic caster direction. Local
        // receiver brightness still uses the finite source distance.
        NorthlightWorldMath::shadowMatrix(pivot,s.direction,48,s.matrix[0]);
        NorthlightWorldMath::shadowMatrix(pivot,s.direction,192,s.matrix[1]);
        s.ready=renderFlashMap(d,s,0,pipeline,drawStatic,drawScene)&&renderFlashMap(d,s,1,pipeline,drawStatic,drawScene);
    }

    bool drawLighting(IDirect3DDevice9* d,UINT width,UINT height){
        bool drawn=false;
        for(const auto& s:slots_)if(s.ready){
            float position[]={s.source.position[0],s.source.position[1],s.source.position[2],s.source.attenuationEnd};
            float color[]={s.source.diffuse[0],s.source.diffuse[1],s.source.diffuse[2],NorthlightLocalLightSelection::transport(s.source)};
            const float direction[]={s.direction.x,s.direction.y,s.direction.z,0};
            if(!flashCheck(d->SetPixelShader(flashShadowPS),"shadow correction")||!flashCheck(d->SetTexture(2,s.map[0]),"near map")||
               !flashCheck(d->SetTexture(3,s.map[1]),"far map")||!flashCheck(d->SetPixelShaderConstantF(7,s.matrix[0],8),"map matrices")||
               !flashCheck(d->SetPixelShaderConstantF(16,direction,1),"direction")||!flashCheck(d->SetPixelShaderConstantF(36,position,1),"position")||
               !flashCheck(d->SetPixelShaderConstantF(44,color,1),"color"))continue;
            for(unsigned sampler:{2u,3u}){
                d->SetSamplerState(sampler,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(sampler,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
                d->SetSamplerState(sampler,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(sampler,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
                d->SetSamplerState(sampler,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(sampler,D3DSAMP_SRGBTEXTURE,FALSE);
            }
            drawn=true;flashCheck(NorthlightScreen::drawViewportQuad(d,width,height),"shadow correction draw");
        }
        return drawn;
    }
};
