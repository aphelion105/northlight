#pragma once
#include "screen_quad.h"
#include <d3d9.h>
#include <climits>
#include <vector>
#include "point_transmission_filter.h"

namespace NorthlightPointShadow {

// One cube submission. The scheduler lends the surfaces and shaders and
// publishes a face only after its static and dynamic caster draws succeed.
template<class Device,class Check>
class FacePass {
    enum class Destination {Direct,Copied,Union};
    Device* device_;
    Check check_;
    const D3DVIEWPORT9 viewport_;
    IDirect3DSurface9* const depth_;
    IDirect3DPixelShader9* const caster_;
    IDirect3DSurface9* face_=nullptr;
    bool prepared_=false,seeded_=false,attached_=false;
    Destination destination_=Destination::Direct;
    float sphere_[4]={};
    IDirect3DTexture9* material_=nullptr;
    float cutoff_=0;
    bool materialKnown_=false;

    bool bindCaster(IDirect3DSurface9* target,const char* operation,bool preserve,bool alreadyBound=false){
        return (alreadyBound||check_(device_->SetRenderTarget(0,target),operation))&&
            check_(device_->SetDepthStencilSurface(depth_),"cube depth target")&&
            check_(device_->SetViewport(&viewport_),"cube viewport")&&
            check_(device_->Clear(0,nullptr,D3DCLEAR_ZBUFFER|(preserve?0:D3DCLEAR_TARGET),0xffffffff,1,0),"clear cube")&&
            check_(device_->SetPixelShader(caster_),"cube PS")&&
            check_(device_->SetPixelShaderConstantF(1,sphere_,1),"cube sphere");
    }

public:
    enum class MeshPass {Static,Terrain};
    struct LiveGeometry {
        IDirect3DVertexDeclaration9* declaration;
        IDirect3DVertexBuffer9* vertices;
        IDirect3DIndexBuffer9* indices;
        UINT stride,vertexCount;
        size_t indexBase;
    };

    FacePass(Device* device,Check check,UINT size,IDirect3DSurface9* depth,IDirect3DPixelShader9* caster):
        device_(device),check_(check),viewport_{0,0,size,size,0,1},depth_(depth),caster_(caster){}
    FacePass(const FacePass&)=delete;
    ~FacePass(){if(attached_)device_->SetRenderTarget(1,nullptr);}

    bool beginFace(IDirect3DSurface9* face,float range){
        if(!prepared_){
            for(unsigned i=0;i<13;++i)if(!check_(device_->SetTexture(i,nullptr),"unbind cube samplers"))return false;
            for(unsigned i=1;i<4;++i)if(!check_(device_->SetRenderTarget(i,nullptr),"unbind cube MRT"))return false;
            for(unsigned i=0;i<4;++i)if(!check_(device_->SetStreamSourceFreq(i,1),"cube stream frequency"))return false;
            if(!check_(device_->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE),"cube blend")||
               !check_(device_->SetRenderState(D3DRS_COLORWRITEENABLE,15),"cube color mask"))return false;
            prepared_=true;
        }
        face_=face;
        sphere_[0]=range*range;
        sphere_[1]=1.f/viewport_.Width;
        seeded_=false;
        materialKnown_=false;
        return true;
    }

    bool beginStatic(IDirect3DSurface9* target,bool pair,bool& canPair){
        if(!check_(device_->SetDepthStencilSurface(nullptr),"unbind cube depth"))return false;
        if(pair){
            attached_=SUCCEEDED(device_->SetRenderTarget(0,face_))&&
                SUCCEEDED(device_->SetRenderState(D3DRS_COLORWRITEENABLE1,15))&&
                SUCCEEDED(device_->SetRenderTarget(1,target));
            seeded_=attached_;
            if(!seeded_)canPair=false;
        }
        return bindCaster(target,"cube cache target",false,seeded_);
    }

    bool finishStatic(){
        if(!attached_)return true;
        if(!check_(device_->SetRenderTarget(1,nullptr),"unbind static cube cache"))return false;
        attached_=false;
        return true;
    }

    bool seeded()const{return seeded_;}
    bool usesUnion()const{return destination_==Destination::Union;}

    bool bindWorldGeometry(IDirect3DVertexDeclaration9* declaration,IDirect3DVertexShader9* shader,
                           const float* matrix,IDirect3DVertexBuffer9* vertices,UINT stride,IDirect3DIndexBuffer9* indices){
        return check_(device_->SetVertexDeclaration(declaration),"cube declaration")&&
            check_(device_->SetVertexShader(shader),"cube static VS")&&
            check_(device_->SetVertexShaderConstantF(0,matrix,4),"cube face matrix")&&
            check_(device_->SetStreamSource(0,vertices,0,stride),"cube static vertices")&&
            check_(device_->SetIndices(indices),"cube static indices")&&
            check_(device_->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP),"cube alpha U")&&
            check_(device_->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP),"cube alpha V");
    }

    bool material(IDirect3DTexture9* texture,float cutoff){
        if(!materialKnown_||material_!=texture)
            if(!check_(device_->SetTexture(0,texture),"cube alpha texture"))return false;
        if(!materialKnown_||cutoff_!=cutoff){
            const float value[]={0,0,0,cutoff};
            if(!check_(device_->SetPixelShaderConstantF(0,value,1),"cube material"))return false;
        }
        materialKnown_=true;
        material_=texture;
        cutoff_=cutoff;
        return true;
    }

    template<class Next,class BindPage,class Record>
    bool drawMeshRanges(size_t count,MeshPass pass,const std::vector<IDirect3DTexture9*>& textures,
                        const std::vector<float>& cutoffs,const Next& next,const BindPage& bindPage,const Record& record){
        const bool statics=pass==MeshPass::Static;
        UINT boundPage=UINT_MAX;
        for(size_t at=0;at<count;){
            const auto batch=next(at);
            if(batch.material>=textures.size()||batch.material>=cutoffs.size())return check_(E_FAIL,"cube material range");
            const float cutoff=cutoffs[batch.material]<=0?-1.f:cutoffs[batch.material];
            NorthlightPointTransmissionFilter<Device> filter;
            if(statics&&!check_(filter.begin(device_,batch.pointOnly&&!(cutoff<0)),"cube transmission filter"))return false;
            // The static VS has no COLOR output. Coverage comes from the cutout texture.
            if(!material(cutoff<0?nullptr:textures[batch.material],cutoff)||
               !bindPage(batch,boundPage)||
               !check_(device_->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,batch.minVertex,batch.vertexCount,batch.start,batch.count),
                       statics?"cube static draw":"cube terrain draw"))return false;
            if(statics&&!check_(filter.restore(),"restore cube alpha filter"))return false;
            record(batch.count);
        }
        return true;
    }

    bool copyCachedBase(IDirect3DSurface9* cacheSurface){
        if(!cacheSurface||FAILED(device_->StretchRect(cacheSurface,nullptr,face_,nullptr,D3DTEXF_NONE)))return false;
        destination_=Destination::Copied;
        return true;
    }

    void chooseDestination(bool cached,IDirect3DSurface9* cacheSurface,bool staticOnly,bool& minBlend){
        if(seeded_){destination_=Destination::Copied;return;}
        if(!cached){destination_=Destination::Direct;return;}
        destination_=Destination::Union;
        if(!staticOnly&&!minBlend)return;
        if(!copyCachedBase(cacheSurface))minBlend=false;
    }

    template<class BindGeometry>
    bool beginDynamic(IDirect3DSurface9* scratch,bool& minBlend,BindGeometry bindGeometry){
        const bool copied=destination_==Destination::Copied;
        if(seeded_){
            if(!check_(device_->Clear(0,nullptr,D3DCLEAR_ZBUFFER,0,1,0),"clear cube dynamic depth"))return false;
        }else{
            if(!check_(device_->SetDepthStencilSurface(nullptr),"unbind cube depth")||
               !bindCaster(usesUnion()?scratch:face_,"cube dynamic target",copied)||!bindGeometry())return false;
        }
        if(!copied)return true;
        if(SUCCEEDED(device_->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE))&&
           SUCCEEDED(device_->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE))&&
           SUCCEEDED(device_->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_MIN))&&
           SUCCEEDED(device_->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE)))return true;
        minBlend=false;
        return check_(E_FAIL,"cube min blend");
    }

    bool bindLiveTerrain(IDirect3DVertexDeclaration9* declaration,IDirect3DVertexBuffer9* vertices,UINT stride,IDirect3DIndexBuffer9* indices){
        const float opaque[]={1,1,1,-1};
        return check_(device_->SetPixelShaderConstantF(0,opaque,1),"cube live material")&&
            check_(device_->SetTexture(0,nullptr),"cube live alpha")&&
            check_(device_->SetVertexDeclaration(declaration),"cube live declaration")&&
            check_(device_->SetStreamSource(0,vertices,0,stride),"cube live vertices")&&
            check_(device_->SetIndices(indices),"cube live indices");
    }
    bool restoreStaticDeclaration(IDirect3DVertexDeclaration9* declaration){
        return check_(device_->SetVertexDeclaration(declaration),"cube restore terrain declaration");
    }

    template<class Indices,class Batch,class Touch,class Record>
    bool drawLiveTerrain(const LiveGeometry& geometry,IDirect3DVertexDeclaration9* staticDeclaration,
                         const Indices& candidates,const Batch& batchAt,const Touch& touch,const Record& record){
        if(!bindLiveTerrain(geometry.declaration,geometry.vertices,geometry.stride,geometry.indices))return false;
        // The ring slice is in use after binding and must survive until the frame fence.
        touch();
        for(size_t index:candidates){
            const auto& batch=batchAt(index);
            if(!check_(device_->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,geometry.vertexCount,
                UINT(geometry.indexBase+batch.start),batch.count),"cube live draw"))return false;
            record(batch.count);
        }
        return restoreStaticDeclaration(staticDeclaration);
    }

    bool resolve(bool staticOnly,IDirect3DTexture9* cached,IDirect3DTexture9* scratch,IDirect3DPixelShader9* shader){
        if(destination_==Destination::Copied){
            return staticOnly||(check_(device_->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE),"cube min blend restore")&&
                check_(device_->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD),"cube min operation restore"));
        }
        if(destination_==Destination::Direct){
            return !staticOnly||(check_(device_->SetDepthStencilSurface(nullptr),"unbind empty cube depth")&&
                check_(device_->SetRenderTarget(0,face_),"empty cube target")&&
                check_(device_->SetViewport(&viewport_),"empty cube viewport")&&
                check_(device_->Clear(0,nullptr,D3DCLEAR_TARGET,0xffffffff,1,0),"clear empty cube"));
        }
        if(!check_(device_->SetDepthStencilSurface(nullptr),"unbind cube depth")||
           !check_(device_->SetRenderTarget(0,face_),"cube target")||!check_(device_->SetViewport(&viewport_),"cube union viewport"))return false;
        device_->SetRenderState(D3DRS_ZENABLE,FALSE);
        device_->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
        device_->SetVertexShader(nullptr);
        device_->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);
        device_->SetStreamSourceFreq(0,1);
        device_->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0);
        device_->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);
        if(!check_(device_->SetTexture(0,staticOnly?cached:scratch),"cube union scratch")||
           !check_(device_->SetTexture(1,cached),"cube union cache"))return false;
        for(unsigned sampler=0;sampler<2;++sampler){
            device_->SetSamplerState(sampler,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
            device_->SetSamplerState(sampler,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
            device_->SetSamplerState(sampler,D3DSAMP_MINFILTER,D3DTEXF_POINT);
            device_->SetSamplerState(sampler,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
            device_->SetSamplerState(sampler,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
            device_->SetSamplerState(sampler,D3DSAMP_SRGBTEXTURE,FALSE);
        }
        const float constants[2][4]={{0,0,0,float(viewport_.Width)},{1.f/viewport_.Width,1.f/viewport_.Height,0,0}};
        return check_(device_->SetPixelShaderConstantF(0,&constants[0][0],2),"cube union constants")&&
            check_(device_->SetPixelShader(shader),"cube union shader")&&
            check_(NorthlightScreen::drawViewportQuad(device_,viewport_.Width,viewport_.Height),"cube union");
    }

    void finishFace(){
        device_->SetTexture(0,nullptr);
        device_->SetTexture(1,nullptr);
        device_->SetRenderState(D3DRS_ZENABLE,TRUE);
        device_->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);
        device_->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);
        device_->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
    }
};

} // namespace NorthlightPointShadow
