#pragma once
#include "auto_normal_memory.h"

// Generated maps and retained source references have separate bounded budgets.
// Source references prevent pointer reuse; they are not duplicate GPU copies.
class NorthlightAutoNormalCache {
    struct Entry {IDirect3DTexture9* source;IDirect3DTexture9* normal;std::uint64_t bytes,sourceBytes,used;};
    IDirect3DDevice9* d;
    NorthlightStateBlockPool states;
    std::vector<Entry> entries;
    size_t recent=0;
    IDirect3DPixelShader9* shader=nullptr;
    std::uint64_t bytes=0,sourceBytes=0,clock=0,budget=32ull<<20;
    unsigned generated=0,builds=0,hits=0,evictions=0;
    unsigned frame=0;bool memoryPressure=false;
public:
    explicit NorthlightAutoNormalCache(IDirect3DDevice9* device):d(device),states(device){refreshBudget();}
    ~NorthlightAutoNormalCache(){reset();drop(shader);}
    void reset(){states.clear();for(auto& e:entries){drop(e.source);drop(e.normal);}entries.clear();bytes=sourceBytes=0;}
    void refreshBudget(){
        MEMORYSTATUSEX memory{};memory.dwLength=sizeof memory;
        budget=memoryPressure?32ull<<20:GlobalMemoryStatusEx(&memory)?NorthlightAutoNormalMemory::budget(memory.ullAvailVirtual,budget):32ull<<20;
        while(bytes>budget&&!entries.empty()){auto old=std::min_element(entries.begin(),entries.end(),[](const Entry& a,const Entry& b){return a.used<b.used;});bytes-=old->bytes;sourceBytes-=old->sourceBytes;drop(old->source);drop(old->normal);entries.erase(old);++evictions;}
    }
    void setMemoryPressure(bool on){memoryPressure=on;refreshBudget();states.clear();}
    void trimMemory(){refreshBudget();states.clear();}
    void endFrame(){generated=0;if(memoryPressure)states.clear();if(++frame%60==0)refreshBudget();}
    void report(){logf("AUTONORMAL cached=%zu bytes=%llu generated=%u hits=%u evicted=%u sourceBytes=%llu budgetMiB=%llu",entries.size(),(unsigned long long)bytes,builds,hits,evictions,(unsigned long long)sourceBytes,(unsigned long long)(budget>>20));}
    IDirect3DTexture9* resolve(IDirect3DBaseTexture9* base){
        if(!base||base->GetType()!=D3DRTYPE_TEXTURE)return nullptr;
        auto* source=static_cast<IDirect3DTexture9*>(base);++clock;
        auto it=entries.end();
        if(recent<entries.size()&&entries[recent].source==source)it=entries.begin()+recent;
        else it=std::find_if(entries.begin(),entries.end(),[&](const Entry& e){return e.source==source;});
        if(it!=entries.end()){recent=size_t(it-entries.begin());it->used=clock;++hits;return it->normal;}
        if(generated>=2)return nullptr;
        D3DSURFACE_DESC desc{};if(FAILED(source->GetLevelDesc(0,&desc))||!desc.Width||!desc.Height||(desc.Usage&(D3DUSAGE_DYNAMIC|D3DUSAGE_RENDERTARGET|D3DUSAGE_DEPTHSTENCIL)))return nullptr;
        const UINT width=std::min(desc.Width,512u),height=std::min(desc.Height,512u);
        const std::uint64_t cost=NorthlightAutoNormalMemory::mipBytes(D3DFMT_A8R8G8B8,width,height,0),sourceCost=NorthlightAutoNormalMemory::mipBytes(desc.Format,desc.Width,desc.Height,source->GetLevelCount());
        constexpr std::uint64_t sourceBudget=128ull<<20;if(cost>budget||sourceCost>sourceBudget)return nullptr;
        while(!entries.empty()&&(bytes+cost>budget||sourceBytes+sourceCost>sourceBudget||entries.size()>=256)){
            auto oldest=std::min_element(entries.begin(),entries.end(),[](const Entry& a,const Entry& b){return a.used<b.used;});
            bytes-=oldest->bytes;sourceBytes-=oldest->sourceBytes;drop(oldest->source);drop(oldest->normal);entries.erase(oldest);++evictions;
        }
        // Native D3D9's fixed-function draw can clear programmable VS constants
        // beyond those restored by an ALL state block. Preserve the bank on builds.
        struct VertexConstants {IDirect3DDevice9* device;float values[256*4]={};UINT count=0;~VertexConstants(){if(count)device->SetVertexShaderConstantF(0,values,count);}} vertex{d};
        D3DCAPS9 caps{};if(FAILED(d->GetDeviceCaps(&caps)))return nullptr;
        const UINT count=std::min<UINT>(caps.MaxVertexShaderConst,256u);if(!count||FAILED(d->GetVertexShaderConstantF(0,vertex.values,count)))return nullptr;vertex.count=count;
        SavedState saved(d,&states);if(!saved.ok)return nullptr;
        auto*& selected=shader;if(!selected&&FAILED(d->CreatePixelShader(kAutoNormalShader,&selected)))return nullptr;
        IDirect3DTexture9* normal=nullptr;IDirect3DSurface9* target=nullptr;
        if(FAILED(d->CreateTexture(width,height,0,D3DUSAGE_RENDERTARGET|D3DUSAGE_AUTOGENMIPMAP,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&normal,nullptr)))return nullptr;
        if(FAILED(normal->GetSurfaceLevel(0,&target))){drop(normal);return nullptr;}
        d->SetDepthStencilSurface(nullptr);for(unsigned i=1;i<4;++i)d->SetRenderTarget(i,nullptr);
        const HRESULT bound=d->SetRenderTarget(0,target);drop(target);if(FAILED(bound)){drop(normal);return nullptr;}
        D3DVIEWPORT9 vp={0,0,width,height,0,1};d->SetViewport(&vp);
        for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_SCISSORTESTENABLE})d->SetRenderState(state,FALSE);
        d->SetRenderState(D3DRS_COLORWRITEENABLE,15);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
        d->SetTexture(0,source);d->SetVertexShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetPixelShader(selected);
        for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})d->SetSamplerState(0,state,D3DTEXF_LINEAR);
        d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);
        d->SetSamplerState(0,D3DSAMP_MAXMIPLEVEL,0);d->SetSamplerState(0,D3DSAMP_MIPMAPLODBIAS,0);
        d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP);d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP);
        const float texel[]={1.f/width,1.f/height,5.f,0};d->SetPixelShaderConstantF(0,texel,1);
        struct V {float x,y,z,w,u,v;};const V quad[]={{-.5f,-.5f,0,1,0,0},{width-.5f,-.5f,0,1,1,0},{-.5f,height-.5f,0,1,0,1},{width-.5f,height-.5f,0,1,1,1}};
        if(FAILED(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(V)))){drop(normal);return nullptr;}
        normal->SetAutoGenFilterType(D3DTEXF_LINEAR);normal->GenerateMipSubLevels();
        source->AddRef();try{entries.push_back({source,normal,cost,sourceCost,clock});}catch(...){source->Release();drop(normal);return nullptr;}recent=entries.size()-1;bytes+=cost;sourceBytes+=sourceCost;++generated;++builds;return normal;
    }
};
