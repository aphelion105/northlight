#pragma once
// Include after host SavedState/drop/logf. All borrowed inputs remain caller-owned.
// The liquid mask (mask.r = view distance of the water surface, mask.g > .003 = water) that AO, GI, fog, the local lights and the celestial discs read.
// 0.3.214 (water MRT): the mask is normally written by the game's OWN water draw through render target 1 (beginMrt/endMrt: the mask variants of the draw's two
// shaders, no render target switch, no second draw) and decoded once per frame by resolveMask(); the 0.3.140 second-draw pass (capture) stays as the fallback
// for any draw or frame the MRT conditions do not hold for. Shader safety scheme (the repository holds no game bytes, and the identity table records the hash of
// the MASK variant only): registerPixel() builds the MRT variant from the bytes the game supplied ONLY after the mask variant of the same bytes matched its
// recorded hash (so the bytes are exactly the known shader), and the MRT variant must then pass verifyMrt(): that proven program with one def inserted
// and the two-MOV suffix replaced by the MRT suffix, nothing else. patchMrt() itself is a deterministic transform cross-checked against the Python oracle
// (tests/test_water_shader_patch.py).
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
#include "diagnostics_switch.h"
#include "water_material_mask_compiled.h"
class NorthlightWaterRenderer {
    IDirect3DDevice9* d;
    const NorthlightWaterShaderIdentity* identities;std::size_t identityCount;
    NorthlightStateBlockPool stateBlocks;
    std::unordered_map<IDirect3DVertexShader9*,IDirect3DVertexShader9*> vertex;
    std::unordered_map<IDirect3DPixelShader9*,IDirect3DPixelShader9*> pixel;
    struct MrtPixel {IDirect3DPixelShader9* shader;unsigned major;};
    std::unordered_map<IDirect3DPixelShader9*,MrtPixel> pixelMrt; // 0.3.214 (water MRT): the game's pixel shader -> its MRT variant (and its shader model)
    IDirect3DTexture9 *mask=nullptr,*raw=nullptr;
    IDirect3DSurface9 *maskSurface=nullptr,*depth=nullptr,*rawSurface=nullptr,*twin=nullptr; // raw: the (resolved) render target 1 data; twin: its multisampled twin when the game's target is multisampled
    IDirect3DPixelShader9* resolvePS=nullptr;
    IDirect3DVertexShader9* materialVS=nullptr;IDirect3DPixelShader9* materialPS=nullptr;
    UINT width=0,height=0;D3DMULTISAMPLE_TYPE twinType=D3DMULTISAMPLE_NONE;DWORD twinQuality=0;
    // 0.3.214 (water MRT)
    bool mrtVariants=true,mrtCaps=false,mrtFailed=false,mrtBound=false,mrtActive=false,mrtDirty=false,mrtCaptured=false,mrtResolved=false,mrtCleared=false,mrtLogged=false;
    unsigned mrtDraws=0,mrtDegraded=0,mrtSkipMask=0,mrtBinds=0,mrtRun=0,mrtLongestRun=0; /* 0.3.215: binds of render target 1 this frame, the MRT draws of the current bind, the longest such run this frame */DWORD mrtPrevWrite1=15;IDirect3DVertexShader9* mrtVS=nullptr;IDirect3DPixelShader9* mrtPS=nullptr;
    unsigned patchedShaders=0,patchRejects=0,mrtShaders=0;double patchMicroseconds=0;
    bool failed=false,captured=false,maskMayBeBound=true;float maskMinZ=0,maskMaxZ=1;unsigned captures=0,maskScans=0,clears=0,readFailures=0;std::uint64_t scanEpoch=~std::uint64_t(0);
    bool check(HRESULT hr,const char* where){if(SUCCEEDED(hr))return true;if(!failed)logf("WATER disabled: %s HRESULT=0x%08lx",where,(unsigned long)hr);failed=true;return false;}
    bool target(UINT w,UINT h,D3DFORMAT fmt,IDirect3DTexture9*& tex,IDirect3DSurface9*& surface){
        return check(d->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,fmt,D3DPOOL_DEFAULT,&tex,nullptr),"create target")&&check(tex->GetSurfaceLevel(0,&surface),"get surface");
    }
    bool maskResources(UINT w,UINT h){
        if(failed||!w||!h)return false;
        if(w!=width||h!=height){unbindMrt();reset();width=w;height=h;}
        if(mask&&depth)return true; /* 0.3.214 (water MRT): the MRT path creates the mask texture without the second pass's own depth */
        return (mask||target(w,h,D3DFMT_G32R32F,mask,maskSurface))&&check(d->CreateDepthStencilSurface(w,h,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,&depth,nullptr),"mask depth");
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
    // mrt: build the MRT pixel variants at registration (the host's creation-time setting; tests that pin the second-draw pass to 0.3.140 pass false).
    explicit NorthlightWaterRenderer(IDirect3DDevice9* device,const NorthlightWaterShaderIdentity* table=kWaterShaderIdentities,std::size_t tableCount=std::size(kWaterShaderIdentities),bool mrt=true)
        :d(device),identities(table),identityCount(tableCount),stateBlocks(device),mrtVariants(mrt){}
    ~NorthlightWaterRenderer(){reset();drop(resolvePS);drop(materialVS);drop(materialPS);for(auto& p:vertex)drop(p.second);for(auto& p:pixel)drop(p.second);for(auto& p:pixelMrt)drop(p.second.shader);}
    NorthlightWaterRenderer(const NorthlightWaterRenderer&)=delete;
    void releaseStateCache(){stateBlocks.clear();}
    // The host unbinds render target 1 before it resets or releases the device (unbindMrt); mrtBound is cleared here only to match.
    void reset(){stateBlocks.clear();drop(maskSurface);drop(depth);drop(mask);drop(rawSurface);drop(raw);drop(twin);width=height=0;twinType=D3DMULTISAMPLE_NONE;twinQuality=0;captured=false;captures=0;mrtBound=mrtActive=mrtDirty=mrtCaptured=mrtResolved=mrtCleared=false;}
    void endFrame(){if((!captured&&!mrtCaptured)||failed)stateBlocks.clear();captured=mrtCaptured=mrtResolved=mrtDirty=mrtCleared=false;captures=maskScans=clears=readFailures=mrtDraws=mrtDegraded=mrtBinds=mrtLongestRun=0;}
    // Explicit retry only after host frame cleanup; no allocation or hot-path work.
    void recover(){if(!failed)return;reset();failed=false;logf("WATER explicit recovery requested");}
    bool hasCapturedWater()const{return (captured||mrtResolved)&&!failed;}
    // 0.3.214 (water MRT): an MRT frame's data is only handed out after resolveMask() decoded it.
    IDirect3DTexture9* maskTextureForDepth(float minZ,float maxZ){
        IDirect3DTexture9* out=(captured||mrtResolved)&&!mrtDirty&&!failed&&mask&&std::isfinite(minZ)&&std::isfinite(maxZ)&&maxZ>minZ&&
            std::fabs(minZ-maskMinZ)<.0001f&&std::fabs(maxZ-maskMaxZ)<.0001f?mask:nullptr;
        if(out)maskMayBeBound=true;
        return out;
    }
    // Sampled diagnostics: mask draws and host-stage scans this frame.
    unsigned frameCaptures()const{return captures;}
    unsigned frameMrtDraws()const{return mrtDraws;}
    unsigned frameMrtDegraded()const{return mrtDegraded;} // planned as MRT, made as second draws
    unsigned frameMrtBinds()const{return mrtBinds;}
    unsigned frameMrtLongestRun()const{return mrtLongestRun;}
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
        auto oldMrt=pixelMrt.find(original);if(oldMrt!=pixelMrt.end()){drop(oldMrt->second.shader);pixelMrt.erase(oldMrt);}
        if(failed)return;
        std::vector<NorthlightWaterShaderPatch::Word> patched;const DWORD* code=maskShader(hash,false,words,count,patched);if(!code)return;
        IDirect3DPixelShader9* shader=nullptr;if(!check(d->CreatePixelShader(code,&shader),"water pixel mask shader"))return;
        pixel.emplace(original,shader);
        // 0.3.214 (water MRT): the MRT variant, only after the mask variant above matched its recorded hash (the bytes are the known shader) and then only if
        // it is exactly that proven program plus the def and the MRT suffix (verifyMrt). A refusal keeps this shader on the second-draw pass.
        bool mrt=false;
        if(mrtVariants){
            std::vector<NorthlightWaterShaderPatch::Word> variant;const char* why=NorthlightWaterShaderPatch::patchMrt(reinterpret_cast<const NorthlightWaterShaderPatch::Word*>(words),count,variant);
            if(!why&&!NorthlightWaterShaderPatch::verifyMrt(reinterpret_cast<const NorthlightWaterShaderPatch::Word*>(words),count,patched,variant))why="verification against the mask variant failed";
            IDirect3DPixelShader9* made=nullptr;
            if(!why&&FAILED(d->CreatePixelShader(reinterpret_cast<const DWORD*>(variant.data()),&made)))why="CreatePixelShader refused it";
            if(why){if(mrtShaders++<8)logf("WATER MRT pixel variant skipped hash=%016llx: %s (second draw stays)",(unsigned long long)hash,why);}
            else{pixelMrt.emplace(original,MrtPixel{made,unsigned((words[0]>>8)&255)});mrt=true;}
        }
        logf("WATER registered pixel hash=%016llx mrt=%d",(unsigned long long)hash,int(mrt));
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
        if((captured||mrtCaptured)&&(vp.MinZ!=maskMinZ||vp.MaxZ!=maskMaxZ))return;
        if(stateEpoch!=scanEpoch){scanEpoch=stateEpoch;maskMayBeBound=true;}
        MaskRestore save(d,vs,ps,mainDepth);if(!save.read(vp,userPointer)){++readFailures;return;}
        if(maskResources(mainWidth,mainHeight))drawMask(vi->second,pi->second,vp,save,draw);
    }
    // Own geometry mask: material identity and position ABI were validated by
    // the host. No native colour or per-vertex glint enters this mask.
    template<class Draw> void captureMaterial(IDirect3DVertexShader9* vs,IDirect3DPixelShader9* ps,UINT w,UINT h,IDirect3DSurface9* mainDepth,const D3DVIEWPORT9& vp,unsigned up,std::uint64_t epoch,Draw draw){
        if(failed||vp.X||vp.Y||vp.Width!=w||vp.Height!=h)return;
        if((captured||mrtCaptured)&&(vp.MinZ!=maskMinZ||vp.MaxZ!=maskMaxZ))return;
        if(epoch!=scanEpoch){scanEpoch=epoch;maskMayBeBound=true;}
        if(!materialVS&&!check(d->CreateVertexShader(kWaterMaterialMaskVS,&materialVS),"material mask VS"))return;
        if(!materialPS&&!check(d->CreatePixelShader(kWaterMaterialMaskPS,&materialPS),"material mask PS"))return;
        MaskRestore save(d,vs,ps,mainDepth);if(!save.read(vp,up)){++readFailures;return;}
        DWORD wrap=0,alpha=0;if(FAILED(d->GetRenderState(D3DRS_WRAP7,&wrap))||FAILED(d->GetRenderState(D3DRS_ALPHATESTENABLE,&alpha))){++readFailures;return;}
        struct MaterialRestore {IDirect3DDevice9* d;DWORD wrap,alpha;~MaterialRestore(){d->SetRenderState(D3DRS_WRAP7,wrap);d->SetRenderState(D3DRS_ALPHATESTENABLE,alpha);}} restore{d,wrap,alpha};
        if(FAILED(d->SetRenderState(D3DRS_WRAP7,0))||FAILED(d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE)))return;
        if(maskResources(w,h))drawMask(materialVS,materialPS,vp,save,draw);
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

    // ---- 0.3.214 (water MRT): the mask written by the game's own water draw ----
    // Semantics of render target 1 (G32R32F, cleared to 0 once per frame): a covered sample holds (distance, 1, 1, 1) where the draw's alpha is above .003,
    // (0,0,0,0) = nothing. oC1's alpha is the cover too, so the game's own blend (read per draw, see blendKeeps) either replaces the old value (cover 1) or keeps it
    // (cover 0 with SRCALPHA/INVSRCALPHA or ONE/INVSRCALPHA); the draw's depth test is the game's, so water behind opaque geometry never lands. Overlapping water layers
    // resolve by draw order (the last covering draw wins), where the second-draw pass kept the nearest. A multisampled game target gets a multisampled twin that
    // StretchRect averages once per frame; resolveMask() turns the averaged (n/N*distance, n/N) back into (distance, 1) for pixels with at least half of their samples covered.
    // The host's one-time device caps verdict: two render targets, independent write masks and bit depths, blending on the extra target, pixel shader 2.0.
    void setMrtCaps(bool ok){mrtCaps=ok;}
    bool mrtEnabled()const{return mrtVariants&&mrtCaps&&!mrtFailed&&!failed;}
    bool isMrtBound()const{return mrtBound;}
    void unbindMrt(){if(mrtBound){d->SetRenderTarget(1,nullptr);mrtBound=false;}}
    enum class Plan {Skip,Redraw,Mrt};
    // captureWater's verdict for a draw it recognised as water (main depth and full viewport already verified): Skip = no mask for this draw (what capture() does silently),
    // Redraw = the second-draw pass, Mrt = the draw writes the mask itself (beginMrt before it, endMrt after).
    Plan plan(IDirect3DVertexShader9* vs,IDirect3DPixelShader9* ps,UINT mainWidth,UINT mainHeight,const D3DVIEWPORT9& vp)const{
        if(failed||vertex.find(vs)==vertex.end()||pixel.find(ps)==pixel.end())return Plan::Skip;
        if(vp.X||vp.Y||vp.Width!=mainWidth||vp.Height!=mainHeight)return Plan::Skip;
        if((captured||mrtCaptured)&&(vp.MinZ!=maskMinZ||vp.MaxZ!=maskMaxZ))return Plan::Skip;
        return mrtEnabled()&&pixelMrt.find(ps)!=pixelMrt.end()?Plan::Mrt:Plan::Redraw;
    }
    // The blend of a draw may write render target 1 only when its result is the new value or the old one: disabled, or ADD with a source factor of ONE or SRCALPHA
    // and a destination factor of ZERO or INVSRCALPHA (oC1's alpha is the cover, 0 or 1). Separate alpha factors do not matter: the format has no alpha.
    static bool blendKeeps(DWORD enable,DWORD src,DWORD dst,DWORD op){
        return !enable||(op==D3DBLENDOP_ADD&&(src==D3DBLEND_ONE||src==D3DBLEND_SRCALPHA)&&(dst==D3DBLEND_ZERO||dst==D3DBLEND_INVSRCALPHA));
    }
    // Just before the game's draw. True: render target 1 holds the data target, the draw's shaders are the mask variants and colour write 1 is on (endMrt undoes the
    // swaps; the binding stays until unbindMrt). False: the draw must be made as a second draw (render target 1 may still be bound: the host unbinds it first).
    bool beginMrt(IDirect3DVertexShader9* vs,IDirect3DPixelShader9* ps,UINT mainWidth,UINT mainHeight,D3DMULTISAMPLE_TYPE msType,DWORD msQuality,const D3DVIEWPORT9& vp){
        if(!mrtEnabled()||mrtActive)return false;
        auto vi=vertex.find(vs);auto pi=pixelMrt.find(ps);
        if(vi==vertex.end()||pi==pixelMrt.end())return degrade(1);
        DWORD blend=0,src=0,dst=0,op=0,fog=0,srgb=0;
        const bool known=SUCCEEDED(d->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend))&&SUCCEEDED(d->GetRenderState(D3DRS_SRCBLEND,&src))&&SUCCEEDED(d->GetRenderState(D3DRS_DESTBLEND,&dst))&&
            SUCCEEDED(d->GetRenderState(D3DRS_BLENDOP,&op))&&SUCCEEDED(d->GetRenderState(D3DRS_FOGENABLE,&fog))&&SUCCEEDED(d->GetRenderState(D3DRS_SRGBWRITEENABLE,&srgb));
        if(!known)return degrade(16);
        if(!blendKeeps(blend,src,dst,op))return degrade(2,blend,src,dst,op);
        if(fog&&pi->second.major<3)return degrade(4,blend,src,dst,op,fog); // pixel shaders below 3.0 may get table fog after the shader, on every target
        if(srgb)return degrade(8,blend,src,dst,op,fog,srgb);
        if(!mrtResources(mainWidth,mainHeight,msType,msQuality))return degrade(32);
        IDirect3DSurface9* target=twin?twin:rawSurface;
        if(!mrtCleared){ // before the target is bound: a Clear would clear render target 0 too
            unbindMrt();
            if(!clearTarget(target)){mrtFail("clear",D3DERR_INVALIDCALL);return degrade(32);}
            mrtCleared=true;
        }
        if(!mrtBound){
            const HRESULT hr=d->SetRenderTarget(1,target);
            if(FAILED(hr)){mrtFail("bind render target 1",hr);return degrade(32);}
            mrtBound=true;++mrtBinds;mrtRun=0; /* 0.3.215: a new bind starts a new run */
        }
        if(!(captured||mrtCaptured)){maskMinZ=vp.MinZ;maskMaxZ=vp.MaxZ;}
        mrtCaptured=true;mrtDirty=true;
        if(FAILED(d->GetRenderState(D3DRS_COLORWRITEENABLE1,&mrtPrevWrite1)))mrtPrevWrite1=15;
        d->SetRenderState(D3DRS_COLORWRITEENABLE1,15);
        d->SetVertexShader(vi->second);d->SetPixelShader(pi->second.shader);
        mrtVS=vs;mrtPS=ps;mrtActive=true;return true;
    }
    void endMrt(bool drawn){
        if(!mrtActive)return;
        mrtActive=false;
        d->SetPixelShader(mrtPS);d->SetVertexShader(mrtVS);d->SetRenderState(D3DRS_COLORWRITEENABLE1,mrtPrevWrite1);
        if(drawn){++mrtDraws;if(++mrtRun>mrtLongestRun)mrtLongestRun=mrtRun;}
    }
    // Once per frame, before the effects read the mask and after render target 1 was unbound: the multisampled twin is resolved into the data texture, the mask is cleared
    // (unless a second draw already did) and one full-screen pass decodes the data into it. False: no MRT data this frame (a failure disables the MRT path for the session).
    bool resolveMask(){
        if(!mrtDirty)return true;
        unbindMrt();mrtDirty=false;
        if(failed||mrtFailed||!raw||!rawSurface||!maskSurface||!resolvePS){mrtFailed=true;return false;}
        if(twin){const HRESULT hr=d->StretchRect(twin,nullptr,rawSurface,nullptr,D3DTEXF_NONE);if(FAILED(hr)){mrtFail("resolve",hr);return false;}}
        if(!captured&&!clearTarget(maskSurface)){mrtFail("mask clear",D3DERR_INVALIDCALL);return false;}
        SavedState save(d,&stateBlocks);if(!save.ok){mrtFail("state save",D3DERR_INVALIDCALL);return false;}
        d->SetDepthStencilSurface(nullptr);for(unsigned i=1;i<4;++i)d->SetRenderTarget(i,nullptr);
        for(unsigned i=0;i<16;++i)d->SetTexture(i,nullptr); // the mask is about to be render target 0; no stage may still hold it
        d->SetVertexShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetStreamSourceFreq(0,1);
        const struct{D3DRENDERSTATETYPE s;DWORD v;} states[]={{D3DRS_ZENABLE,FALSE},{D3DRS_ZWRITEENABLE,FALSE},{D3DRS_ALPHATESTENABLE,FALSE},{D3DRS_ALPHABLENDENABLE,FALSE},{D3DRS_SEPARATEALPHABLENDENABLE,FALSE},{D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_COLORWRITEENABLE,15},{D3DRS_FOGENABLE,FALSE},{D3DRS_STENCILENABLE,FALSE},{D3DRS_SCISSORTESTENABLE,FALSE},{D3DRS_CLIPPLANEENABLE,0},{D3DRS_SRGBWRITEENABLE,FALSE},{D3DRS_FILLMODE,D3DFILL_SOLID},{D3DRS_MULTISAMPLEMASK,0xffffffff},{D3DRS_WRAP0,0}};
        for(auto& state:states)d->SetRenderState(state.s,state.v);
        d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0);d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);
        d->SetTexture(0,raw);d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
        d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT);d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);
        HRESULT hr=d->SetRenderTarget(0,maskSurface);
        if(SUCCEEDED(hr))hr=d->SetPixelShader(resolvePS);
        if(SUCCEEDED(hr))hr=quad();
        if(FAILED(hr)){mrtFail("decode pass",hr);return false;}
        mrtResolved=true;maskMayBeBound=true;return true;
    }
private:
    bool mrtResources(UINT w,UINT h,D3DMULTISAMPLE_TYPE type,DWORD quality){
        if(w!=width||h!=height){unbindMrt();reset();width=w;height=h;}
        if(twin&&(type==D3DMULTISAMPLE_NONE||twinType!=type||twinQuality!=quality)){unbindMrt();drop(twin);mrtCleared=false;}
        auto make=[&](IDirect3DTexture9*& tex,IDirect3DSurface9*& surface){
            if(tex)return true;
            HRESULT hr=d->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,D3DFMT_G32R32F,D3DPOOL_DEFAULT,&tex,nullptr);
            if(SUCCEEDED(hr))hr=tex->GetSurfaceLevel(0,&surface);
            if(FAILED(hr)){drop(surface);drop(tex);mrtFail("create target",hr);return false;}
            return true;
        };
        if(!make(mask,maskSurface)||!make(raw,rawSurface))return false;
        if(type!=D3DMULTISAMPLE_NONE&&!twin){
            const HRESULT hr=d->CreateRenderTarget(w,h,D3DFMT_G32R32F,type,quality,FALSE,&twin,nullptr);
            if(FAILED(hr)){drop(twin);mrtFail("create multisampled target",hr);return false;}
            twinType=type;twinQuality=quality;mrtCleared=false;
            logf("WATER mask MRT targets %ux%u G32R32F: multisampled twin ms=%u/%lu plus the resolved data and the mask",w,h,unsigned(type),(unsigned long)quality);
        }
        else if(type==D3DMULTISAMPLE_NONE&&!mrtLogged)logf("WATER mask MRT targets %ux%u G32R32F: no multisampling, the data target is a plain texture (no resolve)",w,h);
        mrtLogged=true;
        if(!resolvePS){
            const auto words=NorthlightWaterShaderPatch::resolveShader();
            const HRESULT hr=d->CreatePixelShader(reinterpret_cast<const DWORD*>(words.data()),&resolvePS);
            if(FAILED(hr)){drop(resolvePS);mrtFail("resolve shader",hr);return false;}
        }
        return true;
    }
    // ColorFill first (no target swap); where the driver refuses (multisampled targets), render target 0 is swapped for the Clear and put back with viewport and scissor.
    bool clearTarget(IDirect3DSurface9* target){
        if(SUCCEEDED(d->ColorFill(target,nullptr,0)))return true;
        IDirect3DSurface9* game=nullptr;D3DVIEWPORT9 vp={};RECT scissor={};DWORD scissorOn=0;
        if(FAILED(d->GetRenderTarget(0,&game))||!game||FAILED(d->GetViewport(&vp))){drop(game);return false;}
        const bool scissorKnown=SUCCEEDED(d->GetScissorRect(&scissor)),stateKnown=SUCCEEDED(d->GetRenderState(D3DRS_SCISSORTESTENABLE,&scissorOn)); // SetRenderTarget(0) resets the scissor rect too
        bool ok=false;
        if(SUCCEEDED(d->SetRenderTarget(0,target))){
            d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);ok=SUCCEEDED(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));
            if(stateKnown)d->SetRenderState(D3DRS_SCISSORTESTENABLE,scissorOn);
            d->SetRenderTarget(0,game);
        }
        d->SetViewport(&vp);if(scissorKnown)d->SetScissorRect(&scissor);drop(game);return ok;
    }
    void mrtFail(const char* where,HRESULT hr){
        if(!mrtFailed)logf("WATER mask MRT disabled: %s HRESULT=0x%08lx (the second-draw pass is used from now on)",where,(unsigned long)hr);
        mrtFailed=true;unbindMrt();
    }
    // Why a planned MRT draw is made as a second draw instead: logged once per reason bit (1 no variant, 2 blend, 4 fog before ps_3_0, 8 sRGB write, 16 state read, 32 resources).
    bool degrade(unsigned why,DWORD blend=0,DWORD src=0,DWORD dst=0,DWORD op=0,DWORD fog=0,DWORD srgb=0){
        ++mrtDegraded;
        if(!(mrtSkipMask&why)){mrtSkipMask|=why;logf("WATER mask MRT fallback to the second draw: reason=%u blend=%lu src=%lu dst=%lu op=%lu fog=%lu srgb=%lu",why,(unsigned long)blend,(unsigned long)src,(unsigned long)dst,(unsigned long)op,(unsigned long)fog,(unsigned long)srgb);}
        return false;
    }
};
