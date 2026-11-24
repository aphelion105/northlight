#pragma once
#include "device_mirror.h"
#include "game_particle_atlas.h"
#include "ordered_particle_indices.h"
#include "particle_texture_pair_shader.h"
#include "shader_read_set.h"
#include "tracked_buffers.h"
#include "vertex_declaration_cache.h"
#include <unordered_map>

class NorthlightGameParticlePair {
public:
    struct Coverage {
        void* owner;
        HRESULT(*run)(void*,unsigned,IDirect3DPixelShader9*,void*,HRESULT(*)(void*));
        unsigned mode;
        bool operator==(const Coverage& other)const{return owner==other.owner&&run==other.run&&mode==other.mode;}
    };
private:
    using Draw=NorthlightOrderedParticles::Draw;
    using Window=NorthlightOrderedParticles::Window;
    ExtensionDevice& device;
    DeviceMirror& mirror;
    const std::unordered_map<void*,NorthlightShaderReads::ReadSet>& shaderReads;
    NorthlightVertexDeclarations::Cache declarations;
    std::unique_ptr<NorthlightGameParticleAtlas> atlas;
    struct TexturePairProgram {
        template<class T> using Ref=NorthlightDrawSnapshot::Ref<T>;
        Ref<IDirect3DVertexShader9> vertex,sourceVertex;
        Ref<IDirect3DPixelShader9> pixel,sourcePixel;
        Ref<IDirect3DVertexDeclaration9> layout,sourceLayout;
        unsigned stride=0,sampler=0,semantic=0,selector=0;
    };
    std::array<TexturePairProgram,8> texturePairPrograms;
    const bool texturePairEnabled;
    TexturePairProgram* routed=nullptr;
    NorthlightGameParticleAtlas::Selection selected{};
    NorthlightGameParticleAtlas::Transform transform{};
    NorthlightDrawSnapshot::Ref<IDirect3DBaseTexture9> originalTexture;
    DeviceMirror::Stream source{};
    const void* atlasBuffer=nullptr;
    unsigned atlasUv=0;
    bool textureChanged=false,sameTexturePair=false;
    static constexpr unsigned MaxRows=NorthlightParticleTexturePair::MaxConstantRows;
    struct ConstantProgram {
        const TexturePairProgram* source=nullptr;
        unsigned count=0,bank=0;
        std::array<unsigned,MaxRows> rows{};
        NorthlightDrawSnapshot::Ref<IDirect3DVertexShader9> vertex;
    };
    std::array<ConstantProgram,32> constantPrograms;
    ConstantProgram* constantProgram=nullptr;
    float firstConstants[MaxRows][4]={};
    Coverage coverage{};
    Window window{};
    bool armed=false,pending=false;
    std::uint64_t state=0;
    std::uint64_t indexIdentity=0,indexRevision=0;
    unsigned checkedQuads=0,validQuads=0;
    std::vector<unsigned char> indexUpload;
    D3DCAPS9 caps{};
    const NorthlightShaderReads::ReadSet* vertexReads=nullptr;
    unsigned firstUncheckedConstant=0;

    HRESULT draw(const Draw& args){
        return device.MirrorDevice::DrawIndexedPrimitive(D3DPT_TRIANGLELIST,args.base,0,args.vertices,0,args.primitives);
    }
    template<class Paint> HRESULT paint(Paint fn,IDirect3DPixelShader9* pixel=nullptr){
        return coverage.run?coverage.run(coverage.owner,coverage.mode,pixel,&fn,[](void* data){return (*static_cast<Paint*>(data))();}):fn();
    }
    bool indices(IDirect3DIndexBuffer9* buffer,unsigned quads){
        NorthlightCaptureMetadata::Info index;
        const NorthlightCaptureMetadata::Request request{buffer,true};
        NorthlightTrackedBuffers::captureMetadata(&request,&index,1);
        constexpr unsigned quadBytes=6*sizeof(std::uint16_t);
        if(!index.identity||!index.revision||index.format!=D3DFMT_INDEX16||quads>index.size/quadBytes)return false;
        if(index.identity!=indexIdentity||index.revision!=indexRevision){
            indexIdentity=index.identity;
            indexRevision=index.revision;
            checkedQuads=validQuads=0;
        }
        if(quads<=checkedQuads||validQuads<checkedQuads)return quads<=validQuads;
        NorthlightReplayCopies::Slot* slot=nullptr;
        const auto* bytes=NorthlightReplayCopies::read(buffer,0,quadBytes*quads,slot,true);
        if(bytes){
            validQuads=NorthlightOrderedParticles::quadPrefix(bytes,quads);
            NorthlightReplayCopies::unpin(*slot);
        }else{
            if(!atlas)return false;
            try{indexUpload.resize(quadBytes*quads);}catch(...){return false;}
            if(!NorthlightParticleUploads::cache().read(buffer,0,quadBytes*quads,indexUpload.data())){
                D3DINDEXBUFFER_DESC desc{};
                void* mapped=nullptr;
                if(FAILED(buffer->GetDesc(&desc))||desc.Pool!=D3DPOOL_MANAGED||
                   FAILED(buffer->Lock(0,quadBytes*quads,&mapped,D3DLOCK_READONLY)))return false;
                if(mapped)std::memcpy(indexUpload.data(),mapped,quadBytes*quads);
                if(FAILED(buffer->Unlock())||!mapped)return false;
            }
            validQuads=NorthlightOrderedParticles::quadPrefix(indexUpload.data(),quads);
        }
        checkedQuads=quads;
        return quads==validQuads;
    }
    const NorthlightShaderReads::ReadSet* reads(void* shader)const{
        const auto it=shaderReads.find(shader);
        return it!=shaderReads.end()&&it->second.exact?&it->second:nullptr;
    }
    template<class Shader> static bool readProgram(Shader* shader,std::vector<std::uint32_t>& code){
        UINT bytes=0;
        if(FAILED(shader->GetFunction(nullptr,&bytes))||!bytes||bytes%4||bytes>16384)return false;
        code.resize(bytes/4);
        return SUCCEEDED(shader->GetFunction(code.data(),&bytes));
    }
    bool readsRow(unsigned row)const{return vertexReads&&(vertexReads->f[row/64]&(1ull<<(row%64)));}
    const float* readRow(unsigned row,float* fallback){
        if(mirror.vsFloatKnown[row])return mirror.vsFloat[row];
        return SUCCEEDED(device.GetVertexShaderConstantF(row,fallback,1))?fallback:nullptr;
    }
    bool prefersRoutedConstants(){
        if(!texturePairEnabled||!window.nextConstants||!vertexReads)return false;
        const unsigned end=window.firstConstant+window.constantCount;
        for(;firstUncheckedConstant<end;++firstUncheckedConstant){
            const unsigned row=firstUncheckedConstant;
            if(!readsRow(row))continue;
            float live[4];
            const auto* old=readRow(row,live);
            if(!old||std::memcmp(old,window.nextConstants+4*(row-window.firstConstant),sizeof live))return true;
        }
        return false;
    }
    ConstantProgram* constantsFor(const unsigned* rows,unsigned count)try{
        ConstantProgram* entry=nullptr;
        for(auto& program:constantPrograms){
            if(program.source==routed&&program.count==count&&std::equal(rows,rows+count,program.rows.begin()))
                return program.vertex.p?&program:nullptr;
            if(!entry&&!program.source)entry=&program;
        }
        if(!entry)return nullptr;
        entry->source=routed;
        entry->count=count;
        std::copy_n(rows,count,entry->rows.begin());
        std::vector<std::uint32_t> code,patched;
        if(!readProgram(routed->vertex.p,code)||
           !NorthlightParticleTexturePair::patchVertexConstants(code,routed->selector,rows,count,patched,entry->bank)||
           entry->bank+count>=caps.MaxVertexShaderConst||
           FAILED(device.real->CreateVertexShader(reinterpret_cast<const DWORD*>(patched.data()),entry->vertex.out())))return nullptr;
        return entry;
    }catch(...){return nullptr;}
    bool usesAtlas(IDirect3DPixelShader9* ps,IDirect3DVertexDeclaration9* declaration,
                   IDirect3DBaseTexture9* first,IDirect3DBaseTexture9* second){
        DWORD address=0,mag=0,min=0,mip=0,level=0,wrap=0,anisotropy=0;
        if(FAILED(device.GetSamplerState(0,D3DSAMP_ADDRESSU,&address))||
           FAILED(device.GetSamplerState(0,D3DSAMP_MAGFILTER,&mag))||
           FAILED(device.GetSamplerState(0,D3DSAMP_MINFILTER,&min))||
           FAILED(device.GetSamplerState(0,D3DSAMP_MIPFILTER,&mip))||
           FAILED(device.GetSamplerState(0,D3DSAMP_MAXMIPLEVEL,&level))||
           FAILED(device.GetSamplerState(0,D3DSAMP_MAXANISOTROPY,&anisotropy))||
           FAILED(device.GetRenderState(D3DRS_WRAP0,&wrap))||wrap||level||
           (address!=D3DTADDRESS_WRAP&&address!=D3DTADDRESS_CLAMP)||
           (mag!=D3DTEXF_POINT&&mag!=D3DTEXF_LINEAR&&mag!=D3DTEXF_ANISOTROPIC)||
           (min!=D3DTEXF_POINT&&min!=D3DTEXF_LINEAR&&min!=D3DTEXF_ANISOTROPIC)||mip>D3DTEXF_LINEAR||anisotropy>16)
            return false;
        IDirect3DVertexShader9* vs=nullptr;
        bool transformed=false;
        const int UV=device.peekVertexShader(vs)?atlas->uvOffset(vs,ps,declaration,declarations,source.stride,transformed):-1;
        if(UV<0){++atlasShaderMisses;return false;}
        const auto candidate=atlas->material(first,second,address==D3DTADDRESS_WRAP);
        if(!candidate.texture){++atlasMaterialMisses;return false;}
        transform={};
        if(transformed&&!transform.read(device)){++atlasTransformMisses;return false;}
        atlasBuffer=source.buffer;
        NorthlightParticleUploads::atlasUploads().watch(atlasBuffer);
        atlasUv=unsigned(UV);
        if(!copyAtlasVertices(window.first,candidate,candidate.firstTile,0)){++atlasVertexMisses;return false;}
        selected=candidate;
        ++atlasPrepared;
        return true;
    }
    TexturePairProgram* texturePairProgram(IDirect3DVertexShader9* vs,IDirect3DPixelShader9* ps,
                                           IDirect3DVertexDeclaration9* decl)try{
        TexturePairProgram* entry=nullptr;
        for(auto& program:texturePairPrograms){
            if(program.sourceVertex.p==vs&&program.sourcePixel.p==ps&&program.sourceLayout.p==decl&&program.stride==source.stride)
                return program.layout.p?&program:nullptr;
            if(!entry&&!program.sourceVertex.p)entry=&program;
        }
        if(!entry||source.stride+4>caps.MaxStreamStride||source.stride%4)return nullptr;
        vs->AddRef();entry->sourceVertex.p=vs;
        ps->AddRef();entry->sourcePixel.p=ps;
        decl->AddRef();entry->sourceLayout.p=decl;entry->stride=source.stride;
        unsigned texcoords=0;
        const D3DVERTEXELEMENT9* elements=nullptr;
        UINT count=0;
        if(!declarations.get(decl,elements,count)||count>MAXD3DDECLLENGTH)return nullptr;
        for(unsigned i=0;i+1<count;++i)if(elements[i].Usage==D3DDECLUSAGE_TEXCOORD)
            texcoords|=1u<<(elements[i].UsageIndex&15);
        std::vector<std::uint32_t> vsCode,psCode,vertex,pixel;
        NorthlightParticleTexturePair::Registers registers;
        if(!readProgram(vs,vsCode)||!readProgram(ps,psCode)||!NorthlightParticleTexturePair::patch(vsCode,psCode,texcoords,vertex,pixel,registers))
            return nullptr;
        std::array<D3DVERTEXELEMENT9,MAXD3DDECLLENGTH+1> layout{};
        std::copy_n(elements,count-1,layout.data());
        layout[count-1]={0,WORD(source.stride),D3DDECLTYPE_FLOAT1,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,BYTE(registers.semantic)};
        layout[count]=elements[count-1];
        auto* raw=device.real;
        if(FAILED(raw->CreateVertexShader(reinterpret_cast<const DWORD*>(vertex.data()),entry->vertex.out()))||
           FAILED(raw->CreatePixelShader(reinterpret_cast<const DWORD*>(pixel.data()),entry->pixel.out()))||
           FAILED(raw->CreateVertexDeclaration(layout.data(),entry->layout.out())))return nullptr;
        entry->sampler=registers.sampler;
        entry->semantic=registers.semantic;
        entry->selector=registers.input;
        return entry;
    }catch(...){return nullptr;}
    bool copyRoutedVertices(const Draw& args,unsigned begin,float selector){
        return atlas->copyTexturePairVertices(source.buffer,source.offset+std::uint64_t(args.base)*source.stride,
                                              args.vertices,source.stride,begin,selector);
    }
    bool usesTexturePair(IDirect3DPixelShader9* ps,IDirect3DVertexDeclaration9* declaration,
                         IDirect3DBaseTexture9* first,IDirect3DBaseTexture9* second){
        if(!texturePairEnabled||first->GetType()!=D3DRTYPE_TEXTURE||second->GetType()!=D3DRTYPE_TEXTURE)return false;
        DWORD bias=0,wrap=0;
        IDirect3DVertexShader9* vs=nullptr;
        if(FAILED(device.GetSamplerState(0,D3DSAMP_MIPMAPLODBIAS,&bias))||bias||!device.peekVertexShader(vs))return false;
        auto* program=texturePairProgram(vs,ps,declaration);
        if(!program||FAILED(device.GetRenderState(D3DRENDERSTATETYPE(D3DRS_WRAP0+program->semantic),&wrap))||wrap)return false;
        atlasBuffer=source.buffer;
        NorthlightParticleUploads::atlasUploads().watch(atlasBuffer);
        if(!copyRoutedVertices(window.first,0,-1.f)){++atlasVertexMisses;return false;}
        routed=program;
        return true;
    }
    bool usesTextures(IDirect3DPixelShader9* ps,IDirect3DVertexDeclaration9* declaration){
        if(!atlas||window.textureStage!=0||!window.nextTexture||!ps)return false;
        NorthlightDrawSnapshot::Ref<IDirect3DBaseTexture9> first;
        if(FAILED(device.GetTexture(0,first.out()))||!first.p)return false;
        auto* second=static_cast<IDirect3DBaseTexture9*>(const_cast<void*>(window.nextTexture));
        if(first.p==second)return window.first.follows(window.second);
        if(prefersRoutedConstants()){
            if(!usesTexturePair(ps,declaration,first.p,second)&&!usesAtlas(ps,declaration,first.p,second))return false;
        }else if(!usesAtlas(ps,declaration,first.p,second)&&!usesTexturePair(ps,declaration,first.p,second))return false;
        std::swap(originalTexture.p,first.p);
        return true;
    }
    bool copyAtlasVertices(const Draw& draw,const NorthlightGameParticleAtlas::Selection& material,unsigned tile,unsigned begin){
        return atlas->copyVertices(source.buffer,source.offset+std::uint64_t(draw.base)*source.stride,
                                   draw.vertices,source.stride,atlasUv,tile,material.widthScale,begin,transform);
    }
    HRESULT drawFirst(){
        return paint([&]{
            if(!constantProgram)return drawOriginalTexture();
            // The setter has already run. Restore the first emitter's rows
            // only for its physical draw, then restore the actual live rows.
            auto* raw=device.real;
            const auto& program=*constantProgram;
            float live[MaxRows][4];
            for(unsigned i=0;i<program.count;++i)
                if(FAILED(raw->GetVertexShaderConstantF(program.rows[i],live[i],1)))return E_FAIL;
            bool ok=true;
            unsigned changed=0;
            for(;changed<program.count&&ok;++changed)
                ok=SUCCEEDED(raw->SetVertexShaderConstantF(program.rows[changed],firstConstants[changed],1));
            const HRESULT hr=ok?drawOriginalTexture():E_FAIL;
            while(changed){
                --changed;
                ok=SUCCEEDED(raw->SetVertexShaderConstantF(program.rows[changed],live[changed],1))&&ok;
            }
            return ok?hr:E_FAIL;
        });
    }
    HRESULT drawOriginalTexture(){
        if(!textureChanged)return draw(window.first);
        textureChanged=false;
        NorthlightDrawSnapshot::Ref<IDirect3DBaseTexture9> live;
        auto* raw=device.real;
        if(FAILED(raw->GetTexture(0,live.out())))return E_FAIL;
        if(live.p==originalTexture.p)return draw(window.first);
        if(FAILED(raw->SetTexture(0,originalTexture.p)))return E_FAIL;
        const HRESULT hr=draw(window.first);
        return FAILED(raw->SetTexture(0,live.p))?E_FAIL:hr;
    }
    HRESULT drawAtlas(const Draw& combined,bool& drawn){
        const auto& a=window.first;
        if(!textureChanged||!copyAtlasVertices(window.second,selected,selected.secondTile,a.vertices*source.stride)){
            ++atlasVertexMisses;return S_FALSE;
        }
        UINT offset=0;
        auto* stream=atlas->upload(offset);
        if(!stream)return S_FALSE;
        return paint([&]{
            auto* raw=device.real;
            const auto textureSet=raw->SetTexture(0,selected.texture);
            const auto streamSet=SUCCEEDED(textureSet)?raw->SetStreamSource(0,stream,offset,source.stride):E_FAIL;
            HRESULT hr=S_FALSE;
            if(SUCCEEDED(streamSet)){
                hr=draw({0,combined.vertices,combined.primitives});
                drawn=SUCCEEDED(hr);
            }
            const bool streamRestored=SUCCEEDED(raw->SetStreamSource(0,source.buffer,source.offset,source.stride));
            const bool textureRestored=SUCCEEDED(raw->SetTexture(0,static_cast<IDirect3DBaseTexture9*>(const_cast<void*>(window.nextTexture))));
            return streamRestored&&textureRestored?(drawn?hr:S_FALSE):E_FAIL;
        });
    }
    HRESULT drawTexturePair(const Draw& combined,bool& drawn){
        if((!sameTexturePair&&!textureChanged)||
           !copyRoutedVertices(window.second,window.first.vertices*(source.stride+4),1.f))return S_FALSE;
        const DWORD stage=routed->sampler;
        constexpr unsigned states=D3DSAMP_DMAPOFFSET-1;
        const auto sampler=[](unsigned i){return D3DSAMPLERSTATETYPE(i+1);};
        DWORD live[states]{},native[states]{};
        auto readSampler=[&](DWORD slot,DWORD* values){
            if(device.knownSamplerStates(slot,(1u<<states)-1,values))return true;
            for(unsigned i=0;i<states;++i)if(FAILED(device.GetSamplerState(slot,sampler(i),values+i)))return false;
            return true;
        };
        NorthlightDrawSnapshot::Ref<IDirect3DBaseTexture9> oldTexture;
        if(!sameTexturePair&&(!readSampler(0,native)||!readSampler(stage,live)||
           FAILED(device.GetTexture(stage,oldTexture.out()))))return S_FALSE;
        UINT offset=0;
        auto* stream=atlas->upload(offset);
        if(!stream)return S_FALSE;
        auto* raw=device.real;
        unsigned changed=0;
        bool ready=true;
        for(unsigned i=0;i<states&&ready&&!sameTexturePair;++i)if(live[i]!=native[i]){
            changed|=1u<<i;
            ready=SUCCEEDED(raw->SetSamplerState(stage,sampler(i),native[i]));
        }
        float savedBank[MaxRows][4];
        bool bankChanged=false;
        if(ready&&constantProgram){
            const auto& program=*constantProgram;
            bankChanged=SUCCEEDED(device.GetVertexShaderConstantF(program.bank,savedBank[0],program.count));
            ready=bankChanged&&SUCCEEDED(raw->SetVertexShaderConstantF(program.bank,firstConstants[0],program.count));
        }
        const bool bindings=ready;
        auto* vertex=constantProgram?constantProgram->vertex.p:routed->vertex.p;
        auto* pixel=sameTexturePair?nullptr:routed->pixel.p;
        HRESULT hr=S_FALSE;
        if(ready&&(sameTexturePair||SUCCEEDED(raw->SetTexture(stage,originalTexture.p)))&&
           SUCCEEDED(raw->SetVertexDeclaration(routed->layout.p))&&SUCCEEDED(raw->SetVertexShader(vertex))&&
           SUCCEEDED(raw->SetStreamSource(0,stream,offset,source.stride+4))&&
           (!pixel||coverage.run||SUCCEEDED(device.SetPixelShader(pixel)))){
            hr=paint([&]{
                const HRESULT result=draw({0,combined.vertices,combined.primitives});
                drawn=SUCCEEDED(result);
                return result;
            },pixel);
        }
        bool restored=true;
        if(bindings){
            if(pixel&&(!coverage.run||(FAILED(hr)&&!drawn)))restored=SUCCEEDED(device.SetPixelShader(routed->sourcePixel.p));
            restored=SUCCEEDED(raw->SetVertexShader(routed->sourceVertex.p))&&restored;
            restored=SUCCEEDED(raw->SetVertexDeclaration(routed->sourceLayout.p))&&restored;
            restored=SUCCEEDED(raw->SetStreamSource(0,source.buffer,source.offset,source.stride))&&restored;
            if(!sameTexturePair)restored=SUCCEEDED(raw->SetTexture(stage,oldTexture.p))&&restored;
        }
        for(unsigned i=0;i<states;++i)if(changed&(1u<<i))
            restored=SUCCEEDED(raw->SetSamplerState(stage,sampler(i),live[i]))&&restored;
        if(bankChanged)restored=SUCCEEDED(raw->SetVertexShaderConstantF(constantProgram->bank,savedBank[0],constantProgram->count))&&restored;
        return restored?(drawn?hr:S_FALSE):E_FAIL;
    }
    bool compatible(){
        if(!mirror.active()||!mirror.programmableVS||!mirror.renderStateKnown[D3DRS_ALPHABLENDENABLE]||
           !mirror.renderStates[D3DRS_ALPHABLENDENABLE]||!mirror.renderStateKnown[D3DRS_ZWRITEENABLE]||
           mirror.renderStates[D3DRS_ZWRITEENABLE])return false;
        IDirect3DPixelShader9* shader=nullptr;
        bool textureBarrier=false;
        if(window.textureStage!=~0u){
            const auto* pixel=device.peekPixelShader(shader)?reads(shader):nullptr;
            textureBarrier=!pixel||(pixel->sampledSamplers&(1u<<window.textureStage));
            if(textureBarrier&&!atlas){++textureBarriers;return false;}
        }
        // Only the atlas path packs two separate vertex ranges into one stream.
        if(!textureBarrier&&!window.first.follows(window.second))return false;
        if(window.constantCount){
            IDirect3DVertexShader9* shader=nullptr;
            vertexReads=device.peekVertexShader(shader)?reads(shader):nullptr;
            if(!vertexReads)return false;
        }
        DWORD stencil=TRUE;
        if(FAILED(device.GetRenderState(D3DRS_STENCILENABLE,&stencil))||stencil)return false;
        UINT frequency=0;
        if(FAILED(device.GetStreamSourceFreq(0,&frequency))||frequency!=1||
           device.GetSoftwareVertexProcessing()||device.GetNPatchMode()!=0)return false;
        const auto& stream=mirror.streams[0];
        if(!stream.known||!stream.buffer||!stream.stride)return false;
        if(window.uploadBuffer&&(window.uploadBuffer!=stream.buffer||
           !NorthlightOrderedParticles::disjointUpload(window.first,stream.offset,stream.stride,window.uploadOffset,window.uploadBytes)))return false;
        IDirect3DVertexDeclaration9* declaration=nullptr;
        UINT extent[4];
        if(!device.peekVertexDeclaration(declaration)||!declarations.captureLayout(declaration,extent)||
           extent[0]>stream.stride||extent[1]||extent[2]||extent[3]||!mirror.indicesKnown||!mirror.indices)return false;
        if(!indices(mirror.indices,(window.first.primitives+window.second.primitives)/2)){++indexMisses;return false;}
        if(textureBarrier){
            source=stream;
            if(!usesTextures(shader,declaration)){++textureBarriers;return false;}
        }else if(window.textureStage==~0u&&prefersRoutedConstants()&&device.peekPixelShader(shader)){
            NorthlightDrawSnapshot::Ref<IDirect3DBaseTexture9> texture;
            if(SUCCEEDED(device.GetTexture(0,texture.out()))&&texture.p){
                source=stream;
                sameTexturePair=usesTexturePair(shader,declaration,texture.p,texture.p);
            }
        }
        return true;
    }

public:
    unsigned windows=0,saved=0,drained=0,proofMisses=0,textureBarriers=0,indexMisses=0;
    unsigned atlasSaved=0,atlasVertexMisses=0,atlasShaderMisses=0,atlasMaterialMisses=0,atlasPrepared=0,atlasTransformMisses=0;
    unsigned routedSaved=0,constantSaved=0,constantMisses=0;
    NorthlightGameParticlePair(ExtensionDevice& device,DeviceMirror& mirror,
                               const std::unordered_map<void*,NorthlightShaderReads::ReadSet>& reads,bool textureAtlas=false,bool texturePair=false)
        :device(device),mirror(mirror),shaderReads(reads),texturePairEnabled(texturePair){
        if(FAILED(device.GetDeviceCaps(&caps)))caps={};
        if(textureAtlas)atlas=std::make_unique<NorthlightGameParticleAtlas>(device.real);
    }
    ~NorthlightGameParticlePair(){NorthlightParticleUploads::atlasUploads().forget(atlasBuffer);}
    unsigned atlasBuilt()const{return atlas?atlas->built:0;}
    bool atlasEnabled()const{return bool(atlas);}
    void resetAtlas(){
        if(!atlas)return;
        NorthlightParticleUploads::atlasUploads().forget(atlasBuffer);
        atlasBuffer=nullptr;
        atlas->reset();
    }
    void nextFrame(){
        if(atlas)atlas->nextFrame();
        atlasSaved=atlasVertexMisses=atlasShaderMisses=atlasMaterialMisses=atlasPrepared=atlasTransformMisses=0;
        routedSaved=constantSaved=constantMisses=0;
    }
    bool arm(const Window& next){
        const auto& a=next.first;
        const auto& b=next.second;
        if((!a.follows(b)&&(!atlas||!next.nextTexture||next.textureStage!=0||!a.quadList()||!b.quadList()))||
           a.primitives+b.primitives>caps.MaxPrimitiveCount||std::uint64_t(a.base)+a.vertices-1>caps.MaxVertexIndex||
           std::uint64_t(b.base)+b.vertices-1>caps.MaxVertexIndex)
            return false;
        window=next;
        vertexReads=nullptr;
        firstUncheckedConstant=next.firstConstant;
        selected={};routed=nullptr;constantProgram=nullptr;coverage={};textureChanged=sameTexturePair=false;
        ++windows;
        return armed=true;
    }
    bool allowsTexture(DWORD stage,IDirect3DBaseTexture9* texture){
        if(!pending||state!=mirror.mutations||window.textureStage!=stage)return false;
        if(selected.texture||routed){
            if(texture!=window.nextTexture)return false;
            textureChanged=true;
        }
        window.textureStage=~0u;
        ++state; // Exactly one accepted mirror setter may advance this stamp.
        return true;
    }
    bool allowsConstants(UINT first,const float* values,UINT count){
        if(!pending||state!=mirror.mutations||!values||!vertexReads||window.firstConstant!=first||
           !window.constantCount||window.constantCount!=count)return false;
        unsigned rows[MaxRows],changes=0;
        // The lookahead already checked this prefix against the same payload.
        // The mutation stamp above guards the state between the two reads.
        const unsigned unchecked=values==window.nextConstants?firstUncheckedConstant:first;
        for(UINT row=unchecked;row<first+count;++row){
            if(!readsRow(row))continue;
            float live[4];
            const auto* old=readRow(row,live);
            if(!old){++constantMisses;return false;}
            if(!std::memcmp(old,values+4*(row-first),sizeof live))continue;
            if(!routed||changes==MaxRows){++constantMisses;return false;}
            rows[changes]=row;
            std::memcpy(firstConstants[changes++],old,sizeof live);
        }
        if(changes&&!(constantProgram=constantsFor(rows,changes))){++constantMisses;return false;}
        window.constantCount=0;
        window.nextConstants=nullptr;
        ++state;
        return true;
    }
    HRESULT flush(){
        armed=false;
        if(!pending)return D3D_OK;
        pending=false;
        ++drained;
        return drawFirst();
    }
    // Capture work before the first native submission has nothing to drain.
    HRESULT barrier(){return pending?flush():D3D_OK;}
    template<class Submit> HRESULT submit(const Draw& args,bool allowed,Submit serial,Coverage plane={}){
        if(armed&&allowed){
            if(!pending&&args==window.first){
                if(compatible()){
                    coverage=plane;
                    state=mirror.mutations;
                    pending=true;
                    return D3D_OK;
                }
                ++proofMisses;
            }else if(pending&&args==window.second&&state==mirror.mutations&&coverage==plane){
                armed=pending=false;
                const auto& a=window.first;
                const auto& b=window.second;
                const Draw combined{a.base,a.vertices+b.vertices,a.primitives+b.primitives};
                if(selected.texture||routed){
                    bool drawn=false;
                    const HRESULT hr=routed?drawTexturePair(combined,drawn):drawAtlas(combined,drawn);
                    if(drawn){++saved;if(routed)++routedSaved;else ++atlasSaved;if(constantProgram)++constantSaved;return hr;}
                    if(FAILED(hr))return hr;
                }else{
                    const HRESULT hr=paint([&]{return draw(combined);});
                    if(SUCCEEDED(hr)){++saved;return hr;}
                }
                const HRESULT first=drawFirst();
                const HRESULT second=serial();
                return FAILED(first)?first:second;
            }
        }
        const HRESULT first=flush();
        const HRESULT next=serial();
        return FAILED(first)?first:next;
    }
};
