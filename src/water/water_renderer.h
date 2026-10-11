#pragma once
// Include after host SavedState/drop/logf. All borrowed inputs remain caller-owned.
#include <d3d9.h>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <chrono>
#include <iterator>
#include <unordered_map>
#include <vector>
#include "water_shader_identities.h"
#include "water_shader_patch.h"
#include "water_compiled_shaders.h"
#include "diagnostics_switch.h"
struct NorthlightWaterContext {
    float inverseView[16]={};
    float projection[3]={};
    float nearZ=0,farZ=0,minZ=0,maxZ=1;
    float sunDirection[3]={},sunColor[3]={};
    float moonDirection[3]={},moonColor[3]={};
    float skyColor[3]={};
    float seconds=0;
    // Source 0 sun, 1 moon. Near/far cascades centered on the current camera.
    // Null pair disables that source's shadow sampling, never aliases another source.
    IDirect3DTexture9* sourceShadows[2][2]={};
    float sourceShadowMatrices[2][2][16]={};
    float sourceShadowDirection[2][3]={};
    float shadowTexel=1.f/1024;
    // 0.3.209 (task 29): the per-source actor shadow maps (moving casters near the player; shadows/actor_shadow_map.h), null/invalid = not sampled.
    IDirect3DTexture9* actorShadows[2]={};
    float actorShadowMatrices[2][16]={};
    bool actorShadowValid[2]={};
};
class NorthlightWaterRenderer {
    IDirect3DDevice9* d;
    const NorthlightWaterShaderIdentity* identities;std::size_t identityCount;
    NorthlightStateBlockPool stateBlocks;
    std::unordered_map<IDirect3DVertexShader9*,IDirect3DVertexShader9*> vertex;
    std::unordered_map<IDirect3DPixelShader9*,IDirect3DPixelShader9*> pixel;
    IDirect3DTexture9 *mask=nullptr,*color=nullptr,*reflection=nullptr;
    IDirect3DSurface9 *maskSurface=nullptr,*colorSurface=nullptr,*reflectionSurface=nullptr,*depth=nullptr;
    IDirect3DPixelShader9 *reflectionPS=nullptr,*compositePS=nullptr;
    UINT width=0,height=0;D3DFORMAT colorFormat=D3DFMT_UNKNOWN;
    unsigned patchedShaders=0,patchRejects=0;double patchMicroseconds=0;
    bool failed=false,captured=false,maskMayBeBound=true;float maskMinZ=0,maskMaxZ=1;unsigned captures=0,frames=0,maskScans=0,clears=0,readFailures=0;std::uint64_t scanEpoch=~std::uint64_t(0);
    bool check(HRESULT hr,const char* where){if(SUCCEEDED(hr))return true;if(!failed)logf("WATER disabled: %s HRESULT=0x%08lx",where,(unsigned long)hr);failed=true;return false;}
    bool target(UINT w,UINT h,D3DFORMAT fmt,IDirect3DTexture9*& tex,IDirect3DSurface9*& surface){
        return check(d->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,fmt,D3DPOOL_DEFAULT,&tex,nullptr),"create target")&&check(tex->GetSurfaceLevel(0,&surface),"get surface");
    }
    bool maskResources(UINT w,UINT h){
        if(failed||!w||!h)return false;
        if(w!=width||h!=height){reset();width=w;height=h;}
        if(mask)return true;
        return target(w,h,D3DFMT_G32R32F,mask,maskSurface)&&check(d->CreateDepthStencilSurface(w,h,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,&depth,nullptr),"mask depth");
    }
    // The mask variant of a recognised original, built from the bytes the game just
    // supplied (one-time, at shader creation; nothing per frame). Only an exact
    // generated identity is patched, and the result must hash to the recorded
    // variant (byte-identical to the 0.3.160 embedded shader) or that one shader
    // is skipped; water stays usable for the others.
    const DWORD* maskShader(std::uint64_t hash,bool isVertex,const DWORD* words,std::size_t count,std::vector<NorthlightWaterShaderPatch::Word>& patched){
        const NorthlightWaterShaderIdentity* id=nullptr;
        for(std::size_t i=0;i<identityCount;++i)if(identities[i].vertex==isVertex&&identities[i].hash==hash){id=&identities[i];break;}
        if(!id||!words||!count)return nullptr;
        const auto start=std::chrono::steady_clock::now();
        static_assert(sizeof(DWORD)==sizeof(NorthlightWaterShaderPatch::Word),"shader token size");
        const char* reason=NorthlightWaterShaderPatch::patch(reinterpret_cast<const NorthlightWaterShaderPatch::Word*>(words),count,patched);
        if(!reason&&(patched.size()!=id->patchedWords||NorthlightWaterShaderPatch::fnv1a64(patched.data(),patched.size())!=id->patchedHash))reason="patched hash mismatch";
        const double us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();
        patchMicroseconds+=us;
        if(reason){if(patchRejects++<8)logf("WATER mask patch skipped %s hash=%016llx: %s",isVertex?"vertex":"pixel",(unsigned long long)hash,reason);return nullptr;}
        ++patchedShaders;
        if(NorthlightDiagnostics::enabled())logf("WATER mask patch time %s hash=%016llx words=%u us=%.1f total_us=%.1f shaders=%u (one-time, at shader creation)",isVertex?"vertex":"pixel",(unsigned long long)hash,unsigned(patched.size()),us,patchMicroseconds,patchedShaders);
        return reinterpret_cast<const DWORD*>(patched.data());
    }
    HRESULT quad(){struct V{float x,y,z,w,u,v;} v[]={{-.5f,-.5f,0,1,0,0},{float(width)-.5f,-.5f,0,1,1,0},{-.5f,float(height)-.5f,0,1,0,1},{float(width)-.5f,float(height)-.5f,0,1,1,1}};D3DVIEWPORT9 vp={0,0,width,height,0,1};d->SetViewport(&vp);return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(V));}
public:
    // identities: the generated table; tests inject their own.
    explicit NorthlightWaterRenderer(IDirect3DDevice9* device,const NorthlightWaterShaderIdentity* table=kWaterShaderIdentities,std::size_t tableCount=std::size(kWaterShaderIdentities))
        :d(device),identities(table),identityCount(tableCount),stateBlocks(device){}
    ~NorthlightWaterRenderer(){reset();drop(reflectionPS);drop(compositePS);for(auto& p:vertex)drop(p.second);for(auto& p:pixel)drop(p.second);}
    NorthlightWaterRenderer(const NorthlightWaterRenderer&)=delete;
    void releaseStateCache(){stateBlocks.clear();}
    void reset(){stateBlocks.clear();drop(maskSurface);drop(colorSurface);drop(reflectionSurface);drop(depth);drop(mask);drop(color);drop(reflection);width=height=0;colorFormat=D3DFMT_UNKNOWN;captured=false;captures=0;}
    void endFrame(){if(!captured||failed)stateBlocks.clear();captured=false;captures=maskScans=clears=readFailures=0;}
    // Explicit retry only after host frame cleanup; no allocation or hot-path work.
    void recover(){if(!failed)return;reset();failed=false;logf("WATER explicit recovery requested");}
    bool hasCapturedWater()const{return captured&&!failed;}
    IDirect3DTexture9* maskTextureForDepth(float minZ,float maxZ){
        IDirect3DTexture9* out=captured&&!failed&&mask&&std::isfinite(minZ)&&std::isfinite(maxZ)&&maxZ>minZ&&
            std::fabs(minZ-maskMinZ)<.0001f&&std::fabs(maxZ-maskMaxZ)<.0001f?mask:nullptr;
        if(out)maskMayBeBound=true;
        return out;
    }
    // Sampled diagnostics: mask draws and host-stage scans this frame.
    unsigned frameCaptures()const{return captures;}
    unsigned frameMaskScans()const{return maskScans;}
    unsigned frameClears()const{return clears;}
    unsigned frameReadFailures()const{return readFailures;}
    bool recognizesVertex(IDirect3DVertexShader9* shader)const{return !failed&&vertex.find(shader)!=vertex.end();}
    // recognizesVertex() split for the host's per-shader tag: hasVertex() only
    // changes in registerVertex(), usable() is the live failure state.
    bool hasVertex(IDirect3DVertexShader9* shader)const{return vertex.find(shader)!=vertex.end();}
    bool usable()const{return !failed;}
    // words/count: the original bytecode (GetFunction), already fetched for the hash.
    void registerVertex(IDirect3DVertexShader9* original,std::uint64_t hash,const DWORD* words,std::size_t count){
        auto old=vertex.find(original);if(old!=vertex.end()){drop(old->second);vertex.erase(old);}
        if(failed)return;
        std::vector<NorthlightWaterShaderPatch::Word> patched;const DWORD* code=maskShader(hash,true,words,count,patched);if(!code)return;
        IDirect3DVertexShader9* shader=nullptr;if(check(d->CreateVertexShader(code,&shader),"water vertex mask shader")){vertex.emplace(original,shader);logf("WATER registered vertex hash=%016llx",(unsigned long long)hash);}
    }
    void registerPixel(IDirect3DPixelShader9* original,std::uint64_t hash,const DWORD* words,std::size_t count){
        auto old=pixel.find(original);if(old!=pixel.end()){drop(old->second);pixel.erase(old);}
        if(failed)return;
        std::vector<NorthlightWaterShaderPatch::Word> patched;const DWORD* code=maskShader(hash,false,words,count,patched);if(!code)return;
        IDirect3DPixelShader9* shader=nullptr;if(check(d->CreatePixelShader(code,&shader),"water pixel mask shader")){pixel.emplace(original,shader);logf("WATER registered pixel hash=%016llx",(unsigned long long)hash);}
    }
    // Exactly the state the mask pass changes, restored through the mirror-aware
    // device: targets, depth, the nine render states, shaders, mask-aliased
    // stages, the scissor/viewport reset by target 0 and, for user-pointer
    // draws, stream 0 (and indices). The equivalent of 0.3.140's D3DSBT_ALL
    // Apply, without invalidating the device mirror (test_water_mask_state.cpp).
    enum UserPointer : unsigned {NoUserPointer,UserVertices,UserVerticesAndIndices};
    struct MaskRestore {
        IDirect3DDevice9* d;IDirect3DVertexShader9* vs;IDirect3DPixelShader9* ps;IDirect3DSurface9* depth;
        IDirect3DSurface9* targets[4]={};RECT scissor={};D3DVIEWPORT9 viewport={};bool active=false;
        static constexpr D3DRENDERSTATETYPE States[9]={D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ZFUNC,D3DRS_ALPHABLENDENABLE,D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_COLORWRITEENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_FOGENABLE,D3DRS_STENCILENABLE};
        DWORD states[9]={};
        IDirect3DBaseTexture9* mask=nullptr;unsigned short maskStages=0;
        unsigned up=NoUserPointer;IDirect3DVertexBuffer9* stream=nullptr;UINT offset=0,stride=0;IDirect3DIndexBuffer9* indices=nullptr;
        MaskRestore(IDirect3DDevice9* device,IDirect3DVertexShader9* v,IDirect3DPixelShader9* p,IDirect3DSurface9* z):d(device),vs(v),ps(p),depth(z){}
        MaskRestore(const MaskRestore&)=delete;MaskRestore& operator=(const MaskRestore&)=delete;
        bool read(const D3DVIEWPORT9& current,unsigned userPointer){
            viewport=current;up=userPointer;
            if(FAILED(d->GetRenderTarget(0,&targets[0]))||!targets[0])return false;
            // Unbound targets answer D3DERR_NOTFOUND; any other failure would restore a wrong binding.
            for(unsigned i=1;i<4;++i){const HRESULT hr=d->GetRenderTarget(i,&targets[i]);if(FAILED(hr)&&hr!=D3DERR_NOTFOUND)return false;}
            if(FAILED(d->GetScissorRect(&scissor)))return false;
            for(unsigned i=0;i<9;++i)if(FAILED(d->GetRenderState(States[i],&states[i])))return false;
            if(up!=NoUserPointer&&FAILED(d->GetStreamSource(0,&stream,&offset,&stride)))return false;
            if(up==UserVerticesAndIndices&&FAILED(d->GetIndices(&indices)))return false;
            return active=true;
        }
        ~MaskRestore(){
            if(active){
                d->SetDepthStencilSurface(nullptr);
                for(unsigned i=1;i<4;++i)d->SetRenderTarget(i,nullptr);
                d->SetRenderTarget(0,targets[0]);
                for(unsigned i=1;i<4;++i)d->SetRenderTarget(i,targets[i]);
                d->SetDepthStencilSurface(depth);
                for(unsigned i=0;i<16;++i)if(maskStages&(1u<<i))d->SetTexture(i,mask);
                for(unsigned i=0;i<9;++i)d->SetRenderState(States[i],states[i]);
                d->SetVertexShader(vs);d->SetPixelShader(ps);
                if(up!=NoUserPointer)d->SetStreamSource(0,stream,offset,stride);
                if(up==UserVerticesAndIndices)d->SetIndices(indices);
                d->SetScissorRect(&scissor);d->SetViewport(&viewport);
            }
            if(indices)indices->Release();
            if(stream)stream->Release();
            for(auto* target:targets)if(target)target->Release();
        }
    };
    // Caller passes the bound shaders, the verified bound depth surface and the
    // viewport already read and validated as the full main target (captureWater:
    // only Get* calls happen between those reads and this capture).
    // stateEpoch: the device mirror's invalidation count. Every state-block
    // Apply (a game block may hold a leaked mask binding), Reset and raw scope
    // invalidates, so any change rescans the stages before the next mask draw.
    // The extension requires DXVK, whose getters always answer: a failed read
    // skips the mask draw without touching state.
    template<class Draw> void capture(IDirect3DVertexShader9* vs,IDirect3DPixelShader9* ps,UINT mainWidth,UINT mainHeight,IDirect3DSurface9* mainDepth,const D3DVIEWPORT9& vp,unsigned userPointer,std::uint64_t stateEpoch,Draw draw){
        if(failed)return;auto vi=vertex.find(vs);auto pi=pixel.find(ps);if(vi==vertex.end()||pi==pixel.end())return;
        if(vp.X||vp.Y||vp.Width!=mainWidth||vp.Height!=mainHeight)return;
        if(captured&&(vp.MinZ!=maskMinZ||vp.MaxZ!=maskMaxZ))return;
        if(stateEpoch!=scanEpoch){scanEpoch=stateEpoch;maskMayBeBound=true;}
        MaskRestore save(d,vs,ps,mainDepth);if(!save.read(vp,userPointer)){++readFailures;return;}
        if(maskResources(mainWidth,mainHeight))drawMask(vi->second,pi->second,vp,save,draw);
    }
    // The 0.3.140 mask pass; `save` restores everything it changes.
    template<class Draw> void drawMask(IDirect3DVertexShader9* maskVS,IDirect3DPixelShader9* maskPS,const D3DVIEWPORT9& vp,MaskRestore& save,Draw draw){
        d->SetDepthStencilSurface(nullptr);for(unsigned i=1;i<4;++i)d->SetRenderTarget(i,nullptr);
        // s2 may still contain last frame's mask in host state. Avoid RT/sampler
        // alias. Only the extension hands the mask out (maskTextureForDepth,
        // render); a scan that read every stage and found none clears the flag.
        // Restores put the mask back, so a hit keeps scanning every draw.
        if(maskMayBeBound){bool complete=true;unsigned short found=0;
            for(unsigned i=0;i<16;++i){IDirect3DBaseTexture9* texture=nullptr;const HRESULT hr=d->GetTexture(i,&texture);complete=complete&&SUCCEEDED(hr);
                if(SUCCEEDED(hr)&&texture==mask){d->SetTexture(i,nullptr);found|=1u<<i;}drop(texture);}
            save.mask=mask;save.maskStages=found;
            maskMayBeBound=found||!complete;++maskScans;}
        if(!check(d->SetRenderTarget(0,maskSurface),"mask target")||!check(d->SetDepthStencilSurface(depth),"mask depth bind"))return;
        d->SetViewport(&vp);
        if(!captured){if(!check(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0),"clear mask"))return;maskMinZ=vp.MinZ;maskMaxZ=vp.MaxZ;++clears;}
        d->SetRenderState(D3DRS_ZENABLE,TRUE);d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);
        d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);d->SetRenderState(D3DRS_FOGENABLE,FALSE);d->SetRenderState(D3DRS_STENCILENABLE,FALSE);
        // Keep original cull, alpha test, texture bindings, constants and geometry.
        if(!check(d->SetVertexShader(maskVS),"mask VS")||!check(d->SetPixelShader(maskPS),"mask PS")||!check(draw(),"mask draw"))return;
        captured=true;++captures;
    }
    bool render(IDirect3DSurface9* targetSurface,IDirect3DTexture9* opaqueDepth,UINT w,UINT h,D3DFORMAT format,const NorthlightWaterContext& context){
        if(failed||!captured||!targetSurface||!opaqueDepth||w!=width||h!=height||context.nearZ<=0||context.farZ<=context.nearZ||context.maxZ<=context.minZ||std::fabs(context.projection[0])<.001f||std::fabs(context.projection[1])<.001f||std::fabs(context.projection[2])<.5f||std::fabs(context.minZ-maskMinZ)>.0001f||std::fabs(context.maxZ-maskMaxZ)>.0001f)return false;
        SavedState save(d,&stateBlocks);if(!save.ok)return false;
        if(!reflectionPS&&!check(d->CreatePixelShader(kWaterReflectionShader,&reflectionPS),"reflection shader"))return false;
        if(!compositePS&&!check(d->CreatePixelShader(kWaterCompositeShader,&compositePS),"water composite shader"))return false;
        if(colorFormat!=format){drop(colorSurface);drop(color);colorFormat=format;}
        if(!color&&!target(w,h,format,color,colorSurface))return false;
        if(!reflection&&!target(w,h,D3DFMT_A16B16G16R16F,reflection,reflectionSurface))return false;
        d->SetDepthStencilSurface(nullptr);for(unsigned i=1;i<4;++i)d->SetRenderTarget(i,nullptr);
        for(unsigned i=0;i<16;++i)d->SetTexture(i,nullptr);
        if(!check(d->StretchRect(targetSurface,nullptr,colorSurface,nullptr,D3DTEXF_NONE),"copy scene"))return false;
        d->SetVertexShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetStreamSourceFreq(0,1);
        const struct{D3DRENDERSTATETYPE s;DWORD v;} states[]={{D3DRS_ZENABLE,FALSE},{D3DRS_ZWRITEENABLE,FALSE},{D3DRS_ALPHATESTENABLE,FALSE},{D3DRS_ALPHABLENDENABLE,FALSE},{D3DRS_SEPARATEALPHABLENDENABLE,FALSE},{D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_COLORWRITEENABLE,15},{D3DRS_FOGENABLE,FALSE},{D3DRS_STENCILENABLE,FALSE},{D3DRS_SCISSORTESTENABLE,FALSE},{D3DRS_CLIPPLANEENABLE,0},{D3DRS_SRGBWRITEENABLE,FALSE},{D3DRS_FILLMODE,D3DFILL_SOLID},{D3DRS_MULTISAMPLEMASK,0xffffffff},{D3DRS_WRAP0,0}};
        for(auto& state:states)d->SetRenderState(state.s,state.v);
        d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0);d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);
        float c[38][4]={};c[0][0]=1.f/w;c[0][1]=1.f/h;c[0][2]=context.nearZ;c[0][3]=context.farZ;
        memcpy(c[1],context.projection,12);c[1][3]=context.minZ;c[2][0]=1/(context.maxZ-context.minZ);c[2][1]=context.seconds;c[2][2]=.85f;c[2][3]=.45f;
        memcpy(c[3],context.inverseView,64);memcpy(c[7],context.sunDirection,12);memcpy(c[8],context.sunColor,12);memcpy(c[9],context.moonDirection,12);memcpy(c[10],context.moonColor,12);memcpy(c[11],context.skyColor,12);
        for(unsigned source=0;source<2;++source){float dot=0;const float* wanted=source?context.moonDirection:context.sunDirection;for(unsigned k=0;k<3;++k)dot+=wanted[k]*context.sourceShadowDirection[source][k];c[28][source]=context.sourceShadows[source][0]&&context.sourceShadows[source][1]&&dot>.98f?1.f:0.f;for(unsigned cascade=0;cascade<2;++cascade)memcpy(c[12+source*8+cascade*4],context.sourceShadowMatrices[source][cascade],64);}
        c[28][2]=context.shadowTexel;
        for(unsigned source=0;source<2;++source){const bool actor=context.actorShadows[source]&&context.actorShadowValid[source]&&c[28][source]>0.f; /* the cascades' direction check (dot>.98) */c[37][source]=actor?1.f:0.f;if(actor)memcpy(c[29+source*4],context.actorShadowMatrices[source],64);} /* 0.3.209 (task 29): ActorMatrix c29..c32, MoonActorMatrix c33..c36, ActorShadowInfo c37 (x sun, y moon valid, z texel) */
        c[37][2]=1.f/1024;
        if(!check(d->SetPixelShaderConstantF(0,c[0],38),"water constants"))return false;
        maskMayBeBound=true;
        IDirect3DTexture9* textures[]={color,opaqueDepth,mask,nullptr,context.sourceShadows[0][0],context.sourceShadows[0][1],context.sourceShadows[1][0],context.sourceShadows[1][1],context.actorShadows[0],context.actorShadows[1]};
        for(unsigned i=0;i<10;++i){d->SetTexture(i,textures[i]);d->SetSamplerState(i,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(i,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);d->SetSamplerState(i,D3DSAMP_MINFILTER,i==0?D3DTEXF_LINEAR:D3DTEXF_POINT);d->SetSamplerState(i,D3DSAMP_MAGFILTER,i==0?D3DTEXF_LINEAR:D3DTEXF_POINT);d->SetSamplerState(i,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(i,D3DSAMP_SRGBTEXTURE,FALSE);}
        if(!check(d->SetRenderTarget(0,reflectionSurface),"reflection target")||!check(d->SetPixelShader(reflectionPS),"reflection bind")||!check(quad(),"reflection draw"))return false;
        if(!check(d->SetRenderTarget(0,targetSurface),"composite target")||!check(d->SetTexture(3,reflection),"reflection input")||!check(d->SetPixelShader(compositePS),"composite bind")||!check(quad(),"composite draw"))return false;
        if(++frames==1||(frames%600==0&&NorthlightDiagnostics::enabled()))logf("WATER frame=%u draws=%u method=screen-space-reflection skyFallback=1 exactLiquidMask=1 sunShadow=%d moonShadow=%d",frames,captures,int(c[28][0]),int(c[28][1]));
        return true;
    }
};
