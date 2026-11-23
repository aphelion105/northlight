#pragma once
#include "model_normal_palette_shader.h"
#include "draw_snapshot.h"
#include "upload_lock.h"
#include "vertex_declaration_cache.h"

class NorthlightModelNormalPalette {
    using Shader=IDirect3DVertexShader9;
    using Layout=IDirect3DVertexDeclaration9;
    using Buffer=IDirect3DVertexBuffer9;
    using Mesh=NorthlightDrawSnapshot::Mesh;
    template<class T> using Ref=NorthlightDrawSnapshot::Ref<T>;
    static constexpr unsigned MaxBones=NorthlightModelNormalPaletteShader::MaxBones;

public:
    struct Program {
        Ref<Shader> source;
        Ref<Shader> instanced;
        Ref<IDirect3DPixelShader9> materialPixel;
        unsigned semantic=0,limit=0,sourceLimit=0;
        std::vector<NorthlightModelNormalPaletteShader::ConstantRun> fixedReads;
        std::vector<NorthlightModelNormalPaletteShader::ConstantRun> instanceReads;
        unsigned instanceRows=0,biasConstant=256;
        bool gameConstants=false;
    };
    struct Declaration {Ref<Layout> source,instanced; unsigned semantic=0;};
    struct Span {std::uint8_t first=0,count=0;};

private:
    struct CachedSpan {NorthlightSourceIdentity::Handle key; unsigned offset=0; Span span;};
    IDirect3DDevice9* d;
    std::array<Program,32> programs{};
    std::array<Declaration,24> layouts{};
    std::array<CachedSpan,128> spans{};
    unsigned programCount=0,layoutCount=0,maxStride=0;
    Ref<Buffer> ids;

    template<class T> static void drop(Ref<T>& ref){if(ref.p)ref.p->Release();ref.p=nullptr;}
    template<class T> static bool bytecode(T* shader,std::vector<DWORD>& code){
        UINT bytes=0;
        if(FAILED(shader->GetFunction(nullptr,&bytes))||bytes<8||bytes>65536||bytes%4)return false;
        code.resize(bytes/4);
        UINT actual=bytes;
        return SUCCEEDED(shader->GetFunction(code.data(),&actual))&&actual==bytes;
    }

public:
    bool unavailable=false;
    explicit NorthlightModelNormalPalette(IDirect3DDevice9* device):d(device){}
    NorthlightModelNormalPalette(const NorthlightModelNormalPalette&)=delete;
    NorthlightModelNormalPalette& operator=(const NorthlightModelNormalPalette&)=delete;

    void reset(){
        drop(ids);
        for(unsigned i=0;i<programCount;++i){drop(programs[i].instanced);drop(programs[i].materialPixel);drop(programs[i].source);}
        for(unsigned i=0;i<layoutCount;++i){drop(layouts[i].instanced);drop(layouts[i].source);}
        spans={};
        programCount=layoutCount=maxStride=0;
        unavailable=false;
    }

    const Program* program(Shader* original,IDirect3DPixelShader9* fogDisabledMaterialPixel=nullptr,bool gameConstants=false)try{
        for(unsigned i=0;i<programCount;++i)if(programs[i].source.p==original&&programs[i].materialPixel.p==fogDisabledMaterialPixel&&
            programs[i].gameConstants==gameConstants)
            return programs[i].instanced.p?&programs[i]:nullptr;
        if(programCount==programs.size())return nullptr;
        auto& entry=programs[programCount++];
        entry.gameConstants=gameConstants;
        original->AddRef();entry.source.p=original;
        if(fogDisabledMaterialPixel){fogDisabledMaterialPixel->AddRef();entry.materialPixel.p=fogDisabledMaterialPixel;}
        std::vector<DWORD> code;
        if(!bytecode(original,code))return nullptr;
        NorthlightModelNormalPaletteShader::MaterialVaryings varyings;
        const NorthlightModelNormalPaletteShader::MaterialVaryings* linked=nullptr;
        if(fogDisabledMaterialPixel){
            std::vector<DWORD> ps;
            if(bytecode(fogDisabledMaterialPixel,ps)&&NorthlightModelNormalPaletteShader::materialVaryings(
                reinterpret_cast<const std::uint32_t*>(ps.data()),ps.size(),varyings))linked=&varyings;
        }
        NorthlightModelNormalPaletteShader::Program proof;
        const auto* originalCode=reinterpret_cast<const std::uint32_t*>(code.data());
        using NorthlightModelNormalPaletteShader::Bias;
        if(!NorthlightModelNormalPaletteShader::build(originalCode,code.size(),proof,Bias::Folded,linked,gameConstants))return nullptr;
        auto hr=d->CreateVertexShader(reinterpret_cast<const DWORD*>(proof.words.data()),entry.instanced.out());
        if(hr==D3DERR_INVALIDCALL){
            // Some D3D9 backends enforce the single input-register read port.
            drop(entry.instanced);
            if(!NorthlightModelNormalPaletteShader::build(originalCode,code.size(),proof,Bias::Separate,linked,gameConstants))return nullptr;
            hr=d->CreateVertexShader(reinterpret_cast<const DWORD*>(proof.words.data()),entry.instanced.out());
        }
        if(FAILED(hr))drop(entry.instanced);
        entry.semantic=proof.semantic;
        entry.limit=proof.limit;entry.sourceLimit=proof.sourceLimit;
        entry.fixedReads=std::move(proof.fixedReads);
        entry.instanceReads=std::move(proof.instanceReads);
        entry.instanceRows=proof.instanceRows;entry.biasConstant=proof.biasConstant;
        return entry.instanced.p?&entry:nullptr;
    }catch(const std::bad_alloc&){return nullptr;}

    static const D3DVERTEXELEMENT9* boneElement(const D3DVERTEXELEMENT9* elements,UINT count){
        const D3DVERTEXELEMENT9* indices=nullptr;
        for(UINT i=0;i+1<count;++i){
            const auto& e=elements[i];
            if(e.Stream)return nullptr;
            if(e.Usage!=D3DDECLUSAGE_BLENDINDICES||e.UsageIndex)continue;
            if(indices||e.Type!=D3DDECLTYPE_UBYTE4||e.Method!=D3DDECLMETHOD_DEFAULT)return nullptr;
            indices=&e;
        }
        return indices;
    }

    Layout* declaration(Layout* original,unsigned semantic,NorthlightVertexDeclarations::Cache& declarations){
        for(unsigned i=0;i<layoutCount;++i)if(layouts[i].source.p==original&&layouts[i].semantic==semantic)
            return layouts[i].instanced.p;
        if(layoutCount==layouts.size())return nullptr;
        const D3DVERTEXELEMENT9* elements=nullptr;
        UINT count=0;
        if(!declarations.get(original,elements,count)||count>MAXD3DDECLLENGTH||!boneElement(elements,count))return nullptr;
        auto& entry=layouts[layoutCount++];
        original->AddRef();
        entry.source.p=original;
        entry.semantic=semantic;
        std::array<D3DVERTEXELEMENT9,MAXD3DDECLLENGTH+1> layout;
        UINT length=0;
        for(UINT n=0;n+1<count;++n){
            const auto& e=elements[n];
            // The original VS never declares this semantic.
            if(e.Usage==D3DDECLUSAGE_TEXCOORD&&e.UsageIndex==semantic)continue;
            layout[length++]=e;
        }
        layout[length++]={1,0,D3DDECLTYPE_FLOAT1,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,BYTE(semantic)};
        layout[length]=elements[count-1];
        if(FAILED(d->CreateVertexDeclaration(layout.data(),entry.instanced.out())))drop(entry.instanced);
        return entry.instanced.p;
    }

    Span span(const Mesh& mesh,Layout* declaration,NorthlightVertexDeclarations::Cache& declarations){
        const auto& key=mesh.replayIdentity?mesh.replayIdentity:mesh.sourceIdentity;
        if(!key)return {};
        const D3DVERTEXELEMENT9* elements=nullptr;
        UINT count=0;
        if(!declarations.get(declaration,elements,count))return {};
        const auto* bone=boneElement(elements,count);
        if(!bone)return {};
        const unsigned offset=bone->Offset;
        auto& entry=spans[(key->hash^offset)&(spans.size()-1)];
        if(entry.key&&entry.offset==offset&&NorthlightSourceIdentity::same(key,entry.key))return entry.span;
        const auto& stream=mesh.streams[0];
        unsigned low=MaxBones,high=0;
        if(offset+4>stream.stride||mesh.indices.empty())return {};
        for(auto index:mesh.indices){
            const auto byte=std::uint64_t(index)*stream.stride+offset;
            if(index>=mesh.vertexCount||byte+4>stream.bytes.size())return {};
            for(unsigned lane=0;lane<4;++lane){
                const unsigned value=stream.bytes[size_t(byte)+lane];
                if(value>=MaxBones)return {};
                low=std::min(low,value);
                high=std::max(high,value);
            }
        }
        entry={key,offset,{std::uint8_t(low),std::uint8_t(high-low+1)}};
        return entry.span;
    }

    Buffer* instanceIds(unsigned stride){
        if(unavailable)return nullptr;
        if(!maxStride){
            D3DCAPS9 caps{};
            if(FAILED(d->GetDeviceCaps(&caps))||caps.VertexShaderVersion<D3DVS_VERSION(3,0)||
               caps.MaxStreams<2||caps.MaxStreamStride<sizeof(float)||caps.MaxVertexShaderConst<256){
                unavailable=true;
                return nullptr;
            }
            maxStride=caps.MaxStreamStride;
        }
        if(stride>maxStride)return nullptr;
        if(ids.p)return ids.p;
        constexpr unsigned count=MaxBones*2-1,bytes=count*sizeof(float);
        Ref<Buffer> buffer;
        void* data=nullptr;
        unavailable=true;
        if(FAILED(d->CreateVertexBuffer(bytes,D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,buffer.out(),nullptr))||!buffer.p||
           FAILED(buffer->Lock(0,bytes,&data,NorthlightUpload::FreshBufferLock)))return nullptr;
        if(data)for(int i=0;i<int(count);++i)static_cast<float*>(data)[i]=float((i-int(MaxBones-1))*3);
        if(FAILED(buffer->Unlock())||!data)return nullptr;
        ids.p=buffer.p;
        buffer.p=nullptr;
        unavailable=false;
        return ids.p;
    }
};
