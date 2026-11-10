#pragma once
#include <d3d9.h>
#include <unordered_map>
#include "model_normal_mrt_patch.h"

// Owns programs and a frame transaction, but borrows the existing normal RT.
// The immutable replay queue remains the rollback journal until commit.
class NorthlightNativeModelNormals {
    template<class T> static void drop(T*& p){if(p)p->Release();p=nullptr;}
    struct Vertex {IDirect3DVertexShader9* source=nullptr;IDirect3DVertexShader9* shader[8]={};unsigned tried=0,semantics=0;bool valid=false;};
    struct Pixel {IDirect3DPixelShader9* source=nullptr;IDirect3DPixelShader9* normal[8][3]={},*zero[3]={};unsigned tried[8]={},zeroTried=0,semantics=0,outputs=0;bool valid=false;};
    IDirect3DDevice9* d;
    void* rasterContext=nullptr;bool(*rasterReader)(void*,const D3DRENDERSTATETYPE*,unsigned,DWORD*)=nullptr;
    std::unordered_map<IDirect3DVertexShader9*,Vertex> vertices;
    std::unordered_map<IDirect3DPixelShader9*,Pixel> pixels;
    // Sources are retained; unordered_map rehash preserves entry addresses.
    Vertex* recentVertex=nullptr;Pixel* recentPixel=nullptr;
    IDirect3DSurface9* describedColor=nullptr;D3DSURFACE_DESC colorDescription{};
    IDirect3DSurface9* sceneDepth=nullptr;
    D3DCAPS9 caps{};bool capable=false,linearTarget=false,blendTarget=false,enabled=false,lastComplete=false,allowed=false,complete=true,started=false,refused=false,depthOnlySeen=false;
    const char* reason="none";
    unsigned probes=0,modelDraws=0,zeroDraws=0,builds=0,committed=0,fallbacks=0,coldPairs=0;
    template<class Shader> static bool code(Shader* shader,std::vector<std::uint32_t>& words){UINT bytes=0;
        if(!shader||FAILED(shader->GetFunction(nullptr,&bytes))||bytes<8||bytes>65536||bytes%4)return false;
        words.resize(bytes/4);return SUCCEEDED(shader->GetFunction(reinterpret_cast<DWORD*>(words.data()),&bytes));
    }
    template<class T> struct Ref {T* p=nullptr;~Ref(){if(p)p->Release();}};
    static unsigned semantics(const std::uint32_t* words,const NorthlightTerrainCoverage::Program& p){unsigned bits=0;
        for(const auto& op:p.ops)if(op.code==31&&(words[op.at+1]&15)==5&&NorthlightWaterShaderPatch::regType(words[op.at+2])==(p.vertex?6u:1u)){unsigned s=(words[op.at+1]>>16)&15;if(s<8)bits|=1u<<s;}return bits;
    }
    Pixel* pixel(IDirect3DPixelShader9* source){if(recentPixel&&recentPixel->source==source)return recentPixel;
        auto found=pixels.find(source);if(found!=pixels.end())return recentPixel=&found->second;if(pixels.size()>=512)return nullptr;
        Pixel entry;entry.source=source;std::vector<std::uint32_t> words;NorthlightTerrainCoverage::Program p;
        entry.valid=code(source,words)&&NorthlightTerrainCoverage::analyse(words.data(),words.size(),p)&&!p.vertex&&NorthlightModelNormalMrt::terminalFlow(p);
        if(entry.valid){entry.semantics=semantics(words.data(),p);entry.outputs=NorthlightModelNormalMrt::outputMask(words.data(),p);}
        auto* added=&pixels.emplace(source,entry).first->second;source->AddRef();return recentPixel=added;
    }
    Vertex* vertex(IDirect3DVertexShader9* source){if(recentVertex&&recentVertex->source==source)return recentVertex;
        auto found=vertices.find(source);if(found!=vertices.end())return recentVertex=&found->second;if(vertices.size()>=256)return nullptr;
        Vertex entry;entry.source=source;std::vector<std::uint32_t> words;NorthlightTerrainCoverage::Program p;
        entry.valid=code(source,words)&&NorthlightTerrainCoverage::analyse(words.data(),words.size(),p,true)&&p.vertex;
        if(entry.valid)entry.semantics=semantics(words.data(),p);auto* added=&vertices.emplace(source,entry).first->second;source->AddRef();return recentVertex=added;
    }
    bool programs(Vertex* v,Pixel& p,unsigned slot,unsigned semantic,IDirect3DVertexShader9*& vs,IDirect3DPixelShader9*& ps){
        std::vector<std::uint32_t> words,patched;
        if(v){
            if(!(v->tried&(1u<<semantic))){std::vector<std::uint32_t> pixelWords;unsigned actual=8;
                if(code(v->source,words)&&code(p.source,pixelWords)&&NorthlightModelNormalMrt::vertex(words.data(),words.size(),pixelWords.data(),pixelWords.size(),patched,actual)&&actual==semantic){
                    if(SUCCEEDED(d->CreateVertexShader(reinterpret_cast<const DWORD*>(patched.data()),v->shader+semantic)))++builds;else drop(v->shader[semantic]);}
                v->tried|=1u<<semantic;
            }
            vs=v->shader[semantic];if(!vs)return false;
        }
        auto& tried=v?p.tried[semantic]:p.zeroTried;auto& shader=v?p.normal[semantic][slot-1]:p.zero[slot-1];
        if(!(tried&(1u<<slot))){
            if(code(p.source,words)&&NorthlightModelNormalMrt::pixel(words.data(),words.size(),slot,v!=nullptr,semantic,patched)){
                if(SUCCEEDED(d->CreatePixelShader(reinterpret_cast<const DWORD*>(patched.data()),&shader)))++builds;else drop(shader);}
            tried|=1u<<slot;
        }
        ps=shader;return ps!=nullptr;
    }
    template<class Call> static bool retry(Call call){return SUCCEEDED(call())||SUCCEEDED(call());}
    bool clear(IDirect3DSurface9* target){
        // Avoid changing the native render targets when FP16 fills are supported.
        if(SUCCEEDED(d->ColorFill(target,nullptr,0)))return true;
        Ref<IDirect3DSurface9> old[4],depth;D3DVIEWPORT9 vp{};
        for(unsigned i=0;i<4;++i){auto hr=d->GetRenderTarget(i,&old[i].p);if(FAILED(hr)&&hr!=D3DERR_NOTFOUND)return false;}
        auto hr=d->GetDepthStencilSurface(&depth.p);if((FAILED(hr)&&hr!=D3DERR_NOTFOUND)||FAILED(d->GetViewport(&vp)))return false;
        bool ok=SUCCEEDED(d->SetDepthStencilSurface(nullptr));
        for(unsigned i=1;i<4;++i)ok=SUCCEEDED(d->SetRenderTarget(i,nullptr))&&ok;
        ok=SUCCEEDED(d->SetRenderTarget(0,target))&&ok;
        if(ok)ok=SUCCEEDED(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));
        bool restored=retry([&]{return d->SetRenderTarget(0,old[0].p);});
        for(unsigned i=1;i<4;++i)restored=retry([&]{return d->SetRenderTarget(i,old[i].p);})&&restored;
        restored=retry([&]{return d->SetDepthStencilSurface(depth.p);})&&restored;
        restored=retry([&]{return d->SetViewport(&vp);})&&restored;
        return restored&&ok;
    }
public:
    // The active factory may promise to restore a shader after submit returns.
    enum PairProof:unsigned {PairValid=1,OuterRestoresVertex=2,OuterRestoresPixel=4};
    struct Result {HRESULT hr=D3D_OK;bool probed=false,written=false;};
    void setRasterStateSource(void* context,bool(*reader)(void*,const D3DRENDERSTATETYPE*,unsigned,DWORD*)){rasterContext=context;rasterReader=context==d?reader:nullptr;}
    explicit NorthlightNativeModelNormals(IDirect3DDevice9* device):d(device){
        capable=d&&SUCCEEDED(d->GetDeviceCaps(&caps))&&caps.NumSimultaneousRTs>=2&&caps.PixelShaderVersion>=D3DPS_VERSION(3,0);
        Ref<IDirect3D9> api;D3DDEVICE_CREATION_PARAMETERS creation{};D3DDISPLAYMODE mode{};
        if(capable&&SUCCEEDED(d->GetDirect3D(&api.p))&&api.p&&SUCCEEDED(d->GetCreationParameters(&creation))&&SUCCEEDED(d->GetDisplayMode(0,&mode))){
            // sRGB writes are ignored for formats without sRGB-write support.
            // Prove that the existing FP16 normal target stays linear once.
            linearTarget=api.p->CheckDeviceFormat(creation.AdapterOrdinal,creation.DeviceType,mode.Format,D3DUSAGE_RENDERTARGET|D3DUSAGE_QUERY_SRGBWRITE,D3DRTYPE_TEXTURE,D3DFMT_A16B16G16R16F)==D3DERR_NOTAVAILABLE;
            blendTarget=SUCCEEDED(api.p->CheckDeviceFormat(creation.AdapterOrdinal,creation.DeviceType,mode.Format,D3DUSAGE_RENDERTARGET|D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING,D3DRTYPE_TEXTURE,D3DFMT_A16B16G16R16F));
        }
    }
    ~NorthlightNativeModelNormals(){reset();}
    void configure(bool on){enabled=on;if(!on){lastComplete=false;cancel("disabled");}}
    bool usesDepth(IDirect3DSurface9* value)const{return sceneDepth&&sceneDepth==value;}
    bool accepting()const{return enabled&&capable&&!refused&&complete;}
    void cancel(const char* cause){if(complete)reason=cause;complete=false;lastComplete=false;}
    void beginFrame(){drop(sceneDepth);describedColor=nullptr;allowed=enabled&&lastComplete;complete=true;started=depthOnlySeen=false;reason="none";probes=modelDraws=zeroDraws=builds=committed=fallbacks=coldPairs=0;}
    void reset(){beginFrame();lastComplete=allowed=false;refused=false;
        recentVertex=nullptr;recentPixel=nullptr;
        for(auto& item:vertices){for(auto*& shader:item.second.shader)drop(shader);drop(item.second.source);}vertices.clear();
        for(auto& item:pixels){for(auto& row:item.second.normal)for(auto*& shader:row)drop(shader);for(auto*& shader:item.second.zero)drop(shader);drop(item.second.source);}pixels.clear();
    }
    bool finish(bool allProbed,bool allWritten,unsigned packets){
        lastComplete=enabled&&capable&&!refused&&complete&&allProbed&&packets!=0;
        if(lastComplete&&allowed&&started&&allWritten){committed=packets;return true;}
        if(packets)++fallbacks;return false;
    }
    template<class Log> void report(unsigned frame,Log log)const{
        log("MODEL_NORMALS MRT frame=%u enabled=%u allowed=%u complete=%u committed=%u probes=%u modelDraws=%u occluderDraws=%u shaderBuilds=%u fallbacks=%u reason=%s vertices=%zu pixels=%zu",frame,unsigned(enabled),unsigned(allowed),unsigned(lastComplete),committed,probes,modelDraws,zeroDraws,builds,fallbacks,reason,vertices.size(),pixels.size());
    }
    template<class Draw,class Prove> Result draw(Draw submit,IDirect3DSurface9* target,IDirect3DSurface9* color,UINT width,UINT height,
                                              IDirect3DVertexShader9* original,bool receiver,Prove prove){
        auto fallback=[&](const char* cause){cancel(cause);return Result{submit(),false,false};};
        if(!enabled||!capable||refused||!complete)return {submit(),false,false};
        // Receivers are opaque. Blended depth writers clear auxiliary RGB with
        // alpha=1 independently of their original native colour/alpha output.
        const D3DRENDERSTATETYPE keys[]={D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_COLORWRITEENABLE,D3DRS_ZFUNC};DWORD values[7]={};
        if(!(rasterReader&&rasterReader(rasterContext,keys,7,values)))
            for(unsigned i=0;i<7;++i)if(FAILED(d->GetRenderState(keys[i],values+i)))return fallback("raster-state");
        const auto z=values[0],write=values[1],blend=values[2],srgb=values[3],alpha=values[4];
        if(!z||!write)return {submit(),false,false};
        // The replay queue ignores draws without scene RGB writes too. Keep
        // previous normals; their stored distance is checked against final depth.
        if(!receiver&&!(values[5]&7)){depthOnlySeen|=values[6]!=D3DCMP_EQUAL&&values[6]!=D3DCMP_NEVER;const auto hr=submit();if(FAILED(hr))cancel("native-submit");return {hr,false,false};}
        // A later RGB depth writer could fail native Z against that invisible
        // occluder but pass the replay's private depth. Retain exact rollback.
        if(depthOnlySeen)return fallback("depth-only-before-rgb");
        if(srgb&&!linearTarget)return fallback("srgb-format");
        if((blend||alpha)&&(!blendTarget||!(caps.PrimitiveMiscCaps&D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING)))return fallback("post-blend");
        if(blend){
            if(receiver)return fallback("blended-receiver");
            const D3DRENDERSTATETYPE states[]={D3DRS_SRCBLEND,D3DRS_DESTBLEND,D3DRS_BLENDOP};DWORD values[3]={};
            if(!(rasterReader&&rasterReader(rasterContext,states,3,values)))
                for(unsigned i=0;i<3;++i)if(FAILED(d->GetRenderState(states[i],values+i)))return fallback("blend-state");
            if((values[0]!=D3DBLEND_ONE&&values[0]!=D3DBLEND_SRCALPHA)||
               (values[1]!=D3DBLEND_ZERO&&values[1]!=D3DBLEND_INVSRCALPHA)||values[2]!=D3DBLENDOP_ADD)return fallback("blend-function");
        }
        Ref<IDirect3DSurface9> rt,depth;D3DVIEWPORT9 vp{};
        if(FAILED(d->GetRenderTarget(0,&rt.p))||!rt.p||FAILED(d->GetDepthStencilSurface(&depth.p))||!depth.p||
           FAILED(d->GetViewport(&vp)))return fallback("target-state");
        if(rt.p!=color||(describedColor&&describedColor!=color))return fallback("target-state");
        // The owner retains frameTarget. Surface dimensions/format/sample count
        // are immutable; current RT/DS/viewport ownership is still proved above.
        if(!describedColor){if(FAILED(color->GetDesc(&colorDescription)))return fallback("target-state");describedColor=color;}
        const auto& desc=colorDescription;
        if(vp.X||vp.Y||vp.Width!=width||vp.Height!=height||desc.Width!=width||desc.Height!=height||
           desc.MultiSampleType!=D3DMULTISAMPLE_NONE||(sceneDepth&&sceneDepth!=depth.p)||
           (desc.Format!=D3DFMT_A16B16G16R16F&&!(caps.PrimitiveMiscCaps&D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS)))return fallback("target-msaa");
        Ref<IDirect3DPixelShader9> oldPS;if(FAILED(d->GetPixelShader(&oldPS.p))||!oldPS.p)return fallback("fixed-pixel");
        Pixel* p=nullptr;try{p=pixel(oldPS.p);}catch(const std::bad_alloc&){return fallback("allocation");}if(!p||!p->valid)return fallback("pixel-program");
        unsigned slot=0;for(unsigned i=1;i<4&&i<caps.NumSimultaneousRTs;++i){if(p->outputs&(1u<<i))continue;Ref<IDirect3DSurface9> used;const auto hr=d->GetRenderTarget(i,&used.p);
            if(FAILED(hr)&&hr!=D3DERR_NOTFOUND)return fallback("target-query");if(!used.p){slot=i;break;}}
        if(!slot)return fallback("occupied-mrt");
        Ref<IDirect3DVertexShader9> oldVS;Vertex* v=nullptr;unsigned semantic=0,proof=0;
        if(receiver){if(FAILED(d->GetVertexShader(&oldVS.p))||!oldVS.p||!original)return fallback("vertex-provenance");
            proof=prove(original,oldVS.p,oldPS.p);if(!(proof&PairValid))return fallback("vertex-provenance");
            try{v=vertex(oldVS.p);}catch(const std::bad_alloc&){return fallback("allocation");}if(!v||!v->valid)return fallback("vertex-program");
            while(semantic<8&&((v->semantics|p->semantics)&(1u<<semantic)))++semantic;if(semantic==8)return fallback("varyings");}
        IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;
        const bool cold=(v&&!(v->tried&(1u<<semantic)))||!((v?p->tried[semantic]:p->zeroTried)&(1u<<slot));
        if(cold){if(coldPairs)return fallback("shader-warmup");++coldPairs;}
        const auto beforeBuilds=builds;
        try{if(!programs(v,*p,slot,semantic,vs,ps))return fallback("shader-factory");}catch(const std::bad_alloc&){return fallback("allocation");}
        // A newly encountered pair is prepared on an ordinary replay frame.
        // Never pay a partial MRT plus its full rollback every steady frame.
        if(builds!=beforeBuilds)allowed=false;
        ++probes;
        if(!sceneDepth){sceneDepth=depth.p;sceneDepth->AddRef();}
        if(!allowed){const auto hr=submit();if(FAILED(hr)){cancel("native-submit");return {hr,false,false};}return {hr,true,false};}
        if(!started){if(!clear(target)){refused=true;return fallback("clear");}started=true;}
        const auto key=slot==1?D3DRS_COLORWRITEENABLE1:slot==2?D3DRS_COLORWRITEENABLE2:D3DRS_COLORWRITEENABLE3;
        DWORD mask=0;if(FAILED(d->GetRenderState(key,&mask)))return fallback("write-mask");
        bool touchedTarget=false,touchedMask=false,touchedVS=false,touchedPS=false;
        auto restore=[&](bool failedBind=false){bool ok=true;
            if(touchedPS&&(failedBind||!(proof&OuterRestoresPixel)))ok=retry([&]{return d->SetPixelShader(oldPS.p);})&&ok;
            if(touchedVS&&(failedBind||!(proof&OuterRestoresVertex)))ok=retry([&]{return d->SetVertexShader(oldVS.p);})&&ok;
            if(touchedMask)ok=retry([&]{return d->SetRenderState(key,mask);})&&ok;
            if(touchedTarget)ok=retry([&]{return d->SetRenderTarget(slot,nullptr);})&&ok;
            return ok;};
        touchedTarget=true;bool bound=SUCCEEDED(d->SetRenderTarget(slot,target));
        if(bound&&mask!=15){touchedMask=true;bound=SUCCEEDED(d->SetRenderState(key,15));}
        if(bound&&receiver){touchedVS=true;bound=SUCCEEDED(d->SetVertexShader(vs));}
        if(bound){touchedPS=true;bound=SUCCEEDED(d->SetPixelShader(ps));}
        if(!bound){refused=true;cancel("bind");const bool restored=restore(true);return {restored?submit():E_FAIL,false,false};}
        const auto hr=submit();const bool restored=restore();
        if(FAILED(hr)||!restored){cancel("draw-restore");return {FAILED(hr)?hr:E_FAIL,false,false};}
        if(receiver)++modelDraws;else ++zeroDraws;return {hr,true,receiver};
    }
};
