#pragma once
#include <d3d9.h>
#include <cmath>
#include <unordered_map>
#include "hdr_ui_patch.h"
#include "hdr_compiled_shaders.h"
#include "saved_state.h"
#include "hdr_quad.h"
#include "hdr_sky_foreground.h"
#include "particle_light_proof.h"

namespace NorthlightHdr {
class Color {
    struct Entry {IDirect3DPixelShader9* original=nullptr;IDirect3DPixelShader9* shader[4]{};bool attempted[4]{};};
    std::unordered_map<IDirect3DPixelShader9*,Entry> shaderBanks[2];
    IDirect3DPixelShader9* world=nullptr;
    IDirect3DPixelShader9* fixedUi=nullptr;IDirect3DPixelShader9* multiFixedUi=nullptr;IDirect3DPixelShader9* diffuseUi=nullptr;IDirect3DPixelShader9* multiFixedSky=nullptr;IDirect3DPixelShader9* fixedAdditive=nullptr;
    IDirect3DPixelShader9* layerShader=nullptr;IDirect3DTexture9* layerTexture=nullptr;
    IDirect3DSurface9* layerSurface=nullptr;IDirect3DSurface9* layerMsaa=nullptr;D3DSURFACE_DESC layerDesc{};
    float uiScale=0;
    template<class T> static void release(T*& p){if(p){p->Release();p=nullptr;}}
    static bool disabledStage(IDirect3DDevice9* d,unsigned stage){
        DWORD op=0,arg=0;if(FAILED(d->GetTextureStageState(stage,D3DTSS_COLOROP,&op)))return false;
        if(op==D3DTOP_DISABLE)return true;
        IDirect3DBaseTexture9* texture=nullptr;
        const bool disabled=SUCCEEDED(d->GetTextureStageState(stage,D3DTSS_COLORARG1,&arg))&&arg==D3DTA_TEXTURE&&
            SUCCEEDED(d->GetTexture(stage,&texture))&&!texture;
        release(texture);return disabled;
    }
    static bool directTextureCoordinates(IDirect3DDevice9* d,unsigned stage,bool additive=false,bool* nativeAdditive=nullptr){
        DWORD transform=0;if(FAILED(d->GetTextureStageState(stage,D3DTSS_TEXTURETRANSFORMFLAGS,&transform)))return false;
        if(transform==D3DTTFF_DISABLE&&!additive)return true;
        // D3D9 ignores affine texture transforms on pretransformed vertices.
        // A single native UV output retains its fixed-function transform.
        // Multi-stage routing and projected coordinates still need the layer.
        DWORD fvf=0;if((transform!=D3DTTFF_COUNT2&&transform!=D3DTTFF_DISABLE)||FAILED(d->GetFVF(&fvf)))return false;
        const bool native=(fvf&D3DFVF_POSITION_MASK)==D3DFVF_XYZ;
        if((fvf&D3DFVF_POSITION_MASK)!=D3DFVF_XYZRHW&&(!additive||!native||
           (fvf&D3DFVF_TEXCOUNT_MASK)!=D3DFVF_TEX1||!(fvf&D3DFVF_DIFFUSE)||(fvf&(3u<<16))))return false;
        IDirect3DVertexShader9* vs=nullptr;const HRESULT hr=d->GetVertexShader(&vs);const bool fixed=!vs;release(vs);
        if(FAILED(hr)||!fixed)return false;
        if(native){
            DWORD value=0;
            // PS 2.0 consumes the original fixed-function lit diffuse output
            // and alpha test. Specular addition is not part of this transfer.
            if(FAILED(d->GetRenderState(D3DRS_SPECULARENABLE,&value))||value)return false;
            if(FAILED(d->GetTextureStageState(stage,D3DTSS_RESULTARG,&value))||value!=D3DTA_CURRENT||
               FAILED(d->GetRenderState(D3DRS_BLENDOP,&value))||value!=D3DBLENDOP_ADD)return false;
            IDirect3DSurface9* target=nullptr;D3DSURFACE_DESC desc{};
            const HRESULT targetHr=d->GetRenderTarget(0,&target);
            const bool singleSample=SUCCEEDED(targetHr)&&target&&SUCCEEDED(target->GetDesc(&desc))&&desc.MultiSampleType==D3DMULTISAMPLE_NONE;
            release(target);if(!singleSample)return false;
            if(nativeAdditive)*nativeAdditive=true;
        }return true;
    }
public:
    unsigned patched=0,rejected=0;
    std::uint64_t layers=0,layerPixels=0,screenDirect=0,attenuationDirect=0,multiTextureDirect=0,fixedAdditiveDirect=0,skyMrtAttempts=0,skyMrtDraws=0;
    ~Color(){reset();}
    std::uint64_t payloadBytes()const{return std::uint64_t(layerDesc.Width)*layerDesc.Height*4*((layerTexture?1:0)+(layerMsaa?unsigned(layerDesc.MultiSampleType):0));}
    void forget(IDirect3DPixelShader9* original){for(auto& shaders:shaderBanks){auto it=shaders.find(original);if(it!=shaders.end()){for(auto*& p:it->second.shader)release(p);release(it->second.original);shaders.erase(it);}}}
    void reset(){for(auto& shaders:shaderBanks){for(auto& e:shaders){for(auto*& p:e.second.shader)release(p);release(e.second.original);}shaders.clear();}release(world);release(fixedUi);release(multiFixedUi);release(diffuseUi);release(multiFixedSky);release(fixedAdditive);release(layerShader);release(layerSurface);release(layerTexture);release(layerMsaa);layerDesc={};uiScale=0;}
    template<class Draw> HRESULT legacyLayer(IDirect3DDevice9* d,float uiWhite,Draw submit,NorthlightStateBlockPool* pool=nullptr){
        SavedState saved(d,pool);if(!saved.ok)return E_FAIL;
        D3DSURFACE_DESC desc{};HRESULT hr=saved.targets[0]->GetDesc(&desc);if(FAILED(hr))return hr;
        ++layers;layerPixels+=std::uint64_t(desc.Width)*desc.Height;
        DWORD enabled=0,src=0,dst=0,writes=15;d->GetRenderState(D3DRS_ALPHABLENDENABLE,&enabled);d->GetRenderState(D3DRS_SRCBLEND,&src);d->GetRenderState(D3DRS_DESTBLEND,&dst);d->GetRenderState(D3DRS_COLORWRITEENABLE,&writes);
        const bool factor=enabled&&((src==D3DBLEND_DESTCOLOR&&dst==D3DBLEND_ZERO)||(src==D3DBLEND_ZERO&&(dst==D3DBLEND_SRCCOLOR||dst==D3DBLEND_INVSRCALPHA)));
        const bool premultiplied=enabled&&!factor&&(src==D3DBLEND_SRCALPHA||dst==D3DBLEND_INVSRCALPHA);
        if(desc.Width!=layerDesc.Width||desc.Height!=layerDesc.Height||desc.MultiSampleType!=layerDesc.MultiSampleType||desc.MultiSampleQuality!=layerDesc.MultiSampleQuality){release(layerSurface);release(layerTexture);release(layerMsaa);layerDesc=desc;}
        if(!layerTexture){hr=d->CreateTexture(desc.Width,desc.Height,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&layerTexture,nullptr);if(SUCCEEDED(hr))hr=layerTexture->GetSurfaceLevel(0,&layerSurface);if(FAILED(hr))return hr;}
        if(desc.MultiSampleType&&!layerMsaa){hr=d->CreateRenderTarget(desc.Width,desc.Height,D3DFMT_A8R8G8B8,desc.MultiSampleType,desc.MultiSampleQuality,FALSE,&layerMsaa,nullptr);if(FAILED(hr))return hr;}
        if(!layerShader){hr=d->CreatePixelShader(reinterpret_cast<const DWORD*>(kHdrLayer),&layerShader);if(FAILED(hr))return hr;}
        for(unsigned i=1;i<4;++i)d->SetRenderTarget(i,nullptr);
        hr=d->SetRenderTarget(0,layerMsaa?layerMsaa:layerSurface);if(FAILED(hr))return hr;
        const D3DVIEWPORT9 full={0,0,desc.Width,desc.Height,0,1};DWORD scissor=0;d->GetRenderState(D3DRS_SCISSORTESTENABLE,&scissor);
        d->SetViewport(&full);d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);d->Clear(0,nullptr,D3DCLEAR_TARGET,factor?0xffffffff:0,1,0);
        d->SetViewport(&saved.viewport);d->SetRenderState(D3DRS_SCISSORTESTENABLE,scissor);
        d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);
        d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_SRCBLENDALPHA,D3DBLEND_ONE);d->SetRenderState(D3DRS_DESTBLENDALPHA,enabled?D3DBLEND_INVSRCALPHA:D3DBLEND_ZERO);d->SetRenderState(D3DRS_BLENDOPALPHA,D3DBLENDOP_ADD);
        hr=submit();if(FAILED(hr))return hr;
        if(layerMsaa){hr=d->StretchRect(layerMsaa,nullptr,layerSurface,nullptr,D3DTEXF_NONE);if(FAILED(hr))return hr;}
        hr=quadState(d,saved.targets[0],desc.Width,desc.Height);if(FAILED(hr))return hr;
        const float scale[]={uiWhite/80,uiWhite/80,uiWhite/80,1},parameters[]={premultiplied?1.f:0.f,factor&&src==D3DBLEND_ZERO&&dst==D3DBLEND_INVSRCALPHA?1.f:0.f,0,0};
        d->SetPixelShaderConstantF(0,scale,1);d->SetPixelShaderConstantF(2,parameters,1);d->SetPixelShader(layerShader);d->SetTexture(0,layerTexture);
        d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_SRCBLEND,factor?D3DBLEND_DESTCOLOR:D3DBLEND_ONE);
        d->SetRenderState(D3DRS_DESTBLEND,factor?D3DBLEND_ZERO:!enabled||dst==D3DBLEND_INVSRCALPHA?D3DBLEND_INVSRCALPHA:D3DBLEND_ONE);
        d->SetRenderState(D3DRS_COLORWRITEENABLE,writes);
        return drawQuad(d,desc.Width,desc.Height);
    }
    IDirect3DPixelShader9* replacement(IDirect3DDevice9* d,IDirect3DPixelShader9* original,float uiWhite,unsigned variant){
        if(!original||variant>3||(variant==3&&uiWhite!=80))return nullptr;
        // World/sky radiance and UI white alternate every frame. Retain both
        // scales; a live UI adjustment invalidates only its own bounded bank.
        auto& shaders=shaderBanks[uiWhite==80?0:1];
        if(uiWhite!=80&&uiScale!=uiWhite){for(auto& e:shaders){for(auto*& p:e.second.shader)release(p);release(e.second.original);}shaders.clear();uiScale=uiWhite;}
        auto it=shaders.find(original);if(it==shaders.end()){if(shaders.size()>=128){++rejected;return nullptr;}it=shaders.emplace(original,Entry{}).first;
            // Internal water variants bypass the proxy's shader-create forget hook.
            original->AddRef();it->second.original=original;}
        auto& entry=it->second;if(entry.attempted[variant])return entry.shader[variant];entry.attempted[variant]=true;
        UINT bytes=0;HRESULT hr=original->GetFunction(nullptr,&bytes);
        if(FAILED(hr)||bytes<12||bytes%4||bytes>65536*4){++rejected;return nullptr;}
        std::vector<std::uint32_t> code(bytes/4),result;
        if(FAILED(original->GetFunction(code.data(),&bytes))){++rejected;return nullptr;}
        // Cache the proof with a separate variant: this PS can also draw UI or
        // non-additive effects at the same white level in the same frame.
        const bool particle=variant==3;
        NorthlightParticleLightProof::Pixel proof;
        if(particle&&!NorthlightParticleLightProof::compilePixel(code.data(),code.size(),proof))return nullptr;
        const bool sm2=code[0]==0xffff0200;
        const auto* transfer=particle?(sm2?kHdrParticlePs2:kHdrParticlePs3):(sm2?kHdrUiPs2:kHdrUiPs3);
        const auto count=particle?(sm2?sizeof(kHdrParticlePs2)/4:sizeof(kHdrParticlePs3)/4):(sm2?sizeof(kHdrUiPs2)/4:sizeof(kHdrUiPs3)/4);
        if(!NorthlightHdrUi::patch(code.data(),code.size(),transfer,count,variant==2?80.f:uiWhite,variant==1,result,nullptr,particle)||
           FAILED(d->CreatePixelShader(reinterpret_cast<const DWORD*>(result.data()),&entry.shader[variant]))){++rejected;return nullptr;}
        ++patched;return entry.shader[variant];
    }
    template<class Draw> HRESULT fixedDraw(IDirect3DDevice9* d,float uiWhite,Draw submit,NorthlightStateBlockPool* pool=nullptr,
                                          SkyForeground* skyCapture=nullptr,IDirect3DSurface9* sky=nullptr,bool* captured=nullptr){
        const bool mrt=skyCapture&&sky&&captured;if(captured)*captured=false;
        auto fallback=[&]{return mrt?S_FALSE:legacyLayer(d,uiWhite,submit,pool);};
        if(mrt){
            ++skyMrtAttempts;
            if(uiWhite!=80)return S_FALSE;
            IDirect3DPixelShader9* ps=nullptr;const HRESULT hr=d->GetPixelShader(&ps);const bool fixed=!ps;release(ps);
            if(FAILED(hr)||!fixed)return S_FALSE;
            IDirect3DVertexShader9* vs=nullptr;const HRESULT vertexHr=d->GetVertexShader(&vs);const bool fixedVertex=!vs;release(vs);
            // Keep native alpha threshold/SELECTARG2 paths in their original
            // shader: separate FF/PS interpolation can differ at a boundary.
            DWORD srgb=0,specular=0,alphaTest=0;
            if(FAILED(vertexHr)||!fixedVertex||FAILED(d->GetRenderState(D3DRS_SRGBWRITEENABLE,&srgb))||srgb||
               FAILED(d->GetRenderState(D3DRS_SPECULARENABLE,&specular))||specular||
               FAILED(d->GetRenderState(D3DRS_ALPHATESTENABLE,&alphaTest))||alphaTest)return S_FALSE;
            for(unsigned stage=0;stage<2;++stage){
                DWORD min=0,mag=0,mip=0;
                if(FAILED(d->GetSamplerState(stage,D3DSAMP_MINFILTER,&min))||FAILED(d->GetSamplerState(stage,D3DSAMP_MAGFILTER,&mag))||
                   min!=mag||(min!=D3DTEXF_POINT&&min!=D3DTEXF_LINEAR)||FAILED(d->GetSamplerState(stage,D3DSAMP_MIPFILTER,&mip))||mip!=D3DTEXF_NONE)return S_FALSE;
                IDirect3DBaseTexture9* base=nullptr;const HRESULT textureHr=d->GetTexture(stage,&base);if(FAILED(textureHr)||!base){release(base);return S_FALSE;}
                D3DSURFACE_DESC desc{};const bool texture=base->GetType()==D3DRTYPE_TEXTURE&&SUCCEEDED(static_cast<IDirect3DTexture9*>(base)->GetLevelDesc(0,&desc));release(base);
                if(!texture||(desc.Format!=D3DFMT_A8R8G8B8&&desc.Format!=D3DFMT_X8R8G8B8&&desc.Format!=D3DFMT_DXT1&&desc.Format!=D3DFMT_DXT3&&desc.Format!=D3DFMT_DXT5))return S_FALSE;
            }
        }
        DWORD fog=0,coordinates=0,blend=0,src=0,dst=0;
        d->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend);if(blend){d->GetRenderState(D3DRS_SRCBLEND,&src);d->GetRenderState(D3DRS_DESTBLEND,&dst);}
        if(!mrt&&blend&&src==D3DBLEND_ZERO&&dst==D3DBLEND_INVSRCALPHA){
            DWORD op=0,writes=0,srgb=0;
            if(SUCCEEDED(d->GetRenderState(D3DRS_BLENDOP,&op))&&op==D3DBLENDOP_ADD&&
               SUCCEEDED(d->GetRenderState(D3DRS_COLORWRITEENABLE,&writes))&&SUCCEEDED(d->GetRenderState(D3DRS_SRGBWRITEENABLE,&srgb))){
                // Cdst*(1-alpha) is independent of the color transfer. Keep
                // the legacy factor layer's destination-alpha policy as well.
                struct Restore{IDirect3DDevice9* d;DWORD writes,srgb;~Restore(){d->SetRenderState(D3DRS_COLORWRITEENABLE,writes);d->SetRenderState(D3DRS_SRGBWRITEENABLE,srgb);}}restore{d,writes,srgb};
                HRESULT hr=d->SetRenderState(D3DRS_COLORWRITEENABLE,writes&~D3DCOLORWRITEENABLE_ALPHA);
                if(SUCCEEDED(hr))hr=d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
                if(SUCCEEDED(hr)){++attenuationDirect;hr=submit();}return hr;
            }
        }
        DWORD firstColor=0,firstAlpha=0,specular=0,blendOp=0;
        if(FAILED(d->GetTextureStageState(0,D3DTSS_COLOROP,&firstColor)))return E_FAIL;
        // The native sky uses diffuse-only additive draws with both operations
        // disabled. Keep other fixed-function combinations in the compatibility path.
        const bool diffuseOnly=firstColor==D3DTOP_DISABLE;
        if(diffuseOnly&&(!blend||src!=D3DBLEND_SRCALPHA||dst!=D3DBLEND_ONE||
           FAILED(d->GetTextureStageState(0,D3DTSS_ALPHAOP,&firstAlpha))||firstAlpha!=D3DTOP_DISABLE||!disabledStage(d,1)||
           FAILED(d->GetRenderState(D3DRS_BLENDOP,&blendOp))||blendOp!=D3DBLENDOP_ADD||
           FAILED(d->GetRenderState(D3DRS_SPECULARENABLE,&specular))||specular))return fallback();
        bool nativeAdditive=false;
        float second[8]={0,1,0,0,0,1,0,0};const bool multi=!diffuseOnly&&!disabledStage(d,1);
        if(mrt&&!multi)return S_FALSE;
        if(multi){
            DWORD result=0;
            if(!disabledStage(d,2)||FAILED(d->GetTextureStageState(0,D3DTSS_RESULTARG,&result))||result!=D3DTA_CURRENT||
               FAILED(d->GetTextureStageState(1,D3DTSS_RESULTARG,&result))||result!=D3DTA_CURRENT||
               FAILED(d->GetTextureStageState(1,D3DTSS_TEXCOORDINDEX,&coordinates))||coordinates>1||
               !directTextureCoordinates(d,1))return fallback();
            second[3]=float(coordinates);
            for(unsigned channel=0;channel<2;++channel){
                float* flags=second+channel*4;flags[0]=flags[1]=flags[2]=0;DWORD op=0;
                if(FAILED(d->GetTextureStageState(1,channel?D3DTSS_ALPHAOP:D3DTSS_COLOROP,&op)))return E_FAIL;
                if(mrt&&channel&&op==D3DTOP_SELECTARG2)return S_FALSE;
                if(op!=D3DTOP_SELECTARG1&&op!=D3DTOP_SELECTARG2&&op!=D3DTOP_MODULATE)return fallback();
                for(unsigned arg=1;arg<=2;++arg){
                    if((op==D3DTOP_SELECTARG1&&arg==2)||(op==D3DTOP_SELECTARG2&&arg==1))continue;
                    DWORD value=0;const auto state=channel?(arg==1?D3DTSS_ALPHAARG1:D3DTSS_ALPHAARG2):(arg==1?D3DTSS_COLORARG1:D3DTSS_COLORARG2);
                    if(FAILED(d->GetTextureStageState(1,state,&value)))return E_FAIL;
                    const unsigned index=value==D3DTA_TEXTURE?0:value==D3DTA_CURRENT?1:value==D3DTA_DIFFUSE?2:3;
                    if(index==3||flags[index])return fallback();flags[index]=1;
                }
            }
            if(second[0]||second[4]){IDirect3DBaseTexture9* texture=nullptr;const HRESULT hr=d->GetTexture(1,&texture);const bool present=texture&&texture->GetType()==D3DRTYPE_TEXTURE;release(texture);if(FAILED(hr))return hr;if(!present)return fallback();}
        }
        if(FAILED(d->GetRenderState(D3DRS_FOGENABLE,&fog))||fog||
           FAILED(d->GetTextureStageState(0,D3DTSS_TEXCOORDINDEX,&coordinates))||coordinates||
           !directTextureCoordinates(d,0,!diffuseOnly&&!multi&&!mrt&&blend&&src==D3DBLEND_SRCALPHA&&dst==D3DBLEND_ONE,&nativeAdditive))return fallback();
        float flags[4]{};
        for(unsigned channel=0;!diffuseOnly&&channel<2;++channel){
            DWORD op=0;if(FAILED(d->GetTextureStageState(0,channel?D3DTSS_ALPHAOP:D3DTSS_COLOROP,&op)))return E_FAIL;
            if(mrt&&channel&&op==D3DTOP_SELECTARG2)return S_FALSE;
            if(channel&&op==D3DTOP_DISABLE){flags[3]=1;continue;}
            if(op!=D3DTOP_SELECTARG1&&op!=D3DTOP_SELECTARG2&&op!=D3DTOP_MODULATE)return fallback();
            for(unsigned arg=1;arg<=2;++arg){
                if((op==D3DTOP_SELECTARG1&&arg==2)||(op==D3DTOP_SELECTARG2&&arg==1))continue;
                DWORD value=0;const auto state=channel?(arg==1?D3DTSS_ALPHAARG1:D3DTSS_ALPHAARG2):(arg==1?D3DTSS_COLORARG1:D3DTSS_COLORARG2);
                if(FAILED(d->GetTextureStageState(0,state,&value)))return E_FAIL;
                if(value==D3DTA_TEXTURE){if(flags[channel*2])return fallback();flags[channel*2]=1;}
                else if(value==D3DTA_DIFFUSE||value==D3DTA_CURRENT){if(flags[channel*2+1])return fallback();flags[channel*2+1]=1;}
                else return fallback();
            }
        }
        if(nativeAdditive&&(flags[0]!=1||flags[1]!=1||flags[2]!=1||flags[3]!=1))return fallback();
        // One retained source read proves presence and the native UNORM range.
        if(flags[0]||flags[2]){
            IDirect3DBaseTexture9* texture=nullptr;D3DSURFACE_DESC desc{};const HRESULT hr=d->GetTexture(0,&texture);
            const bool present=texture!=nullptr;
            const bool unorm=!nativeAdditive||(SUCCEEDED(hr)&&present&&texture->GetType()==D3DRTYPE_TEXTURE&&
                SUCCEEDED(static_cast<IDirect3DTexture9*>(texture)->GetLevelDesc(0,&desc))&&
                (desc.Format==D3DFMT_A8R8G8B8||desc.Format==D3DFMT_X8R8G8B8||desc.Format==D3DFMT_DXT1||desc.Format==D3DFMT_DXT3||desc.Format==D3DFMT_DXT5));
            release(texture);if(FAILED(hr))return hr;if(!present||!unorm)return fallback();
        }
        if(blend&&(src==D3DBLEND_DESTCOLOR||src==D3DBLEND_ZERO))return fallback();
        auto*& shader=mrt?multiFixedSky:nativeAdditive?fixedAdditive:diffuseOnly?diffuseUi:multi?multiFixedUi:fixedUi;
        if(!shader){const HRESULT hr=d->CreatePixelShader(reinterpret_cast<const DWORD*>(mrt?kHdrFixedSky2:nativeAdditive?kHdrFixedAdditive:diffuseOnly?kHdrUiPs3:multi?kHdrFixedUi2:kHdrFixedUi),&shader);if(FAILED(hr))return fallback();}
        const unsigned registers=nativeAdditive?1:multi?4:2;
        float constants[16];DWORD srgb=0;HRESULT hr=d->GetPixelShaderConstantF(0,constants,registers);if(FAILED(hr))return hr;
        hr=d->GetRenderState(D3DRS_SRGBWRITEENABLE,&srgb);if(FAILED(hr))return hr;
        const D3DRENDERSTATETYPE alphaStates[]={D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_SRCBLENDALPHA,D3DRS_DESTBLENDALPHA,D3DRS_BLENDOPALPHA};
        const bool preserveAlpha=diffuseOnly||nativeAdditive;DWORD alpha[4]{};if(preserveAlpha)for(unsigned i=0;i<4;++i){hr=d->GetRenderState(alphaStates[i],alpha+i);if(FAILED(hr))return hr;}
        struct Restore{IDirect3DDevice9* d;float* constants;unsigned registers;DWORD srgb,src;bool screen,diffuse;const D3DRENDERSTATETYPE* alphaStates;DWORD* alpha;~Restore(){d->SetPixelShader(nullptr);d->SetPixelShaderConstantF(0,constants,registers);d->SetRenderState(D3DRS_SRGBWRITEENABLE,srgb);if(screen)d->SetRenderState(D3DRS_SRCBLEND,src);if(diffuse)for(unsigned i=0;i<4;++i)d->SetRenderState(alphaStates[i],alpha[i]);}}restore{d,constants,registers,srgb,src,blend&&src==D3DBLEND_INVDESTCOLOR&&dst==D3DBLEND_ONE,preserveAlpha,alphaStates,alpha};
        const float scale[]={uiWhite/80,uiWhite/80,uiWhite/80,blend&&src==D3DBLEND_ONE&&dst==D3DBLEND_INVSRCALPHA?1.f:0.f};
        hr=d->SetPixelShader(shader);if(SUCCEEDED(hr))hr=d->SetPixelShaderConstantF(0,scale,1);
        if(SUCCEEDED(hr)&&!nativeAdditive)hr=d->SetPixelShaderConstantF(1,flags,1);
        if(SUCCEEDED(hr)&&multi)hr=d->SetPixelShaderConstantF(2,second,2);
        if(SUCCEEDED(hr))hr=d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
        if(SUCCEEDED(hr)&&restore.screen){++screenDirect;hr=d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE);}
        // Match the compatibility composite's additive destination alpha.
        if(preserveAlpha){const DWORD values[]={TRUE,D3DBLEND_ONE,D3DBLEND_ONE,D3DBLENDOP_ADD};for(unsigned i=0;SUCCEEDED(hr)&&i<4;++i)hr=d->SetRenderState(alphaStates[i],values[i]);}
        if(SUCCEEDED(hr)){
            if(mrt){HRESULT result=S_FALSE;if(skyCapture->draw(d,sky,false,submit,result)){*captured=true;++skyMrtDraws;return result;}return S_FALSE;}
            if(multi)++multiTextureDirect;if(nativeAdditive)++fixedAdditiveDirect;hr=submit();
        }return hr;
    }
    template<class Draw> HRESULT draw(IDirect3DDevice9* d,float uiWhite,Draw submit,unsigned particleFamily=0,NorthlightStateBlockPool* pool=nullptr){
        // Preserve depth/stencil/query side effects, but no color needs transfer.
        DWORD writes=0;if(SUCCEEDED(d->GetRenderState(D3DRS_COLORWRITEENABLE,&writes))&&!writes)return submit();
        IDirect3DPixelShader9* original=nullptr;
        if(FAILED(d->GetPixelShader(&original)))return E_FAIL;
        if(!original)return fixedDraw(d,uiWhite,submit,pool);
        DWORD src=0,dst=0,srgb=0,blend=0;d->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend);
        if(blend){d->GetRenderState(D3DRS_SRCBLEND,&src);d->GetRenderState(D3DRS_DESTBLEND,&dst);}
        const HRESULT srgbStatus=d->GetRenderState(D3DRS_SRGBWRITEENABLE,&srgb);if(FAILED(srgbStatus)){original->Release();return srgbStatus;}
        const bool screen=blend&&src==D3DBLEND_INVDESTCOLOR&&dst==D3DBLEND_ONE;
        struct Restore {IDirect3DDevice9* d;IDirect3DPixelShader9* ps;DWORD srgb,src;bool screen;~Restore(){d->SetPixelShader(ps);if(srgb)d->SetRenderState(D3DRS_SRGBWRITEENABLE,srgb);if(screen)d->SetRenderState(D3DRS_SRCBLEND,src);ps->Release();}} restore{d,original,srgb,src,screen};
        // The compatibility layer converted screen sources to linear additive
        // light. A patched shader can do the same on its original geometry.
        const unsigned variant=blend&&src==D3DBLEND_ONE&&dst==D3DBLEND_INVSRCALPHA?1:
            (blend&&((src==D3DBLEND_DESTCOLOR&&dst==D3DBLEND_ZERO)||(src==D3DBLEND_ZERO&&dst==D3DBLEND_SRCCOLOR))?2:0);
        IDirect3DPixelShader9* shader=nullptr;DWORD op=0;
        if((particleFamily==NorthlightM2Family::kParticle||particleFamily==NorthlightM2Family::kParticleUnlit)&&
           uiWhite==80&&blend&&(src==D3DBLEND_ONE||src==D3DBLEND_SRCALPHA)&&dst==D3DBLEND_ONE&&
           SUCCEEDED(d->GetRenderState(D3DRS_BLENDOP,&op))&&op==D3DBLENDOP_ADD)
            shader=replacement(d,original,80,3);
        if(!shader)shader=replacement(d,original,uiWhite,variant);
        if(!shader)return legacyLayer(d,uiWhite,submit,pool);
        HRESULT hr=d->SetPixelShader(shader);if(SUCCEEDED(hr)&&srgb)hr=d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
        if(SUCCEEDED(hr)&&screen){++screenDirect;hr=d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE);}
        if(SUCCEEDED(hr))hr=submit();return hr;
    }
    HRESULT convertWorld(IDirect3DDevice9* d,IDirect3DSurface9* source,IDirect3DSurface9* target,IDirect3DTexture9* copy,IDirect3DSurface9* copySurface,
                         unsigned width,unsigned height,float paperWhite,float maxNits,NorthlightStateBlockPool* pool=nullptr,float contrast=1,float exposure=0){
        if(!world){HRESULT hr=d->CreatePixelShader(reinterpret_cast<const DWORD*>(kHdrWorld),&world);if(FAILED(hr))return hr;}
        SavedState saved(d,pool);if(!saved.ok)return E_FAIL;
        HRESULT hr=d->SetTexture(0,nullptr);if(SUCCEEDED(hr))hr=d->StretchRect(source,nullptr,copySurface,nullptr,D3DTEXF_NONE);if(FAILED(hr))return hr;
        hr=quadState(d,target,width,height);if(FAILED(hr))return hr;
        float scale[]={paperWhite/80,paperWhite/80,paperWhite/80,maxNits/80},grade[]={contrast,std::exp2(exposure),0,0};d->SetPixelShaderConstantF(0,scale,1);d->SetPixelShaderConstantF(1,grade,1);d->SetPixelShader(world);d->SetTexture(0,copy);
        return drawQuad(d,width,height);
    }
};
}
