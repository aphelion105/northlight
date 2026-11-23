#pragma once
#include "world_point_targets.h"
#include "world_gi.h"
#include "saved_state.h"
#include "screen_quad.h"
#include <array>
#include <utility>

namespace NorthlightPointPipelineWarmup {

struct Shaders {
    IDirect3DVertexDeclaration9* declaration;
    IDirect3DVertexShader9* casterVertex;
    IDirect3DPixelShader9* casterPixel;
    IDirect3DPixelShader9* unionPixel;
};

// Device initialization borrows private attachments and launches first-use
// work before the caller can begin a scene. The caller still owns publication.
inline HRESULT submit(IDirect3DDevice9* d,const NorthlightWorldPointTargets& pointTargets,
                      IDirect3DSurface9* face,const Shaders& shaders,bool minBlend,
                      bool& kicked,HRESULT& restoreStatus){
    constexpr UINT size=NorthlightWorldPointTargets::Size;
    HRESULT hr=S_OK;
    const auto ok=[&](HRESULT value){hr=value;return SUCCEEDED(value);};
    {
        SavedState saved(d);const bool captured=saved.ok;saved.ok=false;if(!captured)return E_FAIL;
        // SavedState's general path tolerates absent MRT/depth getters. This
        // optional init draw needs a proven original state before mutation.
        for(unsigned i=0;i<4;++i){IDirect3DSurface9* surface=nullptr;const HRESULT got=d->GetRenderTarget(i,&surface);
            const bool same=surface==saved.targets[i];if(surface)surface->Release();
            if((FAILED(got)&&!(i&&got==D3DERR_NOTFOUND))||!same)return E_FAIL;
        }
        IDirect3DSurface9* depth=nullptr;const HRESULT gotDepth=d->GetDepthStencilSurface(&depth);
        const bool sameDepth=depth==saved.depth;if(depth)depth->Release();
        if((FAILED(gotDepth)&&gotDepth!=D3DERR_NOTFOUND)||!sameDepth)return E_FAIL;
        std::array<float,1024> vsFloat;std::array<float,896> psFloat;
        std::array<int,64> vsInt,psInt;std::array<BOOL,16> vsBool,psBool;
        if(!ok(d->GetVertexShaderConstantF(0,vsFloat.data(),256))||!ok(d->GetPixelShaderConstantF(0,psFloat.data(),224))||
           !ok(d->GetVertexShaderConstantI(0,vsInt.data(),16))||!ok(d->GetPixelShaderConstantI(0,psInt.data(),16))||
           !ok(d->GetVertexShaderConstantB(0,vsBool.data(),16))||!ok(d->GetPixelShaderConstantB(0,psBool.data(),16)))return hr;
        if(!ok(d->BeginScene()))return hr;
        const auto draw=[&]()->HRESULT{
            for(unsigned i=0;i<14;++i)if(!ok(d->SetTexture(i,nullptr)))return hr;
            for(unsigned i=1;i<4;++i)if(!ok(d->SetRenderTarget(i,nullptr)))return hr;
            for(unsigned i=0;i<4;++i)if(!ok(d->SetStreamSourceFreq(i,1)))return hr;
            const std::pair<D3DRENDERSTATETYPE,DWORD> states[]={
                {D3DRS_ZENABLE,TRUE},{D3DRS_ZWRITEENABLE,TRUE},{D3DRS_ZFUNC,D3DCMP_LESSEQUAL},
                {D3DRS_ALPHATESTENABLE,FALSE},{D3DRS_ALPHABLENDENABLE,FALSE},{D3DRS_SEPARATEALPHABLENDENABLE,FALSE},
                {D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_COLORWRITEENABLE,15},{D3DRS_FOGENABLE,FALSE},
                {D3DRS_STENCILENABLE,FALSE},{D3DRS_SCISSORTESTENABLE,FALSE},{D3DRS_CLIPPLANEENABLE,0},
                {D3DRS_SRGBWRITEENABLE,FALSE},{D3DRS_DEPTHBIAS,0},{D3DRS_SLOPESCALEDEPTHBIAS,0},
                {D3DRS_FILLMODE,D3DFILL_SOLID},{D3DRS_MULTISAMPLEMASK,0xffffffff}
            };
            for(const auto& state:states)if(!ok(d->SetRenderState(state.first,state.second)))return hr;
            const D3DVIEWPORT9 viewport={0,0,size,size,0,1};
            if(!ok(d->SetDepthStencilSurface(nullptr))||!ok(d->SetRenderTarget(0,pointTargets.scratchSurface()))||!ok(d->SetDepthStencilSurface(pointTargets.depth()))||
               !ok(d->SetViewport(&viewport))||!ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff,1,0))||
               !ok(d->SetVertexDeclaration(shaders.declaration))||!ok(d->SetVertexShader(shaders.casterVertex))||!ok(d->SetPixelShader(shaders.casterPixel)))return hr;
            const float matrix[]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1},sphere[]={144,1.f/size,0,0};
            if(!ok(d->SetVertexShaderConstantF(0,matrix,4))||!ok(d->SetPixelShaderConstantF(1,sphere,1))||
               !ok(d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP))||!ok(d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP))||
               !ok(d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR))||!ok(d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR))||
               !ok(d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE))||!ok(d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE)))return hr;
            const NorthlightGI::WorldVertex triangle[]={{{-.5f,-.5f,.5f},{0,0,1},0,0},{{.5f,-.5f,.5f},{0,0,1},1,0},{{0,.5f,.5f},{0,0,1},.5f,1}};
            for(unsigned textured=0;textured<2;++textured){const float material[]={0,0,0,textured?.5f:-1.f};
                // Reuse the opaque warmup draw for the MIN pipeline. The
                // textured draw warms the ordinary pipeline without another draw.
                if(!textured&&minBlend&&(!ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE))||!ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE))||
                   !ok(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_MIN))||!ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE))))return hr;
                if(!ok(d->SetPixelShaderConstantF(0,material,1))||!ok(d->SetTexture(0,textured?pointTargets.empty():nullptr))||
                   !ok(d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,triangle,sizeof triangle[0])))return hr;
                if(!textured&&minBlend&&(!ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE))||!ok(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD))))return hr;
            }
            if(!ok(d->SetTexture(0,nullptr))||!ok(d->SetDepthStencilSurface(nullptr))||!ok(d->SetRenderTarget(0,face))||
               !ok(d->SetRenderState(D3DRS_ZENABLE,FALSE))||!ok(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE))||
               !ok(d->SetVertexShader(nullptr))||!ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1))||
               !ok(d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0))||!ok(d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE))||
               !ok(d->SetTexture(0,pointTargets.scratch()))||!ok(d->SetTexture(1,pointTargets.scratch())))return hr;
            for(unsigned sampler=0;sampler<2;++sampler){
                if(!ok(d->SetSamplerState(sampler,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP))||!ok(d->SetSamplerState(sampler,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP))||
                   !ok(d->SetSamplerState(sampler,D3DSAMP_MINFILTER,D3DTEXF_POINT))||!ok(d->SetSamplerState(sampler,D3DSAMP_MAGFILTER,D3DTEXF_POINT))||
                   !ok(d->SetSamplerState(sampler,D3DSAMP_MIPFILTER,D3DTEXF_NONE))||!ok(d->SetSamplerState(sampler,D3DSAMP_SRGBTEXTURE,FALSE)))return hr;
            }
            const float constants[2][4]={{0,0,0,float(size)},{1.f/size,1.f/size,0,0}};
            if(!ok(d->SetPixelShaderConstantF(0,&constants[0][0],2))||!ok(d->SetPixelShader(shaders.unionPixel)))return hr;
            // The full-screen submission sets the viewport again before drawing.
            d->SetViewport(&viewport);
            return NorthlightScreen::drawQuad(d,size,size);
        };
        const HRESULT drawn=draw();HRESULT ended=d->EndScene();
        if(FAILED(ended))ended=d->EndScene(); // Only a successful close proves the scene ended.
        const auto restore=[&]{
            HRESULT result=S_OK;const auto attempt=[&](HRESULT value){if(FAILED(value)&&SUCCEEDED(result))result=value;};
            attempt(d->SetDepthStencilSurface(nullptr));
            for(unsigned i=1;i<4;++i)attempt(d->SetRenderTarget(i,nullptr));
            attempt(d->SetRenderTarget(0,saved.targets[0]));
            for(unsigned i=1;i<4;++i)if(saved.targets[i])attempt(d->SetRenderTarget(i,saved.targets[i]));
            attempt(d->SetDepthStencilSurface(saved.depth));attempt(saved.block->Apply());attempt(d->SetViewport(&saved.viewport));
            // FVF transport must not leave shader constants from the private
            // union draw, even on runtimes with incomplete ALL blocks.
            attempt(d->SetVertexShaderConstantF(0,vsFloat.data(),256));attempt(d->SetPixelShaderConstantF(0,psFloat.data(),224));
            attempt(d->SetVertexShaderConstantI(0,vsInt.data(),16));attempt(d->SetPixelShaderConstantI(0,psInt.data(),16));
            attempt(d->SetVertexShaderConstantB(0,vsBool.data(),16));attempt(d->SetPixelShaderConstantB(0,psBool.data(),16));
            return result;
        };
        restoreStatus=restore();if(FAILED(restoreStatus))restoreStatus=restore();
        if(FAILED(ended)&&SUCCEEDED(restoreStatus))restoreStatus=ended;
        if(FAILED(restoreStatus))return restoreStatus;
        if(FAILED(drawn))return drawn;
    } // Restore every binding/constant plus RT/depth/viewport before submission.
    IDirect3DQuery9* query=nullptr;if(!ok(d->CreateQuery(D3DQUERYTYPE_EVENT,&query)))return hr;
    if(!query)return E_FAIL;
    hr=query->Issue(D3DISSUE_END);
    if(SUCCEEDED(hr)){BOOL value=FALSE;kicked=true;hr=query->GetData(&value,sizeof value,D3DGETDATA_FLUSH);}
    query->Release();
    return hr; // S_FALSE is expected after the one non-blocking submission kick.
}

} // namespace NorthlightPointPipelineWarmup
