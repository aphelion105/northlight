#pragma once
#include "native_more_light_shader.h"
#include <d3d9.h>
#include <unordered_map>
#include <memory>

// Private shader variants and scoped constants. Public shader identity stays
// original for material, geometry, normal capture and native-light ownership.
class NorthlightNativeMoreLightRenderer {
    using Kind=NorthlightNativeMoreLightShader::Kind;
    struct Entry {
        IDirect3DVertexShader9* source=nullptr;IDirect3DVertexShader9* expanded=nullptr;Kind kind=Kind::None;
        ~Entry(){if(expanded)expanded->Release();if(source)source->Release();}
    };
    IDirect3DDevice9* device;
    std::unordered_map<IDirect3DVertexShader9*,Kind> kinds;
    std::unordered_map<IDirect3DVertexShader9*,std::unique_ptr<Entry>> sources,derived;
    Entry* prepare(IDirect3DVertexShader9* source,bool terrain,const DWORD* words=nullptr,size_t count=0,bool registration=false){
        auto old=sources.find(source);if(old!=sources.end())return old->second.get();
        if(!registration){auto found=derived.find(source);if(found!=derived.end())return found->second.get();}
        auto entry=std::make_unique<Entry>();entry->source=source;source->AddRef();
        std::vector<DWORD> saved;
        if(!words){UINT bytes=0;if(FAILED(source->GetFunction(nullptr,&bytes))||bytes<8||bytes>16384||bytes%4)return nullptr;
            saved.resize(bytes/4);if(FAILED(source->GetFunction(saved.data(),&bytes)))return nullptr;words=saved.data();count=saved.size();}
        std::vector<std::uint32_t> output;Kind kind=Kind::None;bool already=false;
        if(NorthlightNativeMoreLightShader::transform(reinterpret_cast<const std::uint32_t*>(words),count,terrain,output,kind,already)){
            if(already||SUCCEEDED(device->CreateVertexShader(reinterpret_cast<const DWORD*>(output.data()),&entry->expanded)))entry->kind=kind;
        }
        // Lightweight capabilities outlive bounded COM/program caches. Unknown
        // original registrations cannot evict a ready source variant.
        const bool original=registration||kinds.count(source)!=0;
        if(original&&entry->kind==Kind::None)return nullptr;
        auto& entries=original?sources:derived;if(entries.size()>=512)entries.clear();
        auto* result=entry.get();entries.emplace(source,std::move(entry));return result;
    }
public:
    unsigned modelLimit=8,terrainLimit=8;bool restoreFailed=false;
    explicit NorthlightNativeMoreLightRenderer(IDirect3DDevice9* d):device(d){}
    void registerShader(IDirect3DVertexShader9* shader,const DWORD* words,size_t count,bool terrain){
        kinds.erase(shader);sources.erase(shader);derived.erase(shader);if(!shader)return;
        if(auto* entry=prepare(shader,terrain,words,count,true))kinds[shader]=entry->kind;
    }
    Kind kind(IDirect3DVertexShader9* shader)const{const auto i=kinds.find(shader);return i==kinds.end()?Kind::None:i->second;}
    void limits(unsigned models,unsigned terrain){modelLimit=std::clamp(models,4u,8u);terrainLimit=std::clamp(terrain,3u,8u);}
    void deviceReset(){restoreFailed=false;}
    template<class Mirror,class Handoff,class Draw> HRESULT draw(Kind expected,const Mirror* mirror,Handoff handoff,Draw submit,bool handoffActive=true){
        if(restoreFailed)return E_FAIL;
        if(expected==Kind::None)return submit();
        IDirect3DVertexShader9* current=nullptr;if(FAILED(device->GetVertexShader(&current))||!current)return submit();
        struct Hold {IDirect3DVertexShader9* value;~Hold(){value->Release();}} held{current};
        Entry* entry=nullptr;try{entry=prepare(current,expected==Kind::Terrain);}catch(const std::bad_alloc&){return submit();}
        if(!entry||entry->kind!=expected)return submit();
        const unsigned registers=expected==Kind::Terrain?15:11,count=expected==Kind::Terrain?5:4;
        const unsigned allowed=expected==Kind::Terrain?terrainLimit-3:modelLimit-4;
        if(entry->expanded&&!allowed)return submit(); // The source already computes exactly the native base.
        const bool unchanged=allowed==count&&!handoffActive;
        if(!entry->expanded&&unchanged)return submit(); // Already eight: no constants or private binding needed.
        float before[60],after[60];unsigned first=0,last=0;
        const unsigned readCount=unchanged&&expected==Kind::Model?4:registers;
        bool cached=mirror&&mirror->heldByThisThread()&&mirror->active();
        if(cached)for(unsigned i=236;i<236+readCount;++i)cached=cached&&mirror->vsFloatKnown[i];
        const float* bank=cached?mirror->vsFloat[236]:before;
        if(!cached&&FAILED(device->GetVertexShaderConstantF(236,before,readCount)))return submit();
        if(!unchanged){if(cached)std::copy_n(bank,4*registers,before);std::copy_n(before,4*registers,after);
            for(unsigned slot=allowed;slot<count;++slot)std::fill_n(after+(expected==Kind::Terrain?12*slot+4:4*slot),3,0.f);
            if(handoffActive)handoff(expected==Kind::Terrain,after);bank=after;
            last=registers;while(first<last&&!std::memcmp(before+4*first,after+4*first,4*sizeof(float)))++first;
            while(last>first&&!std::memcmp(before+4*(last-1),after+4*(last-1),4*sizeof(float)))--last;
        }
        bool lit=false;for(unsigned slot=0;slot<allowed&&!lit;++slot){const float* rgb=bank+(expected==Kind::Terrain?12*slot+4:4*slot);lit=rgb[0]!=0.f||rgb[1]!=0.f||rgb[2]!=0.f;}
        if(entry->expanded&&!lit)return submit(); // No extra RGB survives limits/ownership: keep the source shader.
        bool constantsSet=false;
        const auto retry=[](auto action){return SUCCEEDED(action())||SUCCEEDED(action());};
        const auto restore=[&]{bool ok=true;if(constantsSet)ok=retry([&]{return device->SetVertexShaderConstantF(236+first,before+4*first,last-first);});
            if(entry->expanded)ok=retry([&]{return device->SetVertexShader(current);})&&ok;if(!ok)restoreFailed=true;return ok;};
        if(entry->expanded&&FAILED(device->SetVertexShader(entry->expanded)))return restore()?submit():E_FAIL;
        if(last>first){constantsSet=true;if(FAILED(device->SetVertexShaderConstantF(236+first,after+4*first,last-first)))return restore()?submit():E_FAIL;}
        struct Restore {decltype(restore)& action;bool done=false;~Restore(){if(!done)action();}} saved{restore};
        const HRESULT result=submit();const bool ok=restore();saved.done=true;return ok?result:E_FAIL;
    }
};
