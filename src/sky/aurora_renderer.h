#pragma once
#include "aurora_emission_patch.h"
#include "aurora_source.h"
#include <d3d9.h>
#include <unordered_map>
#include <algorithm>

namespace NorthlightAuroraNative {
// Only the independently proven aurora draw may use this transfer. Native VS,
// textures, alpha, depth and blend state remain authoritative. No extra draw.
class Renderer {
    struct Entry {IDirect3DPixelShader9* original=nullptr;IDirect3DPixelShader9* shaders[2]{};unsigned constants[2]{};};
    std::unordered_map<IDirect3DPixelShader9*,Entry> entries_;
    template<class T> static void drop(T*& p){if(p)p->Release();p=nullptr;}
public:
    ~Renderer(){reset();}
    void reset(){for(auto& item:entries_){drop(item.second.original);for(auto*& shader:item.second.shaders)drop(shader);}entries_.clear();}
    bool prepare(IDirect3DDevice9* d)try{
        IDirect3DPixelShader9* original=nullptr;if(FAILED(d->GetPixelShader(&original))||!original)return false;
        struct Release {IDirect3DPixelShader9* p;~Release(){p->Release();}} release{original};
        auto found=entries_.find(original);if(found!=entries_.end())return found->second.shaders[0]&&found->second.shaders[1];
        if(entries_.size()>=8)return false;
        auto& entry=entries_[original];entry.original=original;original->AddRef();
        UINT bytes=0;if(FAILED(original->GetFunction(nullptr,&bytes))||bytes<8||bytes>65536||bytes%4)return false;
        std::vector<std::uint32_t> native(bytes/4),patched;if(FAILED(original->GetFunction(native.data(),&bytes)))return false;
        for(unsigned linear=0;linear<2;++linear){EmissionPatch info;
            if(!patchEmission(native.data(),native.size(),linear!=0,patched,info)||
               FAILED(d->CreatePixelShader(reinterpret_cast<const DWORD*>(patched.data()),&entry.shaders[linear])))return false;
            entry.constants[linear]=info.constant;
        }return true;
    }catch(const std::bad_alloc&){return false;}
    template<class Draw> bool draw(IDirect3DDevice9* d,bool linear,const NorthlightAuroraSource::RGB& scale,Draw submit,HRESULT& result,float legacyExpansion=0,float intensity=1){
        if(!linear&&scale==NorthlightAuroraSource::RGB{}&&intensity==1)return false;
        IDirect3DPixelShader9* original=nullptr;if(FAILED(d->GetPixelShader(&original))||!original)return false;
        struct Restore {IDirect3DDevice9* d;IDirect3DPixelShader9* original;unsigned constant=0;float old[8]{};bool changed=false;
            ~Restore(){if(changed){d->SetPixelShader(original);d->SetPixelShaderConstantF(constant,old,2);}original->Release();}} restore{d,original};
        const auto found=entries_.find(original);if(found==entries_.end()||!found->second.shaders[linear])return false;
        restore.constant=found->second.constants[linear];if(FAILED(d->GetPixelShaderConstantF(restore.constant,restore.old,2)))return false;
        const float gain=std::exp2(std::isfinite(legacyExpansion)?std::clamp(legacyExpansion,0.f,2.f):0.f);
        const float value[]={scale[0],scale[1],scale[2],std::isfinite(intensity)?std::clamp(intensity,0.f,2.f):1.f,linear?gain:1.f,0,0,0};restore.changed=true;
        if(FAILED(d->SetPixelShaderConstantF(restore.constant,value,2))||FAILED(d->SetPixelShader(found->second.shaders[linear])))return false;
        result=submit();return true;
    }
};
}
