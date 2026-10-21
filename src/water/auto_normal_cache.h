#pragma once
#include "auto_normal_memory.h"
#include "quality_settings.h"
#include "material_height.h"

// GPU-only per-texture normal maps. Source references prevent pointer reuse;
// the budget includes their estimated byte size as well as the generated maps.
class NorthlightAutoNormalCache {
    struct Entry {IDirect3DTexture9* source;IDirect3DTexture9* normal;std::uint64_t bytes,used,version;unsigned frame;bool mutableSource;};
    IDirect3DDevice9* d;
    NorthlightStateBlockPool states;
    std::vector<Entry> entries;
    IDirect3DPixelShader9* shader=nullptr;
    std::uint64_t bytes=0,clock=0;
    unsigned generated=0,builds=0,hits=0,evictions=0;
    unsigned polarity=0,details=75,balance=50,frame=0;
public:
    explicit NorthlightAutoNormalCache(IDirect3DDevice9* device):d(device),states(device){}
    ~NorthlightAutoNormalCache(){reset();drop(shader);}
    void reset(){states.clear();for(auto& e:entries){drop(e.source);drop(e.normal);}entries.clear();bytes=0;}
    void configure(const NorthlightQuality::Settings& s){if(polarity!=s.autoHeightPolarity||details!=s.autoHeightDetails||balance!=s.autoHeightBalance){reset();polarity=s.autoHeightPolarity;details=s.autoHeightDetails;balance=s.autoHeightBalance;}}
    void endFrame(){generated=0;++frame;}
    void report(){logf("AUTONORMAL cached=%zu bytes=%llu generated=%u hits=%u evicted=%u",entries.size(),(unsigned long long)bytes,builds,hits,evictions);}
    IDirect3DTexture9* resolve(IDirect3DBaseTexture9* base){
        if(!base||base->GetType()!=D3DRTYPE_TEXTURE)return nullptr;
        auto* source=static_cast<IDirect3DTexture9*>(base);++clock;
        for(auto it=entries.begin();it!=entries.end();++it)if(it->source==source){if((it->version||it->mutableSource)&&(NorthlightMaterialHeight::version(source)!=it->version||(it->mutableSource&&!it->version&&frame-it->frame>=30))){bytes-=it->bytes;drop(it->source);drop(it->normal);entries.erase(it);break;}it->used=clock;++hits;return it->normal;}
        if(generated>=2)return nullptr;
        D3DSURFACE_DESC desc{};if(FAILED(source->GetLevelDesc(0,&desc))||!desc.Width||!desc.Height||(desc.Usage&D3DUSAGE_DEPTHSTENCIL))return nullptr;
        const UINT width=std::min(desc.Width,512u),height=std::min(desc.Height,512u);
        const std::uint64_t cost=NorthlightAutoNormalMemory::mipBytes(D3DFMT_A8R8G8B8,width,height,0)+NorthlightAutoNormalMemory::mipBytes(desc.Format,desc.Width,desc.Height,source->GetLevelCount());
        constexpr std::uint64_t budget=32ull<<20;if(cost>budget)return nullptr;
        while(!entries.empty()&&(bytes+cost>budget||entries.size()>=128)){
            auto oldest=std::min_element(entries.begin(),entries.end(),[](const Entry& a,const Entry& b){return a.used<b.used;});
            bytes-=oldest->bytes;drop(oldest->source);drop(oldest->normal);entries.erase(oldest);++evictions;
        }
        SavedState saved(d,&states);if(!saved.ok)return nullptr;
        if(!shader&&FAILED(d->CreatePixelShader(kAutoNormalShader,&shader)))return nullptr;
        IDirect3DTexture9* normal=nullptr;IDirect3DSurface9* target=nullptr;
        if(FAILED(d->CreateTexture(width,height,0,D3DUSAGE_RENDERTARGET|D3DUSAGE_AUTOGENMIPMAP,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&normal,nullptr)))return nullptr;
        if(FAILED(normal->GetSurfaceLevel(0,&target))){drop(normal);return nullptr;}
        d->SetDepthStencilSurface(nullptr);for(unsigned i=1;i<4;++i)d->SetRenderTarget(i,nullptr);
        const HRESULT bound=d->SetRenderTarget(0,target);drop(target);if(FAILED(bound)){drop(normal);return nullptr;}
        D3DVIEWPORT9 vp={0,0,width,height,0,1};d->SetViewport(&vp);
        for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_SCISSORTESTENABLE})d->SetRenderState(state,FALSE);
        d->SetRenderState(D3DRS_COLORWRITEENABLE,15);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
        d->SetTexture(0,source);d->SetVertexShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetPixelShader(shader);
        for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})d->SetSamplerState(0,state,D3DTEXF_LINEAR);
        d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);
        d->SetSamplerState(0,D3DSAMP_MAXMIPLEVEL,0);d->SetSamplerState(0,D3DSAMP_MIPMAPLODBIAS,0);
        d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP);d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP);
        NorthlightMaterialHeight::Image image;NorthlightMaterialHeight::read(source,image);const auto profile=NorthlightMaterialHeight::profile(image,polarity,details,balance);
        const float texel[]={1.f/width,1.f/height,5.f,0};d->SetPixelShaderConstantF(0,texel,1);d->SetPixelShaderConstantF(1,profile.data(),1);
        struct V {float x,y,z,w,u,v;};const V quad[]={{-.5f,-.5f,0,1,0,0},{width-.5f,-.5f,0,1,1,0},{-.5f,height-.5f,0,1,0,1},{width-.5f,height-.5f,0,1,1,1}};
        if(FAILED(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(V)))){drop(normal);return nullptr;}
        normal->SetAutoGenFilterType(D3DTEXF_LINEAR);normal->GenerateMipSubLevels();
        source->AddRef();try{entries.push_back({source,normal,cost,clock,NorthlightMaterialHeight::version(source),frame,(desc.Usage&(D3DUSAGE_DYNAMIC|D3DUSAGE_RENDERTARGET))!=0||(source->GetLevelCount()==1&&desc.Format==D3DFMT_A8R8G8B8&&desc.Width>=256&&desc.Height>=256)});}catch(...){source->Release();drop(normal);return nullptr;}bytes+=cost;++generated;++builds;return normal;
    }
};
