#pragma once
#include "patch_terrain_shadow_coverage.h"
#include "world_math.h"
#include <d3d9.h>
#include <array>
#include <cstring>
#include <unordered_map>

// These D3D9 shader objects survive Reset. Re-registering their source
// retires the derived programs and their native-light proof together.
class NorthlightTerrainShadowPrograms {
    template<class T> struct Shader {
        T* value=nullptr;
        Shader()=default;
        Shader(const Shader&)=delete;
        ~Shader(){clear();}
        void clear(){if(value)value->Release();value=nullptr;}
        void created(HRESULT result){if(FAILED(result))clear();}
    };
    struct Pixel {
        Shader<IDirect3DPixelShader9> shader;
        unsigned constants=0;
        bool known=false;
    };
    std::unordered_map<IDirect3DPixelShader9*,Shader<IDirect3DPixelShader9>> neutral_;
    std::unordered_map<IDirect3DVertexShader9*,Shader<IDirect3DVertexShader9>> vertices_;
    std::unordered_map<IDirect3DPixelShader9*,std::array<Pixel,2>> pixels_;
    float matrices_[2][16]={}, view_[16]={}, rows_[24]={};
    bool rowsValid_=false;
    unsigned sources_=1, neutralReports_=0, coverageReports_=0;

    template<class T> static bool read(T* shader,std::vector<uint32_t>& words){
        UINT bytes=0;
        if(!shader||FAILED(shader->GetFunction(nullptr,&bytes))||bytes<8||bytes>65536||bytes%4)return false;
        words.resize(bytes/4);
        return SUCCEEDED(shader->GetFunction(words.data(),&bytes));
    }

public:
    struct Coverage {
        IDirect3DVertexShader9* vertex=nullptr;
        IDirect3DPixelShader9* pixel=nullptr;
        unsigned first=0,count=0;
        const float* rows=nullptr;
    };

    void publish(const bool (&active)[2],const float (&matrices)[2][2][16]){
        sources_=0;
        for(unsigned i=0;i<2;++i)if(active[i])std::memcpy(matrices_[sources_++],matrices[i][1],64);
        rowsValid_=false;
    }

    template<class Change> void forgetPixel(IDirect3DPixelShader9* source,const Change& change){
        if(auto found=neutral_.find(source);found!=neutral_.end()){
            if(found->second.value)change(found->second.value,nullptr,0);
            neutral_.erase(found);
        }
        if(auto found=pixels_.find(source);found!=pixels_.end()){
            for(auto& pixel:found->second)if(pixel.shader.value)change(pixel.shader.value,nullptr,0);
            pixels_.erase(found);
        }
    }

    template<class Change> void forgetVertex(IDirect3DVertexShader9* source,const Change& change){
        if(auto found=vertices_.find(source);found!=vertices_.end()){
            if(found->second.value)change(found->second.value,nullptr,0);
            vertices_.erase(found);
        }
    }

    template<class Change,class Log>
    IDirect3DPixelShader9* neutral(IDirect3DDevice9* d,IDirect3DPixelShader9* source,const Change& change,const Log& log){
        try{
            auto [at,inserted]=neutral_.try_emplace(source);
            auto& replacement=at->second;
            if(!inserted)return replacement.value;
            try{
                std::vector<uint32_t> code,output;
                NorthlightTerrainShadow::PatchInfo info;
                if(read(source,code)&&NorthlightTerrainShadow::patch(code.data(),code.size(),output,&info)){
                    replacement.created(d->CreatePixelShader(reinterpret_cast<const DWORD*>(output.data()),&replacement.value));
                    if(replacement.value)change(replacement.value,output.data(),output.size());
                    if(replacement.value&&neutralReports_++<4)
                        log("TERRAIN SHADOW patched ps_%u_%u: replaced=%u shadow=r%u.%u one=c%d.%u instructions=%u",
                            info.major,info.minor,info.replaced,info.shadowRegister,info.shadowComponent,info.oneConstant,info.oneComponent,info.instructions);
                }
            }catch(const std::bad_alloc&){
                if(replacement.value)change(replacement.value,nullptr,0);
                neutral_.erase(at);
                return nullptr;
            }
            return replacement.value;
        }catch(const std::bad_alloc&){return nullptr;}
    }

    template<class VertexChange,class PixelChange,class Log>
    Coverage coverage(IDirect3DDevice9* d,IDirect3DVertexShader9* vertex,IDirect3DPixelShader9* pixel,
                      const float* inverseView,const VertexChange& changeVertex,const PixelChange& changePixel,const Log& log){
        if(!sources_)return {};
        try{
            auto [v,inserted]=vertices_.try_emplace(vertex);
            if(inserted){
                auto& replacement=v->second;
                try{
                    std::vector<uint32_t> code,output;
                    if(read(vertex,code)&&NorthlightTerrainCoverage::vertex(code.data(),code.size(),output)){
                        replacement.created(d->CreateVertexShader(reinterpret_cast<const DWORD*>(output.data()),&replacement.value));
                        if(replacement.value)changeVertex(replacement.value,output.data(),output.size());
                    }
                }catch(const std::bad_alloc&){
                    if(replacement.value)changeVertex(replacement.value,nullptr,0);
                    vertices_.erase(v);
                    return {};
                }
            }
            if(!v->second.value)return {};

            auto& result=pixels_[pixel][sources_-1];
            if(!result.known){
                try{
                    std::vector<uint32_t> code,output;
                    NorthlightTerrainCoverage::PixelInfo proof;
                    if(read(pixel,code)&&NorthlightTerrainCoverage::pixel(code.data(),code.size(),output,proof,sources_)){
                        result.shader.created(d->CreatePixelShader(reinterpret_cast<const DWORD*>(output.data()),&result.shader.value));
                        if(result.shader.value)changePixel(result.shader.value,output.data(),output.size());
                        result.constants=proof.constants;
                    }
                    if(coverageReports_++<8)
                        log("TERRAIN SHADOW farCoverage=%u sources=%u psConstants=%u addedSlots=%u",
                            unsigned(result.shader.value!=nullptr),sources_,result.constants,proof.addedSlots);
                    result.known=true;
                }catch(const std::bad_alloc&){
                    if(result.shader.value)changePixel(result.shader.value,nullptr,0);
                    result.shader.clear();
                    return {};
                }
            }
            if(!result.shader.value)return {};

            if(!rowsValid_||std::memcmp(view_,inverseView,64)){
                for(unsigned i=0;i<sources_;++i){
                    float projected[16];
                    NorthlightWorldMath::replayProjection(inverseView,matrices_[i],projected);
                    std::memcpy(rows_+i*12,projected,48);
                }
                std::memcpy(view_,inverseView,64);
                rowsValid_=true;
            }
            return {v->second.value,result.shader.value,result.constants,sources_*3,rows_};
        }catch(const std::bad_alloc&){return {};}
    }
};
