// Production point-cube batching, source proof, fault fallback and paired timings.
#undef NDEBUG
#include "renderer.cpp"
#include "actor_client_programs.h"
#include <cassert>
#include <cstdio>
static void must(HRESULT hr){assert(SUCCEEDED(hr));}
struct PaletteFaults:ForwardIDirect3DDevice9 {
    using ForwardIDirect3DDevice9::ForwardIDirect3DDevice9;
    unsigned fault=0,hits=0;bool after=false;
    enum CasterStep {CasterVertex, CasterPixel};
    std::optional<CasterStep> casterFault;
    std::vector<CasterStep> casterSteps;
    enum Step {Layout, Vertex, Projection, PaletteOrigin, AlphaTexture, Cutoff, Vertices, Indices,
               Instances, InstanceRate, Pose, DrawRate, Draw, RestoreVertices, RestoreInstances};
    bool observe=false,observing=false;
    enum MixedFault {MixedOk,MixedAllocate,MixedCopy,MixedSeed,MixedDraw};
    MixedFault mixedFault=MixedOk;
    bool watchMixed=false,mixedTarget=false;
    unsigned mixedHits=0;
    std::vector<IDirect3DSurface9*> mixedTargets;
    IDirect3DVertexDeclaration9* paletteDeclaration=nullptr;
    std::vector<Step> steps;
    void record(Step step){if(observing)steps.push_back(step);}
    IDirect3DVertexBuffer9 *wrapped=nullptr,*wrappedRaw=nullptr;
    struct Buffer:ForwardIDirect3DVertexBuffer9 {
        PaletteFaults& owner;ULONG refs=1;
        Buffer(IDirect3DVertexBuffer9* value,PaletteFaults& o):ForwardIDirect3DVertexBuffer9(value),owner(o){}
        ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
        ULONG STDMETHODCALLTYPE Release()override{const auto n=--refs;if(!n){if(owner.wrapped==this)owner.wrapped=owner.wrappedRaw=nullptr;real->Release();delete this;}return n;}
        HRESULT STDMETHODCALLTYPE Lock(UINT offset,UINT size,void** out,DWORD flags)override{
            if(owner.fault!=4)return real->Lock(offset,size,out,flags);owner.fault=0;++owner.hits;
            if(owner.after)must(real->Lock(offset,size,out,flags));else *out=nullptr;return E_FAIL;
        }
        HRESULT STDMETHODCALLTYPE Unlock()override{
            if(owner.fault!=5)return real->Unlock();owner.fault=0;++owner.hits;if(owner.after)must(real->Unlock());return E_FAIL;
        }
    };
    HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD* code,IDirect3DVertexShader9** out)override{
        if(code==kLocalShadowVSShader){
            casterSteps.push_back(CasterVertex);
            if(casterFault==CasterVertex){casterFault.reset();*out=nullptr;return E_OUTOFMEMORY;}
        }
        if(fault!=1||code!=kLocalPaletteShadowVSShader)return real->CreateVertexShader(code,out);fault=0;++hits;
        if(after)must(real->CreateVertexShader(code,out));else *out=nullptr;return E_FAIL;
    }
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD* code,IDirect3DPixelShader9** out)override{
        if(code==kLocalShadowPSShader){
            casterSteps.push_back(CasterPixel);
            if(casterFault==CasterPixel){
                casterFault.reset();must(real->CreatePixelShader(code,out));return E_OUTOFMEMORY;
            }
        }
        return real->CreatePixelShader(code,out);
    }
    HRESULT STDMETHODCALLTYPE CreateVertexDeclaration(const D3DVERTEXELEMENT9* code,IDirect3DVertexDeclaration9** out)override{
        const bool palette=code[0].Stream!=0xff&&code[1].Stream!=0xff&&code[2].Stream==1;
        if(fault!=2||!palette){
            const auto hr=real->CreateVertexDeclaration(code,out);
            if(observe&&SUCCEEDED(hr)&&palette)paletteDeclaration=*out;
            return hr;
        }
        fault=0;++hits;
        if(after)must(real->CreateVertexDeclaration(code,out));else *out=nullptr;return E_FAIL;
    }
    HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT size,DWORD usage,DWORD fvf,D3DPOOL pool,IDirect3DVertexBuffer9** out,HANDLE* handle)override{
        const bool target=size==NorthlightShadowPalettePass::InstanceBytes&&usage==D3DUSAGE_WRITEONLY&&pool==D3DPOOL_DEFAULT;
        if(target&&fault==3){fault=0;++hits;if(after)must(real->CreateVertexBuffer(size,usage,fvf,pool,out,handle));else *out=nullptr;return E_FAIL;}
        const auto hr=real->CreateVertexBuffer(size,usage,fvf,pool,out,handle);if(target&&SUCCEEDED(hr)){wrappedRaw=*out;wrapped=new Buffer(*out,*this);*out=wrapped;}return hr;
    }
    void resetMixedFault(){
        mixedTargets.clear();mixedTarget=false;
    }
    bool mixedSurface(IDirect3DSurface9* surface)const{
        return std::find(mixedTargets.begin(),mixedTargets.end(),surface)!=mixedTargets.end();
    }
    HRESULT STDMETHODCALLTYPE CreateTexture(UINT w,UINT h,UINT levels,DWORD usage,D3DFORMAT format,D3DPOOL pool,
                                             IDirect3DTexture9** out,HANDLE* shared)override{
        const bool mixed=watchMixed&&w==256&&h==256&&format==D3DFMT_R32F&&usage==D3DUSAGE_RENDERTARGET;
        if(mixed&&mixedFault==MixedAllocate){*out=nullptr;mixedFault=MixedOk;++mixedHits;return E_OUTOFMEMORY;}
        const auto hr=real->CreateTexture(w,h,levels,usage,format,pool,out,shared);
        if(mixed&&SUCCEEDED(hr)){
            NorthlightDrawSnapshot::Ref<IDirect3DSurface9> surface;
            must((*out)->GetSurfaceLevel(0,surface.out()));
            mixedTargets.push_back(surface.p);
        }
        return hr;
    }
    HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD slot,IDirect3DSurface9* surface)override{
        const auto hr=real->SetRenderTarget(slot,surface);
        if(!slot&&SUCCEEDED(hr))mixedTarget=mixedSurface(surface);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE StretchRect(IDirect3DSurface9* from,const RECT* sr,IDirect3DSurface9* to,
                                           const RECT* dr,D3DTEXTUREFILTERTYPE filter)override{
        if((mixedFault==MixedCopy&&mixedSurface(from))||(mixedFault==MixedSeed&&mixedSurface(to))){
            mixedFault=MixedOk;++mixedHits;return E_FAIL;
        }
        return real->StretchRect(from,sr,to,dr,filter);
    }
    HRESULT STDMETHODCALLTYPE SetVertexDeclaration(IDirect3DVertexDeclaration9* value)override{
        observing=observe&&paletteDeclaration&&value==paletteDeclaration;
        record(Layout);return real->SetVertexDeclaration(value);
    }
    HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9* value)override{
        record(Vertex);return real->SetVertexShader(value);
    }
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD stage,IDirect3DBaseTexture9* value)override{
        record(AlphaTexture);return real->SetTexture(stage,value);
    }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT first,const float* data,UINT count)override{
        record(Cutoff);return real->SetPixelShaderConstantF(first,data,count);
    }
    HRESULT STDMETHODCALLTYPE SetStreamSource(UINT stream,IDirect3DVertexBuffer9* value,UINT offset,UINT stride)override{
        record(stream?Instances:Vertices);return real->SetStreamSource(stream,value==wrapped?wrappedRaw:value,offset,stride);
    }
    HRESULT STDMETHODCALLTYPE SetIndices(IDirect3DIndexBuffer9* value)override{
        record(Indices);return real->SetIndices(value);
    }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantF(UINT first,const float* data,UINT count)override{
        record(first==252?PaletteOrigin:first==0?Projection:Pose);
        if(fault!=7||first!=252)return real->SetVertexShaderConstantF(first,data,count);fault=0;++hits;
        if(after)must(real->SetVertexShaderConstantF(first,data,count));return E_FAIL;
    }
    HRESULT STDMETHODCALLTYPE SetStreamSourceFreq(UINT stream,UINT value)override{
        record(value==1?(stream?RestoreInstances:RestoreVertices):(stream?InstanceRate:DrawRate));
        if(stream==1&&value==1)observing=false;
        if(fault!=6||!(value&D3DSTREAMSOURCE_INSTANCEDATA))return real->SetStreamSourceFreq(stream,value);fault=0;++hits;
        if(after)must(real->SetStreamSourceFreq(stream,value));return E_FAIL;
    }
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE type,INT base,UINT min,UINT vertices,UINT start,UINT count)override{
        record(Draw);
        const auto hr=real->DrawIndexedPrimitive(type,base,min,vertices,start,count);
        if(mixedFault==MixedDraw&&mixedTarget&&SUCCEEDED(hr)){mixedFault=MixedOk;++mixedHits;return E_FAIL;}
        return hr;
    }
};
struct PointTerrainCoverageTest {
    struct Vertex {float x,y,z;BYTE weights[4],bones[4];float nx,ny,nz,u,v;DWORD color;};
    static void sharedShadowPrograms(IDirect3DDevice9* device,D3DPRESENT_PARAMETERS& present){
        struct Device:ForwardIDirect3DDevice9 {
            using ForwardIDirect3DDevice9::ForwardIDirect3DDevice9;
            enum Step {Caster,Replay,Vertex,CacheVertex,Cache,Union,Fast,OpaqueFast,Opaque,World,Live};
            std::vector<Step> steps;
            std::optional<Step> fault;
            HRESULT result(Step step,HRESULT hr){steps.push_back(step);return fault==step&&SUCCEEDED(hr)?E_OUTOFMEMORY:hr;}
            HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD* code,IDirect3DPixelShader9** shader)override{
                const auto hr=real->CreatePixelShader(code,shader);
                if(code==kShadowPSShader)return result(Caster,hr);
                if(code==kShadowReplayPSShader)return result(Replay,hr);
                if(code==kStaticCasterPSShader)return result(Cache,hr);
                if(code==kShadowUnionShader)return result(Union,hr);
                if(code==kStaticCasterFastPSShader)return result(Fast,hr);
                if(code==kStaticCasterOpaqueFastPSShader)return result(OpaqueFast,hr);
                if(code==kStaticCasterOpaquePSShader)return result(Opaque,hr);
                return hr;
            }
            HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD* code,IDirect3DVertexShader9** shader)override{
                const auto hr=real->CreateVertexShader(code,shader);
                if(code==kShadowVSShader)return result(Vertex,hr);
                if(code==kShadowCacheVSShader)return result(CacheVertex,hr);
                return hr;
            }
            HRESULT STDMETHODCALLTYPE CreateVertexDeclaration(const D3DVERTEXELEMENT9* elements,IDirect3DVertexDeclaration9** out)override{
                const auto hr=real->CreateVertexDeclaration(elements,out);
                return result(elements[1].Usage==D3DDECLUSAGE_NORMAL?World:Live,hr);
            }
        } calls(device);
        WorldRenderer world(&calls);
        auto& programs=world.shadowPrograms;
        const auto check=[](HRESULT hr,const char*){return SUCCEEDED(hr);};

        calls.fault=Device::Union;
        assert(programs.createUnion(&calls)==E_OUTOFMEMORY&&!programs.unionShader());
        calls.fault.reset();
        must(programs.createUnion(&calls));
        must(programs.createWorldLayout(&calls));
        const auto shared=programs.unionShader();
        assert(shared&&programs.worldLayout());

        calls.fault=Device::Replay;
        assert(!programs.createDirectional(&calls,check)&&programs.casterPixel()&&!programs.replay());
        calls.fault=Device::OpaqueFast;
        assert(programs.createDirectional(&calls,check)&&programs.unionShader()==shared);
        programs.createOptionalCache(&calls);
        assert(programs.opaqueCache()&&!programs.opaqueFastCache());
        const auto interior=programs.cachePixel(true,true),edge=programs.cachePixel(false,true);
        assert(interior.shader==programs.fastCache_&&!interior.textureIndependent);
        assert(edge.shader==programs.opaqueCache()&&edge.textureIndependent);
        calls.fault=Device::Live;
        assert(programs.createLiveLayout(&calls)==E_OUTOFMEMORY&&!programs.liveLayout());
        calls.fault.reset();
        must(programs.createLiveLayout(&calls));
        const std::vector<Device::Step> expected={Device::Union,Device::Union,Device::World,Device::Caster,Device::Replay,
            Device::Replay,Device::Vertex,Device::CacheVertex,Device::Cache,Device::Fast,Device::OpaqueFast,Device::Opaque,
            Device::Live,Device::Live};
        assert(calls.steps==expected);
        assert(programs.createDirectional(&calls,check));
        must(programs.createWorldLayout(&calls));must(programs.createLiveLayout(&calls));assert(calls.steps==expected);
        world.reset(true);
        assert(!programs.unionShader()&&!programs.worldLayout()&&!programs.liveLayout()&&!programs.casterPixel()&&!programs.opaqueCache());
        must(device->Reset(&present));
        calls.steps.clear();
        assert(programs.createDirectional(&calls,check));
        must(programs.createWorldLayout(&calls));must(programs.createLiveLayout(&calls));
        const std::vector<Device::Step> rebuilt={Device::Caster,Device::Replay,Device::Vertex,Device::CacheVertex,
            Device::Cache,Device::Union,Device::World,Device::Live};
        assert(calls.steps==rebuilt);
        std::puts("SHARED_SHADOW_PROGRAMS_PASS warmupReuse=1 requiredOom=3 optionalFallback=1 allocationOrder=exact reset=rebuilt");
    }
    static void resetOnDevice(IDirect3DDevice9* device,D3DPRESENT_PARAMETERS& present){
        PaletteFaults calls(device);
        WorldRenderer world(&calls);
        const auto failures=world.pointFailures;
        calls.casterFault=PaletteFaults::CasterVertex;
        world.prewarmPointShaders();
        assert(calls.casterSteps.size()==1&&!world.pointTargets.casterVertex()&&!world.pointTargets.casterPixel());
        assert(!world.pointSlot().retryAt&&world.pointFailures==failures);
        calls.casterFault=PaletteFaults::CasterPixel;
        assert(!world.pointResources()&&world.pointTargets.casterVertex()&&!world.pointTargets.casterPixel());
        assert(world.pointSlot().retryAt&&!world.pointSlot().cube&&!world.pointSlot().ready&&world.pointFailures==failures+1);
        assert(!world.pointResources()&&calls.casterSteps.size()==3);
        world.pointSlot().retryAt=GetTickCount();
        assert(world.pointResources()&&world.pointSlot().cube&&world.pointResources());
        const std::vector<PaletteFaults::CasterStep> attempts={PaletteFaults::CasterVertex,PaletteFaults::CasterVertex,
            PaletteFaults::CasterPixel,PaletteFaults::CasterPixel};
        assert(calls.casterSteps==attempts);
        const auto casterVertex=world.pointTargets.casterVertex();
        const auto casterPixel=world.pointTargets.casterPixel();
        assert(world.pointPaletteResources()&&world.shadowPalette.buffer());
        const auto shader=world.shadowPalette.shader_;
        const auto declaration=world.shadowPalette.declaration_;
        world.reset(true);
        assert(!world.shadowPalette.buffer()&&world.shadowPalette.canAttempt(world.frames));
        assert(!world.pointSlot().cube&&!world.pointTargets.scratch());
        must(device->Reset(&present));
        assert(world.pointResources()&&world.pointSlot().cube&&calls.casterSteps==attempts);
        assert(world.pointTargets.casterVertex()==casterVertex&&world.pointTargets.casterPixel()==casterPixel);
        assert(world.pointPaletteResources()&&world.shadowPalette.buffer());
        assert(world.shadowPalette.shader_==shader&&world.shadowPalette.declaration_==declaration);
        world.releasePointGPU();
        assert(!world.pointTargets.casterVertex()&&!world.pointTargets.casterPixel()&&!world.pointSlot().cube);
        std::puts("POINT_CASTER_RESOURCES_PASS oom=2 ordered=1 stable=1 reset=1 released=1");
        std::puts("SHADOW_PALETTE_DEVICE_RESET_PASS");
    }
    static void run(IDirect3DDevice9* device,bool timing){
        PaletteFaults calls(device);auto* d=&calls;WorldRenderer w(d);w.valid=true;
        w.active=std::make_shared<WorldRenderer::Snapshot>();w.active->map=w.uploadedMap="point-palette";w.meshGeneration=1;
        const auto& words=ClientShaders::OneBoneVs3;auto program=std::make_shared<NorthlightActorDeformation::Program>();
        assert(NorthlightActorDeformation::compile(words,std::size(words),*program)&&program->kernel.singleSkin());
        IDirect3DVertexShader9 *original=nullptr,*shader=nullptr;must(d->CreateVertexShader(reinterpret_cast<const DWORD*>(words),&original));
        std::vector<uint32_t> patched,sliced;assert(NorthlightShadowShader::patch(words,std::size(words),patched));
        if(NorthlightModelNormalSlice::shadowVertex(patched.data(),patched.size(),sliced))patched.swap(sliced);
        must(d->CreateVertexShader(reinterpret_cast<const DWORD*>(patched.data()),&shader));w.actorPrograms[original]=program;
        const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_UBYTE4N,0,D3DDECLUSAGE_BLENDWEIGHT,0},
            {0,16,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},{0,20,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},
            {0,32,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},{0,40,D3DDECLTYPE_D3DCOLOR,0,D3DDECLUSAGE_COLOR,0},D3DDECL_END()};
        IDirect3DVertexDeclaration9* decl=nullptr;must(d->CreateVertexDeclaration(elements,&decl));
        const D3DVERTEXELEMENT9 world[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,24,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};must(d->CreateVertexDeclaration(world,&w.shadowPrograms.worldLayout_));
        for(unsigned i=0;i<16;++i)w.context.inverseView[i]=i%5==0?1.f:0.f;
        std::vector<Vertex> vertices;constexpr unsigned grid=16;
        for(unsigned y=0;y<=grid;++y)for(unsigned x=0;x<=grid;++x){Vertex v{};v.x=(float(x)/grid-.5f)*1.2f;v.y=(float(y)/grid-.5f)*1.2f;v.z=.08f*std::sin(float(x+y));v.weights[0]=255;v.bones[0]=BYTE((x+y*3)%21);v.nz=1;v.color=0xffffffff;vertices.push_back(v);}
        auto source=std::make_shared<NorthlightDrawSnapshot::Mesh>();source->vertexCount=UINT(vertices.size());source->indexed=true;
        for(unsigned y=0;y<grid;++y)for(unsigned x=0;x<grid;++x){const unsigned a=y*(grid+1)+x,b=a+1,c=a+grid+1,e=c+1;source->indices.insert(source->indices.end(),{a,b,c,b,e,c});}
        source->primitiveCount=UINT(source->indices.size()/3);source->streams[0].stride=sizeof(Vertex);source->streams[0].bytes.resize(vertices.size()*sizeof(Vertex));std::memcpy(source->streams[0].bytes.data(),vertices.data(),source->streams[0].bytes.size());
        IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;void* data=nullptr;
        must(d->CreateVertexBuffer(UINT(source->streams[0].bytes.size()+8*sizeof(Vertex)),0,0,D3DPOOL_MANAGED,&vb,nullptr));must(vb->Lock(8*sizeof(Vertex),UINT(source->streams[0].bytes.size()),&data,0));std::memcpy(data,vertices.data(),source->streams[0].bytes.size());must(vb->Unlock());
        must(d->CreateIndexBuffer(UINT((source->indices.size()+7)*4),0,D3DFMT_INDEX32,D3DPOOL_MANAGED,&ib,nullptr));must(ib->Lock(7*sizeof(uint32_t),UINT(source->indices.size()*4),&data,0));std::memcpy(data,source->indices.data(),source->indices.size()*4);must(ib->Unlock());
        w.meshBuffers.resize(1);w.meshBuffers[0].vb=vb;w.meshBuffers[0].ib=ib;w.vertices=vb;w.indices=ib;
        for(unsigned a=0;a<100;++a){auto p=std::make_unique<WorldRenderer::Replay>();p->shared=source;p->program=program.get();
            p->originalShader=original;original->AddRef();p->shader=shader;shader->AddRef();p->decl=decl;decl->AddRef();p->stream[0]=vb;vb->AddRef();p->stride[0]=sizeof(Vertex);p->index=ib;ib->AddRef();
            p->type=D3DPT_TRIANGLELIST;p->indexed=true;p->base=5;p->offset[0]=3*sizeof(Vertex);p->min=0;p->vertices=source->vertexCount;p->start=7;p->count=source->primitiveCount;p->projectionKind=2;p->constantUsage=NorthlightShaderConstants::analyze(words,std::size(words),2);p->constantGroup=a;
            const float angle=float(a)*6.2831853f/100,tx=5*std::cos(angle),ty=5*std::sin(angle),tz=4+(float(a%7)-3)*.6f;
            for(unsigned b=0;b<75;++b){float* c=p->writableConstants()+4*(31+3*b);c[0]=c[5]=c[10]=1;c[3]=tx+float(b%3)*.005f;c[7]=ty;c[11]=tz;}w.replays.push_back(std::move(p));}
        auto bounds=[&](unsigned expected){for(auto& p:w.replays){p->pointBounds={};p->boundsWork={};}
            for(unsigned pass=0;pass<128;++pass){w.replayBoundsBegin();w.pointCalculateReplayBounds();bool complete=true;for(const auto& p:w.replays)complete&=p->pointBounds.valid&&p->boundsWork.paletteSpan==expected;if(complete){w.replayBoundsResolved=true;return;}if(pass==127)std::fprintf(stderr,"BOUNDS expected=%u valid=%u firstSpan=%u firstKind=%u\n",expected,w.pointBoundsValid,w.replays[0]->boundsWork.paletteSpan,unsigned(w.replays[0]->boundsWork.kind));assert(pass<127);}};
        bounds(21);
        for(auto state:{D3DRS_ALPHATESTENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_FOGENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_SRGBWRITEENABLE})must(d->SetRenderState(state,FALSE));
        must(d->SetRenderState(D3DRS_ZENABLE,TRUE));must(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));must(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));must(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
        IDirect3DSurface9* read=nullptr;must(d->CreateOffscreenPlainSurface(256,256,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&read,nullptr));IDirect3DQuery9* event=nullptr;must(d->CreateQuery(D3DQUERYTYPE_EVENT,&event));
        auto drain=[&]{must(event->Issue(D3DISSUE_END));const auto start=GetTickCount();HRESULT hr;while((hr=event->GetData(nullptr,0,D3DGETDATA_FLUSH))==S_FALSE){assert(GetTickCount()-start<30000);SwitchToThread();}must(hr);};
        {
            constexpr UINT size=NorthlightWorldDirectionalTargets::Size;
            NorthlightDrawSnapshot::Ref<IDirect3DSurface9> target,depth,readback,savedTarget,savedDepth;
            NorthlightDrawSnapshot::Ref<IDirect3DPixelShader9> pixel;
            must(d->GetRenderTarget(0,savedTarget.out()));
            const HRESULT oldDepth=d->GetDepthStencilSurface(savedDepth.out());assert(SUCCEEDED(oldDepth)||oldDepth==D3DERR_NOTFOUND);
            must(d->CreateRenderTarget(size,size,D3DFMT_R32F,D3DMULTISAMPLE_NONE,0,FALSE,target.out(),nullptr));
            must(d->CreateDepthStencilSurface(size,size,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,depth.out(),nullptr));
            must(d->CreateOffscreenPlainSurface(size,size,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,readback.out(),nullptr));
            must(d->CreatePixelShader(kShadowReplayPSShader,pixel.out()));
            std::array<std::array<float,1024>,3> saved{};
            for(unsigned i=0;i<saved.size();++i)std::copy_n(w.replays[i]->constants,1024,saved[i].begin());
            for(unsigned i=0;i<saved.size();++i)for(unsigned b=0;b<21;++b){
                auto* bone=w.replays[i]->writableConstants()+4*(31+3*b);
                bone[3]=float(i)*3.f-3.f;bone[7]=0;bone[11]=3;
            }
            auto& middle=*w.replays[1];++middle.base;middle.offset[0]-=sizeof(Vertex);
            bounds(21);
            const float matrix[]={.125f,0,0,0,0,.125f,0,0,0,0,.125f,0,0,0,0,1};
            const std::vector<uint32_t> packets{0,1,2};
            const auto directional=[&](bool palette){
                w.shadowPalette.unsupported_=!palette;
                WorldRenderer::DirectionalReplayWork work;
                NorthlightShadowReplay::Pass replayPass(d);
                NorthlightRenderThreadProbe::Off split;size_t bytes=0,calls=0;
                const auto issue=[&](uint32_t i,const NorthlightReplayDrawState::Range* range,WorldRenderer::ShadowIndexPair* pair,NorthlightShaderConstants::Range floats){
                    return w.submitReplay(w.replays[i].get(),replayPass,matrix,matrix,split,bytes,calls,
                        [](HRESULT hr,const char*){return SUCCEEDED(hr);},range,pair,floats);
                };
                must(d->BeginScene());w.prepareWorldShadowState();must(d->SetRenderTarget(0,target.p));must(d->SetDepthStencilSurface(depth.p));
                must(d->SetPixelShader(pixel.p));must(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff,1,0));
                assert(w.runDirectionalReplays(packets,matrix,matrix,replayPass,work,issue,[](uint32_t){}));
                must(d->EndScene());drain();must(d->GetRenderTargetData(target.p,readback.p));
                for(UINT stream=0;stream<2;++stream){UINT frequency=0;must(d->GetStreamSourceFreq(stream,&frequency));assert(frequency==1);}
                std::vector<float> image(size_t(size)*size);D3DLOCKED_RECT lock{};must(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));
                for(UINT y=0;y<size;++y)std::memcpy(image.data()+y*size,static_cast<const char*>(lock.pBits)+y*lock.Pitch,size*sizeof(float));
                must(readback->UnlockRect());return std::make_pair(work,std::move(image));
            };
            const auto scalar=directional(false);
            calls.observe=true;
            const auto instanced=directional(true);
            calls.observe=false;
            using S=PaletteFaults;
            const std::vector<S::Step> expected{S::Layout,S::Vertex,S::Projection,S::PaletteOrigin,S::AlphaTexture,
                S::Cutoff,S::Vertices,S::Indices,S::Instances,S::InstanceRate,S::Pose,S::DrawRate,S::Draw,
                S::RestoreVertices,S::RestoreInstances};
            assert(calls.steps==expected);
            assert(scalar.first.draws==3&&instanced.first.draws==2&&instanced.first.palettePackets==2);
            assert(std::count_if(scalar.second.begin(),scalar.second.end(),[](float z){return z<.99f;})>1000);
            assert(scalar.second==instanced.second);
            for(unsigned b=0;b<21;++b)middle.writableConstants()[4*(31+3*b)+3]=3;
            bounds(21);const auto overlap=directional(true);
            assert(overlap.first.draws==3&&!overlap.first.paletteDraws&&overlap.second==directional(false).second);
            --middle.base;middle.offset[0]+=sizeof(Vertex);
            for(unsigned i=0;i<saved.size();++i)std::copy_n(saved[i].data(),1024,w.replays[i]->writableConstants());
            bounds(21);w.shadowPalette.unsupported_=false;
            must(d->SetDepthStencilSurface(nullptr));must(d->SetRenderTarget(0,savedTarget.p));must(d->SetDepthStencilSurface(savedDepth.p));
            must(d->SetPixelShader(nullptr));
            std::puts("DIRECTIONAL_PALETTE_GPU_PASS disjointBefore=3 disjointAfter=2 overlapping=3 exactDepth=1");
        }
        auto image=[&]{std::vector<float> out(6*256*256);for(unsigned f=0;f<6;++f){must(d->GetRenderTargetData(w.pointSlot().faces[f],read));D3DLOCKED_RECT lock{};must(read->LockRect(&lock,nullptr,D3DLOCK_READONLY));for(unsigned y=0;y<256;++y)std::memcpy(out.data()+f*256*256+y*256,static_cast<char*>(lock.pBits)+y*lock.Pitch,256*4);must(read->UnlockRect());}return out;};
        auto compare=[&](const std::vector<float>& a,const std::vector<float>& b){assert(a.size()==b.size());size_t covered=0;for(size_t i=0;i<a.size();++i){assert(std::isfinite(a[i])&&std::isfinite(b[i])&&std::fabs(a[i]-b[i])<=2e-6f);covered+=a[i]<.999f;}assert(covered>1000);};
        NorthlightLocalLights::Light light{{0,0,4},{1,.4f,.1f},1,12,17,3,NorthlightLocalLights::CastShadow};
        auto render=[&](bool optimized){w.shadowPalette.unsupported_=!optimized;w.pointReplayOrderReady=false;w.pointReplayCasters.invalidate();w.pointSlot().schedule.invalidate();w.pointSlot().retryAt=0;w.pointWork={};must(d->BeginScene());const auto result=w.renderPointShadow(light,true,true);must(d->EndScene());drain();for(unsigned stream=0;stream<2;++stream){UINT freq=0;must(d->GetStreamSourceFreq(stream,&freq));assert(freq==1);}return result;};
        assert(render(false));
        if(w.pointMinBlend&&w.pointMixedReuseEnabled()){
            const auto& rigid=ClientShaders::NoBoneVs3;
            NorthlightDrawSnapshot::Ref<IDirect3DVertexShader9> rigidOriginal;
            must(d->CreateVertexShader(reinterpret_cast<const DWORD*>(rigid),rigidOriginal.out()));
            std::vector<uint32_t> code,compact;
            assert(NorthlightShadowShader::patch(rigid,std::size(rigid),code));
            if(NorthlightModelNormalSlice::shadowVertex(code.data(),code.size(),compact))code.swap(compact);
            auto& metadata=w.captureShaders[rigidOriginal.p];
            must(d->CreateVertexShader(reinterpret_cast<const DWORD*>(code.data()),&metadata.replacement));
            metadata.pointRigidRows=NorthlightPointShadow::RigidPoseRows::fromProgram(code.data(),code.size());
            metadata.usage=NorthlightShaderConstants::analyze(rigid,std::size(rigid),2);
            source->sourceIdentity=NorthlightSourceIdentity::make({23,56,91});
            const auto oldUsage=w.replays[0]->constantUsage;
            constexpr unsigned rigidCount=12;
            for(unsigned i=0;i<rigidCount;++i){
                auto& p=*w.replays[i];
                assert(!p.gpuCached&&!p.shadowSkinned);
                p.originalShader=rigidOriginal.p;p.shader=metadata.replacement;
                p.constantUsage=metadata.usage;
                p.gpuCached=true;
            }
            w.pointCold={};assert(render(false)&&!w.pointCold.mixedFaces&&!w.pointCold.mixedBakedDraws);
            const auto seedStatic=[&]{
                SavedState saved(d,&w.stateBlocks);assert(saved.ok);
                must(d->SetDepthStencilSurface(nullptr));
                for(unsigned face=0;face<6;++face){
                    assert(w.pointCacheResources(face));
                    must(d->SetRenderTarget(0,w.pointSlot().cacheSurface[face]));
                    must(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xffffffff,1,0));
                }
                assert(saved.restore());
            };
            seedStatic();
            w.pointMixedFaces.refuseStore();
            w.pointCold={};assert(render(false));const auto baseline=image();const auto before=w.pointDraws;
            calls.watchMixed=true;
            const auto reset=[&]{
                w.pointMixedFaces.clear();
                w.pointSlot().mixedTokens.fill(0);
                calls.resetMixedFault();
            };
            reset();
            w.pointCold={};assert(render(false)&&w.pointCold.mixedBakedDraws&&image()==baseline);
            w.pointCold={};assert(render(false)&&w.pointCold.mixedFaces&&w.pointDraws<before&&image()==baseline);
            for(auto fault:{PaletteFaults::MixedCopy,PaletteFaults::MixedAllocate,PaletteFaults::MixedSeed,PaletteFaults::MixedDraw}){
                if(fault!=PaletteFaults::MixedCopy)reset();
                calls.mixedFault=fault;const auto hits=calls.mixedHits;
                w.pointCold={};assert(render(false)&&w.pointSlot().ready&&image()==baseline);
                assert(calls.mixedHits==hits+1&&w.pointCold.mixedFailures);
            }
            reset();w.pointCold={};assert(render(false)&&w.pointCold.mixedBakedDraws&&image()==baseline);
            w.releasePointGPU(true);
            unsigned remaining=0;w.pointMixedFaces.visitTextures([&](auto*){++remaining;});assert(!remaining);
            calls.resetMixedFault();calls.watchMixed=false;
            assert(render(false));seedStatic();
            assert(render(false)&&image()==baseline);
            reset();
            for(unsigned i=0;i<rigidCount;++i){
                auto& p=*w.replays[i];
                p.originalShader=original;p.shader=shader;p.constantUsage=oldUsage;p.gpuCached=false;
            }
            source->sourceIdentity={};
            metadata.replacement->Release();w.captureShaders.erase(rigidOriginal.p);
            w.releasePointGPU(true);
            std::puts("POINT_MIXED_CACHE_GPU_PASS exactDepth=1 failures=allocation,copy,seed,draw release=1 rebuilt=1 directSkipped=1");
        }
        unsigned comparisons=0;
        for(unsigned pose=0;pose<3;++pose){if(pose){const float angle=float(pose)*.3f;w.context.inverseView[0]=w.context.inverseView[5]=std::cos(angle);w.context.inverseView[1]=std::sin(angle);w.context.inverseView[4]=-std::sin(angle);
                for(auto& p:w.replays)for(unsigned bone=0;bone<21;++bone)p->writableConstants()[4*(31+3*bone)+3]+=.05f;bounds(21);}
            assert(render(false));const auto reference=image();const auto oldDraws=w.pointDraws;unsigned weights[6];std::copy_n(w.pointSlot().faceWeights,6,weights);
            assert(render(true));compare(reference,image());assert(std::equal(weights,weights+6,w.pointSlot().faceWeights));assert(w.pointWork.palettePackets>0&&w.pointDraws<oldDraws/2);++comparisons;}
        // Material layers can submit an identical opaque pose repeatedly while
        // unrelated native registers and capture groups change between packets.
        const auto originalLight=light;
        std::vector<std::array<float,1024>> originalBanks(w.replays.size());
        for(size_t i=0;i<w.replays.size();++i){
            auto& packet=*w.replays[i];
            std::copy_n(packet.constants,1024,originalBanks[i].begin());
            std::copy_n(originalBanks[i-i%4].data(),1024,packet.writableConstants());
            packet.writableConstants()[4*(31+3*74)]=float(i);
        }
        bounds(21);
        for(unsigned shift=0;shift<2;++shift){
            light.position[0]=originalLight.position[0]+float(shift)*.75f;
            const auto signedZeros=[&](bool alternate){
                for(size_t i=0;i<w.replays.size();++i)
                    w.replays[i]->writableConstants()[4*31+1]=alternate&&(i%2)?-0.f:0.f;
            };
            // Both controls use palette instancing. A different zero sign
            // preserves the image while forcing a distinct bytewise pose.
            signedZeros(true);assert(render(true));const auto before=image();
            const auto triangles=w.pointWork.triangles;
            const auto draws=w.pointWork.replay;
            assert(!w.pointWork.duplicateInstances);
            unsigned weights[6];std::copy_n(w.pointSlot().faceWeights,6,weights);
            signedZeros(false);assert(render(true));assert(image()==before);
            assert(w.pointWork.duplicateInstances&&w.pointWork.triangles*3<triangles&&w.pointWork.replay<draws);
            assert(std::equal(weights,weights+6,w.pointSlot().faceWeights));
            std::printf("POINT_REPEAT_POSE_GPU_PASS moved=%u beforeDraws=%u afterDraws=%u beforeTriangles=%llu afterTriangles=%llu duplicateInstances=%u exactFaces=6\n",
                shift,draws,w.pointWork.replay,(unsigned long long)triangles,(unsigned long long)w.pointWork.triangles,w.pointWork.duplicateInstances);
            if(shift==1){
                for(unsigned bone=0;bone<21;++bone)w.replays[1]->writableConstants()[4*(31+3*bone)+3]+=1.5f;
                bounds(21);
                signedZeros(true);assert(render(true));const auto animated=image();
                assert(animated!=before);
                signedZeros(false);assert(render(true));assert(image()==animated&&w.pointWork.duplicateInstances);
            }
        }
        light=originalLight;
        for(size_t i=0;i<w.replays.size();++i)std::copy_n(originalBanks[i].data(),1024,w.replays[i]->writableConstants());
        bounds(21);assert(render(true));
        // Material-only layout variants keep the same POSITION and BLENDINDICES.
        // Every variant is a real device declaration, and the scalar reference
        // uses that native declaration for the draw.
        const auto oneLayoutDraws=w.pointDraws;
        for(unsigned variant=0;variant<16;++variant){
            D3DVERTEXELEMENT9 alternate[std::size(elements)];
            std::copy_n(elements,std::size(elements),alternate);
            alternate[5].Usage=D3DDECLUSAGE_TEXCOORD;
            alternate[5].UsageIndex=BYTE(1+variant%8);
            alternate[5].Type=variant<8?D3DDECLTYPE_D3DCOLOR:D3DDECLTYPE_UBYTE4N;
            IDirect3DVertexDeclaration9* materialDecl=nullptr;
            must(d->CreateVertexDeclaration(alternate,&materialDecl));
            for(size_t i=variant;i<w.replays.size();i+=16){
                auto& p=*w.replays[i];p.decl->Release();p.decl=materialDecl;materialDecl->AddRef();
            }
            materialDecl->Release();
        }
        bounds(21);
        assert(render(false));const auto materialReference=image();const auto scalarDraws=w.pointDraws;
        assert(render(true));compare(materialReference,image());
        std::printf("POINT_PALETTE_MATERIAL layouts=16 individual=%u grouped=%u singleLayout=%u\n",scalarDraws,w.pointDraws,oneLayoutDraws);
        assert(w.pointDraws==oneLayoutDraws);++comparisons;
        for(auto& p:w.replays){p->decl->Release();p->decl=decl;decl->AddRef();}
        bounds(21);
        // Rebase a nonzero range without changing the original vertex bone IDs.
        auto sparse=std::make_shared<NorthlightDrawSnapshot::Mesh>(*source);auto sparseVertices=vertices;for(auto& v:sparseVertices)v.bones[0]=BYTE(v.bones[0]+50);std::memcpy(sparse->streams[0].bytes.data(),sparseVertices.data(),sparse->streams[0].bytes.size());
        must(vb->Lock(8*sizeof(Vertex),UINT(source->streams[0].bytes.size()),&data,0));std::memcpy(data,sparseVertices.data(),sparse->streams[0].bytes.size());must(vb->Unlock());for(auto& p:w.replays)p->shared=sparse;bounds(71);
        for(const auto& p:w.replays)assert(p->boundsWork.paletteFirst==50);
        assert(render(false));const auto sparseImage=image();assert(render(true));compare(sparseImage,image());assert(w.pointWork.paletteDraws);++comparisons;
        must(vb->Lock(8*sizeof(Vertex),UINT(source->streams[0].bytes.size()),&data,0));std::memcpy(data,vertices.data(),source->streams[0].bytes.size());must(vb->Unlock());for(auto& p:w.replays)p->shared=source;bounds(21);
        std::vector<std::unique_ptr<WorldRenderer::Replay>> tail;while(w.replays.size()>12){tail.push_back(std::move(w.replays.back()));w.replays.pop_back();}
        for(const auto spec:std::array<std::array<unsigned,2>,8>{{{0,1},{0,16},{0,41},{0,42},{1,41},{33,41},{50,21},{74,1}}}){const auto first=spec[0],span=spec[1];auto changed=std::make_shared<NorthlightDrawSnapshot::Mesh>(*source);auto v=vertices;
            for(unsigned i=0;i<v.size();++i)v[i].bones[0]=BYTE(first+i%span);std::memcpy(changed->streams[0].bytes.data(),v.data(),changed->streams[0].bytes.size());
            must(vb->Lock(8*sizeof(Vertex),UINT(source->streams[0].bytes.size()),&data,0));std::memcpy(data,v.data(),changed->streams[0].bytes.size());must(vb->Unlock());for(auto& p:w.replays)p->shared=changed;bounds(first+span);for(const auto& p:w.replays)assert(p->boundsWork.paletteFirst==first);
            assert(render(false));const auto expected=image();assert(render(true));compare(expected,image());assert(bool(w.pointWork.paletteDraws)==(span<=41));++comparisons;}
        must(vb->Lock(8*sizeof(Vertex),UINT(source->streams[0].bytes.size()),&data,0));std::memcpy(data,vertices.data(),source->streams[0].bytes.size());must(vb->Unlock());for(auto& p:w.replays)p->shared=source;bounds(21);
        while(!tail.empty()){w.replays.push_back(std::move(tail.back()));tail.pop_back();}bounds(21);
        IDirect3DTexture9* white=nullptr;must(d->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&white,nullptr));D3DLOCKED_RECT lock{};must(white->LockRect(0,&lock,nullptr,0));*static_cast<DWORD*>(lock.pBits)=0xffffffff;must(white->UnlockRect(0));
        for(auto& p:w.replays){p->cutoff=.5f;p->texture=white;white->AddRef();}assert(render(false));const auto alpha=image();assert(render(true));compare(alpha,image());assert(!w.pointWork.paletteDraws);++comparisons;
        must(white->LockRect(0,&lock,nullptr,0));*static_cast<DWORD*>(lock.pBits)=0x00ffffff;must(white->UnlockRect(0));
        for(unsigned i=0;i<w.replays.size();++i)w.replays[i]->cutoff=i==10||i==50||i==90?.5f:-1;
        assert(render(false));const auto mixed=image();assert(render(true));compare(mixed,image());assert(w.pointWork.paletteDraws&&w.pointWork.palettePackets);++comparisons;
        // The three transparent packets reject every pixel with either native
        // alpha testing or shader clip. The opaque neighbours can be instanced.
        NorthlightShadowAlpha::Info info{};info.outputKnown=true;info.outputMode=NorthlightShadowAlpha::Texture;
        for(auto& p:w.replays)if(p->cutoff>=0)p->alphaMode=NorthlightShadowAlpha::fixedTestMode(info,false,127,false,D3DFMT_A8R8G8B8);
        w.pointCold={};assert(render(false));assert(image()==mixed&&w.pointCold.alphaScopes>0);
        w.pointCold={};assert(render(true));compare(mixed,image());assert(w.pointCold.alphaScopes&&w.pointWork.palettePackets);++comparisons;
        for(auto& p:w.replays){p->alphaMode=0;p->cutoff=-1;p->texture->Release();p->texture=nullptr;}white->Release();
        assert(render(false));const auto reference=image();
        for(unsigned fault=1;fault<=7;++fault)for(bool after:{false,true}){w.releasePointGPU();calls.fault=fault;calls.after=after;const auto hits=calls.hits;
            const bool result=render(true);assert(calls.hits==hits+1&&!calls.fault);if(fault<=5){assert(result&&!w.pointWork.paletteDraws);compare(reference,image());}
            else assert(!result&&!w.pointSlot().ready);w.frames+=121;assert(render(true));compare(reference,image());assert(w.pointWork.paletteDraws);++comparisons;}
        const auto paletteShader=w.shadowPalette.shader_;
        const auto declaration=w.shadowPalette.declaration_;
        assert(paletteShader&&declaration&&w.shadowPalette.buffer());
        w.releasePointGPU(true);
        assert(!w.shadowPalette.buffer()&&w.shadowPalette.shader_==paletteShader&&w.shadowPalette.declaration_==declaration);
        assert(w.shadowPalette.canAttempt(w.frames));
        assert(render(true));compare(reference,image());++comparisons;
        // Interleaved compatible meshes could not use the old adjacent-run
        // palette path. Compare with that path still enabled in native order.
        for(unsigned a=0;a<w.replays.size();++a)w.replays[a]->count=source->primitiveCount-2*(a%3);
        const auto order=[&](bool sorted){w.shadowPalette.unsupported_=false;w.pointReplayOrderReady=false;w.pointReplayCasters.invalidate();
            if(!sorted){w.pointOrderReplays();for(size_t i=0;i<w.replays.size();++i)w.pointReplayOrder[i]=i;}};
        const auto renderOrder=[&](bool sorted){order(sorted);w.pointSlot().schedule.invalidate();w.pointWork={};must(d->BeginScene());assert(w.renderPointShadow(light,true,true));must(d->EndScene());drain();};
        renderOrder(false);const auto unordered=image();const auto unorderedDraws=w.pointDraws;unsigned orderWeights[6];std::copy_n(w.pointSlot().faceWeights,6,orderWeights);
        renderOrder(true);compare(unordered,image());assert(std::equal(orderWeights,orderWeights+6,w.pointSlot().faceWeights));assert(w.pointDraws*3<unorderedDraws*2);++comparisons;
        std::printf("POINT_ORDER_GPU_PASS originalDraws=%u groupedDraws=%u triangles=%llu barriers=opaque-only depthTolerance=2e-6\n",unorderedDraws,w.pointDraws,(unsigned long long)w.pointWork.triangles);
        if(timing)for(unsigned block=0;block<12;++block)for(unsigned turn=0;turn<2;++turn){const bool sorted=(block+turn)&1;order(sorted);const auto start=std::chrono::steady_clock::now();unsigned draws=0;
            must(d->BeginScene());for(unsigned l=0;l<10;++l){light.sourceId=100+l;light.position[0]=float(l)*.07f;w.pointSlot().schedule.invalidate();assert(w.renderPointShadow(light,true,true));draws+=w.pointDraws;}must(d->EndScene());
            const auto submitted=std::chrono::steady_clock::now();drain();const auto done=std::chrono::steady_clock::now();
            std::printf("POINT_ORDER_TIMING block=%u sorted=%u cpuMs=%.6f completeMs=%.6f draws=%u\n",block,unsigned(sorted),std::chrono::duration<double,std::milli>(submitted-start).count(),std::chrono::duration<double,std::milli>(done-start).count(),draws);must(d->Present(nullptr,nullptr,nullptr,nullptr));}
        std::printf("POINT_PALETTE_GPU_PASS comparisons=%u actors=100 faces=6 spans=1,16,21,41,42,71 rebasedFirst=50 coldResume=1 alphaFallback=1 faults=14 recoveryWithoutRestart=1 reset=1 depthTolerance=2e-6\n",comparisons);
        if(timing){IDirect3DQuery9 *begin=nullptr,*end=nullptr,*hz=nullptr,*disjoint=nullptr;must(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&begin));must(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));must(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&hz));must(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));
            for(unsigned unique=0;unique<4;++unique){
            if(unique==2){auto shifted=std::make_shared<NorthlightDrawSnapshot::Mesh>(*source);auto v=vertices;for(auto& item:v)item.bones[0]=74;
                std::memcpy(shifted->streams[0].bytes.data(),v.data(),shifted->streams[0].bytes.size());must(vb->Lock(8*sizeof(Vertex),UINT(shifted->streams[0].bytes.size()),&data,0));std::memcpy(data,v.data(),shifted->streams[0].bytes.size());must(vb->Unlock());for(auto& p:w.replays)p->shared=shifted;bounds(75);for(const auto& p:w.replays)assert(p->boundsWork.paletteFirst==74);}
            if(unique==3){
                must(vb->Lock(8*sizeof(Vertex),UINT(source->streams[0].bytes.size()),&data,0));
                std::memcpy(data,source->streams[0].bytes.data(),source->streams[0].bytes.size());must(vb->Unlock());
                for(size_t i=0;i<w.replays.size();++i){
                    w.replays[i]->shared=source;
                    std::copy_n(originalBanks[i-i%4].data(),1024,w.replays[i]->writableConstants());
                }
                bounds(21);
            }
            for(unsigned a=0;a<w.replays.size();++a)w.replays[a]->count=source->primitiveCount-(unique==1?a:0);
            for(unsigned block=0;block<8;++block)for(unsigned mode=0;mode<2;++mode){const bool optimized=(block&1)?!mode:mode;double cpu=0,gpu=0,complete=0;unsigned draws=0,packets=0;
                if(unique==3)for(size_t i=0;i<w.replays.size();++i)w.replays[i]->writableConstants()[4*31+1]=(!optimized&&i%2)?-0.f:0.f;
                for(unsigned round=0;round<8;++round){const auto start=std::chrono::steady_clock::now();w.shadowPalette.unsupported_=unique!=3&&!optimized;w.pointReplayOrderReady=false;w.pointWork={};must(d->BeginScene());must(disjoint->Issue(D3DISSUE_BEGIN));must(hz->Issue(D3DISSUE_END));must(begin->Issue(D3DISSUE_END));
                    for(unsigned l=0;l<10;++l){light.sourceId=100+l;light.position[0]=float(l)*.07f;w.pointSlot().schedule.invalidate();assert(w.renderPointShadow(light,true,true));draws+=w.pointDraws;}
                    must(end->Issue(D3DISSUE_END));must(disjoint->Issue(D3DISSUE_END));must(d->EndScene());const auto submitted=std::chrono::steady_clock::now();drain();const auto done=std::chrono::steady_clock::now();UINT64 a=0,b=0,h=0;BOOL invalid=TRUE;must(begin->GetData(&a,sizeof a,0));must(end->GetData(&b,sizeof b,0));must(hz->GetData(&h,sizeof h,0));must(disjoint->GetData(&invalid,sizeof invalid,0));assert(!invalid&&h&&b>=a);
                    if(round){cpu+=std::chrono::duration<double,std::milli>(submitted-start).count();complete+=std::chrono::duration<double,std::milli>(done-start).count();gpu+=double(b-a)*1000/h;packets+=w.pointWork.palettePackets;}must(d->Present(nullptr,nullptr,nullptr,nullptr));}
                std::printf("%s unique=%u block=%u optimized=%u cpuMs=%.6f gpuMs=%.6f completeMs=%.6f draws=%u packets=%u\n",unique==3?"POINT_REPEAT_POSE_TIMING":"POINT_PALETTE_TIMING",unique,block,unsigned(optimized),cpu/7,gpu/7,complete/7,draws/8,packets/7);
            }}begin->Release();end->Release();hz->Release();disjoint->Release();}
        read->Release();event->Release();original->Release();shader->Release();decl->Release();
    }
};
int main(int argc,char** argv){assert(argc==3||argc==4);std::setvbuf(stdout,nullptr,_IONBF,0);const auto path=std::wstring(argv[2],argv[2]+std::strlen(argv[2]));assert(GetFullPathNameW(path.c_str(),MAX_PATH,rootPath,nullptr));
    const auto hwnd=CreateWindowW(L"STATIC",L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(hwnd);auto module=LoadLibraryA(argv[1]);assert(module);
    auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=hwnd;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;must(api->CreateDevice(0,D3DDEVTYPE_HAL,hwnd,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    PointTerrainCoverageTest::sharedShadowPrograms(d,pp);
    PointTerrainCoverageTest::resetOnDevice(d,pp);
    PointTerrainCoverageTest::run(d,argc==4);d->Release();api->Release();FreeLibrary(module);DestroyWindow(hwnd);
}
