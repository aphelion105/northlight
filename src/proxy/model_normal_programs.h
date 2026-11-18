#pragma once
#include <d3d9.h>
#include <unordered_map>
#include "model_normal_patch.h"
#include "auto_bump_vertex_patch.h"
#include "model_normal_slice.h"
#include "native_material_capture_patch.h"
#include "shader_read_set.h"
#include "shader_tags.h"
#include "model_normal_range.h"

// Owns patched replay programs and the source references behind their lookup keys.
class NorthlightModelNormalPrograms {
public:
    struct Vertex {IDirect3DVertexShader9 *source=nullptr,*shader=nullptr;NorthlightShaderReads::ReadSet reads;unsigned first=0,count=256;NorthlightModelNormalRange::Kind rangeKind=NorthlightModelNormalRange::None;bool compact=false,terrain=false;std::uint64_t programHash=0;UINT programBytes=0;};
    struct Pixel {IDirect3DPixelShader9 *source=nullptr,*shader=nullptr;unsigned control=0,first=0,count=0;NorthlightShaderReads::ReadSet reads;bool compactVertex=false;Pixel* equivalent=nullptr;std::uint64_t programHash=0;UINT programBytes=0;};
private:
    IDirect3DDevice9* d;
    std::unordered_map<IDirect3DVertexShader9*,Vertex> vertices;
    // Optional slices never consume the original shader cache's admission.
    std::unordered_map<IDirect3DVertexShader9*,Vertex> compactVertices;
    std::unordered_map<IDirect3DPixelShader9*,Pixel> pixels[2];
    template<class T> static void release(T*& p){if(p)p->Release();p=nullptr;}
    template<class Shader> static bool exactCode(Shader* shader,const std::vector<std::uint32_t>& program){
        std::vector<DWORD> exact(program.size());const UINT bytes=UINT(program.size()*sizeof(std::uint32_t));UINT actual=bytes;
        return SUCCEEDED(shader->GetFunction(exact.data(),&actual))&&actual==bytes&&!std::memcmp(exact.data(),program.data(),bytes);
    }
public:
    unsigned compactBuilt=0,vertexBuilt=0,vertexReused=0,pixelBuilt=0,pixelReused=0;
#ifdef NORTHLIGHT_MODEL_NORMALS_TEST
    bool pixelSharingEnabled=true;
    bool pixelCanonicalEnabled=true;
#endif
    explicit NorthlightModelNormalPrograms(IDirect3DDevice9* device):d(device){}
    NorthlightModelNormalPrograms(const NorthlightModelNormalPrograms&)=delete;
    NorthlightModelNormalPrograms& operator=(const NorthlightModelNormalPrograms&)=delete;
    ~NorthlightModelNormalPrograms(){
        for(auto* map:{&vertices,&compactVertices})for(auto& v:*map){release(v.second.source);release(v.second.shader);}
        for(auto& map:pixels)for(auto& v:map){release(v.second.source);release(v.second.shader);}
    }
    Vertex* vertex(IDirect3DVertexShader9* source,bool compact=false,bool terrain=false){
        auto& cache=compact?compactVertices:vertices;
        auto it=cache.find(source);if(it!=cache.end())return compact&&!it->second.shader?vertex(source,false,terrain):&it->second;
        if(cache.size()>=(compact?64u:512u))return compact?vertex(source,false,terrain):nullptr;
        Vertex v;v.source=source;v.terrain=terrain;source->AddRef();UINT bytes=0;
        if(SUCCEEDED(source->GetFunction(nullptr,&bytes))&&bytes>=8&&bytes<=65536&&bytes%4==0){
            std::vector<DWORD> code(bytes/4);std::vector<std::uint32_t> patched;
            const auto* words=reinterpret_cast<const uint32_t*>(code.data());
            if(SUCCEEDED(source->GetFunction(code.data(),&bytes))&&(terrain?
                !NorthlightWaterShaderPatch::patch(words,code.size(),patched)&&NorthlightAutoBumpVertex::terrainNormal(words,code.size(),patched):
                NorthlightModelNormal::vertex(words,code.size(),patched))){
                if(!terrain)v.rangeKind=NorthlightModelNormalRange::classify(reinterpret_cast<const uint32_t*>(code.data()),code.size(),true);
                if(compact){std::vector<std::uint32_t> sliced;
                    if(!NorthlightModelNormalSlice::vertex(patched.data(),patched.size(),sliced)||sliced.size()>=patched.size()){
                        cache.emplace(source,v);return vertex(source,false,terrain);
                    }
                    std::vector<std::uint32_t> canonical;
                    if(NorthlightModelNormalSlice::canonicalVertex(sliced.data(),sliced.size(),canonical))sliced.swap(canonical);
                    patched.swap(sliced);v.compact=true;
                }
                v.reads=NorthlightShaderReads::scan(patched.data(),patched.size());
                if(v.reads.exact){
                    unsigned first=256,last=0;
                    for(unsigned c=0;c<256;++c)if(v.reads.f[c>>6]&(1ull<<(c&63))){first=std::min(first,c);last=c+1;}
                    v.first=last?first:0;v.count=last-v.first;
                }
                v.programBytes=UINT(patched.size()*sizeof(std::uint32_t));v.programHash=NorthlightShaderTags::fnv1a(patched.data(),v.programBytes);
                for(auto& item:cache){const auto& existing=item.second;
                    if(existing.shader&&existing.compact==v.compact&&existing.programBytes==v.programBytes&&existing.programHash==v.programHash&&exactCode(existing.shader,patched)){
                        v.shader=existing.shader;v.shader->AddRef();++vertexReused;break;}}
                if(!v.shader){const HRESULT hr=d->CreateVertexShader(reinterpret_cast<const DWORD*>(patched.data()),&v.shader);
                    if(compact&&FAILED(hr))release(v.shader);
                    if(v.shader){++vertexBuilt;compactBuilt+=v.compact;}}
            }
        }
        auto* result=&cache.emplace(source,v).first->second;
        return compact&&!result->shader?vertex(source,false,terrain):result;
    }
    Pixel* pixel(IDirect3DPixelShader9* source,bool alpha){
        auto& map=pixels[alpha];auto it=map.find(source);if(it!=map.end())return it->second.equivalent?it->second.equivalent:&it->second;if(map.size()>=512)return nullptr;
        Pixel v;v.source=source;source->AddRef();UINT bytes=0;
        if(SUCCEEDED(source->GetFunction(nullptr,&bytes))&&bytes>=8&&bytes<=65536&&bytes%4==0){
            std::vector<DWORD> code(bytes/4);std::vector<std::uint32_t> patched;
            if(SUCCEEDED(source->GetFunction(code.data(),&bytes))&&NorthlightNativeMaterialCapture::patch(reinterpret_cast<const uint32_t*>(code.data()),code.size(),patched,v.control,alpha,false,true)){
                // The original lighting can differ while receiver bytecode is
                // identical. Share an existing cache node before asking D3D to
                // create another program; no extra lookup on a warm source hit.
                const auto create=[&](const std::vector<std::uint32_t>& program,bool compact){
                    const UINT bytes=UINT(program.size()*sizeof(std::uint32_t));const auto hash=NorthlightShaderTags::fnv1a(program.data(),bytes);
#ifdef NORTHLIGHT_MODEL_NORMALS_TEST
                    if(pixelSharingEnabled)
#endif
                    for(auto& cache:pixels)for(auto& item:cache){auto& existing=item.second;
                        if(!existing.shader||existing.control!=v.control||existing.compactVertex!=compact||existing.programBytes!=bytes||existing.programHash!=hash)continue;
                        // Hashes only shortlist candidates. Ask the retained
                        // shader for exact bytes, without another bytecode cache.
                        if(exactCode(existing.shader,program)){
                            v.equivalent=&existing;++pixelReused;return true;}}
                    if(FAILED(d->CreatePixelShader(reinterpret_cast<const DWORD*>(program.data()),&v.shader))||!v.shader){release(v.shader);return false;}
                    v.programHash=hash;v.programBytes=bytes;v.compactVertex=compact;++pixelBuilt;return true;
                };
                std::vector<std::uint32_t> sliced,canonical;const auto originalControl=v.control;
                const bool compact=NorthlightModelNormalSlice::pixel(patched.data(),patched.size(),sliced);
                if(compact
#ifdef NORTHLIGHT_MODEL_NORMALS_TEST
                    &&pixelCanonicalEnabled
#endif
                    &&NorthlightModelNormalSlice::canonicalPixel(sliced.data(),sliced.size(),v.control,canonical)){sliced.swap(canonical);v.control=0;}
                if(compact&&create(sliced,true))patched.swap(sliced);
                else {v.control=originalControl;create(patched,false);}
                if(v.equivalent){auto* result=v.equivalent;map.emplace(source,std::move(v));return result;}
                v.reads=NorthlightShaderReads::scan(patched.data(),patched.size());
                v.reads.samplers=v.reads.sampledSamplers;
                unsigned last=v.control+1;v.first=v.control;
                for(unsigned c=0;c<224;++c)if(v.reads.f[c>>6]&(1ull<<(c&63))){v.first=std::min(v.first,c);last=std::max(last,c+1);}
                v.count=last-v.first;
            }
        }
        return &map.emplace(source,std::move(v)).first->second;
    }
};
