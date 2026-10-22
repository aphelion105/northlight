#define NOMINMAX
#include <d3d9.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <new>
#include <unordered_map>
static int failAllocationAfter=-1;
void* operator new(std::size_t n){if(failAllocationAfter==0){failAllocationAfter=-1;throw std::bad_alloc();}if(failAllocationAfter>0)--failAllocationAfter;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
#include "forwarders.h"
template<class T>static void drop(T*& p){if(p)p->Release();p=nullptr;}
static void logf(const char*,...){}
#include "quality_settings.h"
#include "auto_displacement_renderer.h"
#include "saved_state.h"
#include "auto_bump_renderer.h"
#include "device_mirror.h"

struct DisplacementStateTestAccess {
    using Mesh=NorthlightDrawSnapshot::Mesh;
    static void cache();
    static void capture();
    static void posed(){
        using namespace NorthlightWaterShaderPatch;using namespace NorthlightActorDeformation;
        NorthlightAutoDisplacement g(nullptr);Program p;p.major=3;p.positionRegister=0;p.skinned=true;
        p.inputs={{0,D3DDECLUSAGE_POSITION,0},{1,D3DDECLUSAGE_BLENDINDICES,0}};
        Operation address;address.code=46;address.destination=dst(3,0,1);address.source[0].token=src(1,1,0);p.operations.push_back(address);
        Operation add;add.code=2;add.destination=dst(0,0);add.source[0].token=src(1,0);add.source[1].token=src(2,0)|0x2000;add.source[1].address=src(3,0,0);p.operations.push_back(add);
        const D3DVERTEXELEMENT9 decl[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},D3DDECL_END()};
        auto mesh=std::make_shared<Mesh>();mesh->vertexCount=3;mesh->primitiveCount=1;mesh->streams[0].stride=16;mesh->streams[0].bytes.resize(48);
        for(unsigned i=0;i<3;++i){float v[]={float(i),float(i),float(i)};std::memcpy(mesh->streams[0].bytes.data()+16*i,v,12);mesh->streams[0].bytes[16*i+12]=uint8_t(i);}
        std::shared_ptr<const Mesh> source=mesh;float low[3],high[3];
        auto check=[&]{assert(g.distantPoseBounds(p,source,decl,3,low,high));for(unsigned i=0;i<3;++i){Four input[16],v;assert(sampledVertexInputsAt(p,*source,decl,3,i,input)&&evaluateFast(p,input,g.constants,v));for(unsigned k=0;k<3;++k)assert(v[k]>=low[k]&&v[k]<=high[k]);}};
        bool ready=false;for(unsigned f=0;f<20&&!ready;++f){for(unsigned i=0;i<3;++i)g.constants[4*i+2]=100+float(f+i);ready=g.distantPoseBounds(p,source,decl,3,low,high);g.endFrame();}assert(ready);
        for(float z:{200.f,-200.f}){g.endFrame();for(unsigned i=0;i<3;++i)g.constants[4*i+2]=z+float(i);g.poseVerticesThisFrame=8192;g.lastDepthLower=0;check();assert(g.lastDepthLower>100);}
        for(float z:{0.f,3.f}){g.endFrame();for(unsigned i=0;i<3;++i)g.constants[4*i+2]=z+float(i);g.lastDepthLower=0;assert(!g.distantPoseBounds(p,source,decl,3,low,high)&&g.lastDepthLower==0);} // Near bounds/fade remain on the original path.
        auto previous=source;mesh=std::make_shared<Mesh>(*source);float changed=500;for(unsigned i=0;i<3;++i)std::memcpy(mesh->streams[0].bytes.data()+16*i+8,&changed,4);source=mesh;
        ready=false;for(unsigned f=0;f<20&&!ready;++f){g.endFrame();ready=g.distantPoseBounds(p,source,decl,3,low,high);}assert(ready&&low[2]>500); // New immutable generation cannot reuse the near source envelope.
        previous=source;mesh=std::make_shared<Mesh>(*source);changed=0;std::memcpy(mesh->streams[0].bytes.data()+8,&changed,4);source=mesh;g.endFrame();assert(!g.distantPoseBounds(p,source,decl,3,low,high)); // A far vertex alone is not a culling proof.
        Program unsupported=p;unsupported.operations[1].code=6;assert(!g.distantPoseBounds(unsupported,source,decl,3,low,high));
        g.reset();assert(!g.poseEnvelopes.entries()&&!g.envelopeBudget.vertices);
        std::puts("DISPLACEMENT_POSE_ENVELOPE_PASS relative_palette current_pose new_source cold_fallback budget reset");
    }
    static void positionOnly(IDirect3DVertexDeclaration9* declaration){
        NorthlightAutoDisplacement g(nullptr);float vertices[]={1,2,60,0,3,4,61,0,5,6,62,0};Mesh mesh;
        const NorthlightDrawSnapshot::Draw draw{D3DPT_TRIANGLELIST,0,0,3,0,1,false};
        assert(g.rangeSnapshots.readUP(declaration,draw,nullptr,D3DFMT_UNKNOWN,vertices,16,mesh));
        assert(mesh.streams[0].stride==16&&mesh.vertexCount==3&&mesh.streams[1].bytes.empty());
        for(unsigned i=0;i<3;++i)assert(!std::memcmp(mesh.streams[0].bytes.data()+i*16,vertices+i*4,12));
        assert(!g.snapshots.readUP(declaration,draw,nullptr,D3DFMT_UNKNOWN,vertices,16,mesh));
        std::puts("MATERIAL_POSITION_ONLY_PASS unused_unbound_stream full_geometry_still_required");
    }
    static void rangeProgram(NorthlightAutoDisplacement& g,IDirect3DVertexShader9* key,bool skinned=false){
        using namespace NorthlightWaterShaderPatch;
        NorthlightAutoDisplacement::Material m;m.first=0;m.count=1;m.skinned=skinned;m.view.major=3;m.view.positionRegister=0;m.view.inputs.push_back({0,D3DDECLUSAGE_POSITION,0});
        NorthlightActorDeformation::Operation copy;copy.code=1;copy.destination=dst(0,0);copy.source[0].token=src(1,0);m.view.operations.push_back(copy);
        NorthlightActorDeformation::Operation move;move.code=2;move.destination=dst(0,0,7);move.source[0].token=src(0,0);move.source[1].token=src(2,0);m.view.operations.push_back(move);
        g.materials[key]=std::move(m);
        g.constants[2]=123;g.constantStamp.valid=true;
    }
    static void rangePreservesPose(const NorthlightAutoDisplacement& g){assert(g.constants[2]==123&&g.constantStamp.valid);}
    static void rangeBudget(NorthlightAutoDisplacement& g,IDirect3DVertexShader9* shader,IDirect3DVertexDeclaration9* decl){
        std::vector<float> vertices(90000,1.f);NorthlightAutoDisplacement::Args args{{D3DPT_TRIANGLELIST,0,0,30000,0,10000,false},nullptr,D3DFMT_UNKNOWN,vertices.data(),12};float nearest=0;
        for(unsigned i=0;i<8;++i)g.nearestMaterialDepth(shader,args,nearest);
        assert(g.rangeSnapshots.bytesRead()>0&&g.rangeSnapshots.bytesRead()<=(512u<<10));assert(g.snapshots.bytesRead()==0);
        NorthlightDrawSnapshot::Mesh mesh;assert(g.snapshots.readUP(decl,args.draw,nullptr,D3DFMT_UNKNOWN,vertices.data(),12,mesh));
        assert(g.snapshots.bytesRead()==vertices.size()*sizeof(float));g.endFrame();assert(!g.snapshots.bytesRead()&&!g.rangeSnapshots.bytesRead());
        assert(g.snapshots.snapshotCacheLimit()+g.rangeSnapshots.snapshotCacheLimit()==(32u<<20));
        std::puts("MATERIAL_RANGE_BUDGET_PASS isolated_reads warm_tracking combined_cache_cap");
    }
    static bool read(NorthlightAutoDisplacement& g,bool skinned=true){return g.readConstants(skinned?0:20,skinned?256:4,skinned);}
    static const float* values(const NorthlightAutoDisplacement& g){return g.constants;}
    static bool certified(const NorthlightAutoDisplacement& g){return g.constantStamp.valid;}
    static void declaration(NorthlightAutoDisplacement& g,IDirect3DVertexDeclaration9* d,UINT stride,bool accepted=true,bool indexed=false){
        const D3DVERTEXELEMENT9* e=nullptr;UINT n=0;assert(g.declarations.get(d,e,n));
        float vertices[16]={};NorthlightDrawSnapshot::Mesh mesh;
        const NorthlightDrawSnapshot::Draw draw{D3DPT_TRIANGLELIST,0,0,3,0,1,indexed};const uint16_t indices[]={0,1,2};
        assert(g.snapshots.readUP(d,draw,indexed?indices:nullptr,indexed?D3DFMT_INDEX16:D3DFMT_UNKNOWN,vertices,stride,mesh)==accepted);
        assert(n==2&&e[1].Stream==0xff); // The source end element must never be overwritten.
    }
};
void DisplacementStateTestAccess::cache(){
    using G=NorthlightAutoDisplacement;using Mesh=NorthlightDrawSnapshot::Mesh;using E=G::Entry;
    auto mesh=[](unsigned n){auto p=std::make_shared<Mesh>();p->vertexCount=3;p->primitiveCount=1;p->indexed=true;p->indices={0,1,2};p->streams[0].bytes.resize(256);std::memcpy(p->streams[0].bytes.data(),&n,sizeof n);return std::shared_ptr<const Mesh>(p);};
    auto entry=[](const std::shared_ptr<const Mesh>& s,size_t bytes,uint64_t used,unsigned program=1){auto e=std::make_unique<E>();e->source=s;e->bytes=bytes;e->used=used;e->domain=1;e->program=program;e->baseFrame=1;return e;};
    G g(nullptr);std::shared_ptr<const Mesh> query;
    for(unsigned i=0;i<256;++i){query=mesh(i);g.retain(entry(query,100,i+1));}
    unsigned visits=0;g.candidates(query,0,false,[&](const E& e){assert(e.source==query);++visits;});assert(visits==1);
    g.indexedEntries=false;unsigned linear=0;g.candidates(query,0,false,[&](const E&){++linear;});assert(linear==256);g.indexedEntries=true;
    auto alias=std::make_shared<Mesh>(*query);alias->streams[0].stride=96;alias->streams[1].bytes={1,2,3};
    const auto fingerprint=G::sourceFingerprint(*query);assert(G::sourceFingerprint(*alias)==fingerprint);
    auto find=[&](const std::shared_ptr<const Mesh>& s,bool equivalent,unsigned program=1){return g.firstCandidate(s,G::sourceFingerprint(*s),equivalent,[&](const E& e){return e.program==program&&G::sameSource(e,s,equivalent);});};
    assert(!find(alias,false)&&find(alias,true)->source==query);
    // A sampled-content collision must still compare the unsampled bytes.
    auto collision=std::make_shared<Mesh>(*query);collision->streams[0].bytes[128]=1;assert(G::sourceFingerprint(*collision)==fingerprint);assert(!find(collision,true)&&!g.findOverlay(collision,fingerprint,nullptr));
    auto* original=find(alias,true);g.retain(entry(alias,200,500,2));assert(find(alias,true)==original&&find(alias,true,2)->source==alias);
    original->uvConstants={-0.f};const std::vector<float> zero={0.f},nan={std::numeric_limits<float>::quiet_NaN()};
    assert(g.firstCandidate(query,fingerprint,true,[&](const E& e){return e.uvConstants==zero;})==original);
    assert(!g.firstCandidate(query,fingerprint,true,[&](const E& e){return e.uvConstants==nan;}));
    auto keyed=entry(query,100,501,3);keyed->uvConstants={-0.f};auto* keyedPointer=keyed.get();g.retain(std::move(keyed));
    auto key=[&](const E& e,const std::vector<float>& uv){return G::materialFingerprint(G::sourceFingerprint(*e.source),e.program,e.detail,e.address,e.domain,e.material,e.texture,e.texture1,e.heightVersion,e.heightVersion1,uv);};
    const auto exact=key(*keyedPointer,zero);assert(exact==keyedPointer->materialFingerprint);
    assert(g.firstMaterial(exact,[&](const E& e){return e.uvConstants==zero&&G::sameSource(e,alias,true);})==keyedPointer);
    assert(!g.firstMaterial(exact,[&](const E& e){return e.uvConstants==nan;})); // Even a hash collision cannot authorize NaN equality.
    assert(!g.firstMaterial(exact,[&](const E& e){return G::sameSource(e,collision,true);}));
    for(unsigned field=0;field<10;++field){auto changed=entry(query,100,1,3);changed->uvConstants=zero;
        switch(field){case 0:changed->program=4;break;case 1:changed->detail=1;break;case 2:changed->address=1;break;case 3:changed->domain=2;break;case 4:changed->material=1;break;case 5:changed->texture=reinterpret_cast<IDirect3DTexture9*>(uintptr_t(1));break;case 6:changed->texture1=reinterpret_cast<IDirect3DTexture9*>(uintptr_t(1));break;case 7:changed->heightVersion=1;break;case 8:changed->heightVersion1=1;break;case 9:changed->uvConstants={1};break;}
        const auto changedKey=key(*changed,changed->uvConstants);changed->texture=changed->texture1=nullptr;assert(!g.firstMaterial(changedKey,[](const E&){return true;}));}
    keyedPointer->baseFrame=0;
    // Latest successful base wins, and equal stamps preserve insertion order.
    assert(g.findOverlay(query,fingerprint,nullptr)->source==alias);original->used=500;assert(g.findOverlay(query,fingerprint,nullptr)==original);
    g.candidates(query,fingerprint,true,[&](E& e){if(G::sameSource(e,query,true))e.baseFrame=0;});assert(!g.findOverlay(query,fingerprint,nullptr));
    // Source sharing charges only one payload and releases that charge on its last owner.
    g.reset();auto shared=mesh(42);const auto sourceBytes=shared->byteSize();std::weak_ptr<const Mesh> lifetime=shared;
    for(unsigned i=0;i<10;++i)g.retain(entry(shared,100,i+1));assert(g.resident==1000+sourceBytes);
    for(unsigned i=0;i<9;++i){g.evictOldest();assert(g.resident==(9-i)*100+sourceBytes);assert(g.sourceLookup.size()==9-i&&g.contentLookup.size()==9-i&&g.materialLookup.size()==9-i);}
    shared.reset();assert(!lifetime.expired());g.evictOldest();assert(lifetime.expired()&&g.resident==0&&g.sourceLookup.empty()&&g.contentLookup.empty());
    // Evicting the last old owner while admitting a new variant restores the source charge.
    shared=mesh(7);g.retain(entry(shared,100,1));g.cacheBudget=sourceBytes+110;g.makeRoom(shared,110);assert(g.entries.empty());g.retain(entry(shared,110,2));assert(g.resident==sourceBytes+110);
    auto another=mesh(8);g.retain(entry(another,100,3));g.memoryPressure=true;MEMORYSTATUSEX memory{};memory.ullAvailVirtual=1024ull<<20;
    // Pressure uses the same eviction path, including every candidate index.
    g.entries[0]->bytes=40u<<20;g.resident+=40u<<20;g.resident-=110;g.refreshBudget(memory);assert(g.resident==100+another->byteSize()&&g.entries.size()==1);
    assert(!find(shared,false)&&find(another,false));g.reset();assert(g.resident==0&&g.sourceLookup.empty()&&g.contentLookup.empty());
    // Both partial index-population failures must leave the ordered fallback usable.
    for(int failure=0;failure<3;++failure){g.entries.reserve(4);g.sourceLookup.reserve(4);g.materialLookup.reserve(4);auto next=entry(shared,100,1);failAllocationAfter=failure;g.retain(std::move(next));assert(failAllocationAfter==-1&&!g.indexedEntries&&g.sourceLookup.empty()&&g.contentLookup.empty()&&g.materialLookup.empty());assert(find(shared,false)&&g.firstMaterial(0,[](const E&){return true;}));g.reset();}
    {G empty(nullptr);auto next=entry(shared,100,1);failAllocationAfter=0;bool failed=false;try{empty.retain(std::move(next));}catch(const std::bad_alloc&){failed=true;}assert(failed&&empty.resident==0&&empty.entries.empty()&&empty.sourceLookup.empty());}
    // Unknown and future errors remain visible rather than disappearing from the total.
    for(unsigned i=0;i<g.snapshotReasons.size();++i){NorthlightDrawSnapshot::Diagnostics why;why.error=NorthlightDrawSnapshot::Error(i);assert(!g.snapshotFailure(why));}
    NorthlightDrawSnapshot::Diagnostics unknown;unknown.error=NorthlightDrawSnapshot::Error(999);g.snapshotFailure(unknown);uint64_t sum=0;for(auto count:g.snapshotReasons)sum+=count;assert(sum==g.snapshotRejected&&g.snapshotReasons[0]==2);
    std::printf("DISPLACEMENT_LOOKUP_PASS indexedVisits=%u linearVisits=%u sharedBytes=%zu repeatedChargeBytes=%zu\n",visits,linear,1000+sourceBytes,10*(100+sourceBytes));
}
struct Backend:ForwardIDirect3DDevice9 {
    float bank[1024]={};bool fail=false;IDirect3DDevice9* mutate=nullptr;
    Backend():ForwardIDirect3DDevice9(nullptr){}
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT first,float* out,UINT count)override{
        if(fail)return D3DERR_INVALIDCALL;std::memcpy(out,bank+first*4,count*16);
        if(mutate){auto* writer=mutate;mutate=nullptr;const float changed[4]={21,22,23,24};assert(SUCCEEDED(writer->SetVertexShaderConstantF(64,changed,1)));}return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantF(UINT first,const float* in,UINT count)override{if(fail)return D3DERR_INVALIDCALL;std::memcpy(bank+first*4,in,count*16);return D3D_OK;}
};
struct Meter:ForwardIDirect3DDevice9 {
    unsigned calls=0,registers=0;bool fail=false;explicit Meter(IDirect3DDevice9* d):ForwardIDirect3DDevice9(d){}
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT first,float* out,UINT count)override{++calls;registers+=count;return fail?D3DERR_INVALIDCALL:real->GetVertexShaderConstantF(first,out,count);}
};
struct Declaration:IDirect3DVertexDeclaration9 {
    unsigned refs=1,reads=0;D3DVERTEXELEMENT9 elements[2];bool fail=false;
    explicit Declaration(WORD offset=0,WORD stream=0):elements{{stream,offset,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},D3DDECL_END()}{}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**)override{return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{assert(refs);return --refs;}
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9**)override{return E_FAIL;}
    HRESULT STDMETHODCALLTYPE GetDeclaration(D3DVERTEXELEMENT9* out,UINT* count)override{++reads;if(fail||*count<2)return D3DERR_INVALIDCALL;std::memcpy(out,elements,sizeof elements);*count=2;return D3D_OK;}
};
struct RangeBackend:Backend {
    Declaration decl;
    HRESULT STDMETHODCALLTYPE GetVertexDeclaration(IDirect3DVertexDeclaration9** out)override{*out=&decl;decl.AddRef();return D3D_OK;}
};
struct SplitDeclaration:Declaration {
    HRESULT STDMETHODCALLTYPE GetDeclaration(D3DVERTEXELEMENT9* out,UINT* count)override{
        if(*count<3)return D3DERR_INVALIDCALL;const D3DVERTEXELEMENT9 e[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{1,0,D3DDECLTYPE_D3DCOLOR,0,D3DDECLUSAGE_COLOR,0},D3DDECL_END()};std::memcpy(out,e,sizeof e);*count=3;return D3D_OK;
    }
};
template<class Interface> struct CaptureShader:Interface {
    unsigned refs=1,reads=0;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**)override{return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{assert(refs);return --refs;}
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9**)override{return E_FAIL;}
    HRESULT STDMETHODCALLTYPE GetFunction(void*,UINT*)override{++reads;return E_FAIL;}
};
struct CaptureSurface:IDirect3DSurface9 {
    unsigned refs=1;UINT size=64;D3DMULTISAMPLE_TYPE samples=D3DMULTISAMPLE_2_SAMPLES;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**)override{return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{assert(refs);return --refs;}
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9**)override{return E_FAIL;}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID,const void*,DWORD,DWORD)override{return E_FAIL;}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID,void*,DWORD*)override{return E_FAIL;}
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID)override{return E_FAIL;}
    DWORD STDMETHODCALLTYPE SetPriority(DWORD)override{return 0;}
    DWORD STDMETHODCALLTYPE GetPriority()override{return 0;}
    void STDMETHODCALLTYPE PreLoad()override{}
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType()override{return D3DRTYPE_SURFACE;}
    HRESULT STDMETHODCALLTYPE GetContainer(REFIID,void**)override{return E_FAIL;}
    HRESULT STDMETHODCALLTYPE GetDesc(D3DSURFACE_DESC* out)override{*out={D3DFMT_A8R8G8B8,D3DRTYPE_SURFACE,D3DUSAGE_RENDERTARGET,D3DPOOL_DEFAULT,samples,0,size,size};return D3D_OK;}
    HRESULT STDMETHODCALLTYPE LockRect(D3DLOCKED_RECT*,const RECT*,DWORD)override{return E_FAIL;}
    HRESULT STDMETHODCALLTYPE UnlockRect()override{return E_FAIL;}
    HRESULT STDMETHODCALLTYPE GetDC(HDC*)override{return E_FAIL;}
    HRESULT STDMETHODCALLTYPE ReleaseDC(HDC)override{return E_FAIL;}
};
struct CaptureBackend:Backend {
    CaptureShader<IDirect3DVertexShader9> vs;CaptureShader<IDirect3DPixelShader9> ps;CaptureSurface surface;
    struct SkinDeclaration:Declaration {
        HRESULT STDMETHODCALLTYPE GetDeclaration(D3DVERTEXELEMENT9* out,UINT* count)override{
            if(*count<3)return D3DERR_INVALIDCALL;
            const D3DVERTEXELEMENT9 e[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},D3DDECL_END()};std::memcpy(out,e,sizeof e);*count=3;return D3D_OK;
        }
    } decl;
    unsigned pixelConstants=0,textures=0,samplers=0,depthAllocations=0;
    HRESULT STDMETHODCALLTYPE GetPixelShader(IDirect3DPixelShader9** out)override{*out=&ps;ps.AddRef();return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetVertexDeclaration(IDirect3DVertexDeclaration9** out)override{*out=&decl;decl.AddRef();return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT9* out)override{*out={0,0,surface.size,surface.size,0,1};return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetRenderTarget(DWORD n,IDirect3DSurface9** out)override{*out=n?nullptr:&surface;if(n)return D3DERR_NOTFOUND;surface.AddRef();return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface9** out)override{*out=&surface;surface.AddRef();return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE key,DWORD* out)override{
        *out=key==D3DRS_ZENABLE||key==D3DRS_ZWRITEENABLE?TRUE:key==D3DRS_ZFUNC?D3DCMP_LESSEQUAL:key==D3DRS_COLORWRITEENABLE?15:0;return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantF(UINT,float*,UINT)override{++pixelConstants;return E_FAIL;}
    HRESULT STDMETHODCALLTYPE GetSamplerState(DWORD,D3DSAMPLERSTATETYPE,DWORD*)override{++samplers;return E_FAIL;}
    HRESULT STDMETHODCALLTYPE GetTexture(DWORD,IDirect3DBaseTexture9** out)override{++textures;*out=nullptr;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(UINT,UINT,D3DFORMAT,D3DMULTISAMPLE_TYPE,DWORD,BOOL,IDirect3DSurface9**,HANDLE*)override{++depthAllocations;return E_FAIL;}
};
struct MaterialCaptureStateTestAccess {
    static NorthlightAutoDisplacement& setup(NorthlightAutoBump& b,CaptureBackend& d,unsigned domain){
        b.enabled=true;b.width=b.heightPixels=d.surface.size;
        for(auto& mode:b.modes)mode=0;b.modes[domain-1]=3;
        d.vs.AddRef();b.vertices[&d.vs]={&d.vs,domain};b.pixels[&d.ps]=1;return b.displacement;
    }
    static unsigned rejected(const NorthlightAutoBump& b){return b.rangeRejected;}
};
void DisplacementStateTestAccess::capture(){
    using namespace NorthlightWaterShaderPatch;using namespace NorthlightActorDeformation;
    for(unsigned domain:{1u,4u})for(bool skinned:{false,true})for(auto samples:{D3DMULTISAMPLE_NONE,D3DMULTISAMPLE_2_SAMPLES}){
        CaptureBackend d;d.surface.samples=samples;{
            NorthlightAutoBump b(&d);auto& g=MaterialCaptureStateTestAccess::setup(b,d,domain);
            NorthlightAutoDisplacement::Material m;m.skinned=true;m.id=1;m.count=3;m.view.major=3;m.view.positionRegister=0;m.view.skinned=true;
            m.view.inputs={{0,D3DDECLUSAGE_POSITION,0},{1,D3DDECLUSAGE_BLENDINDICES,0}};
            Operation address;address.code=46;address.destination=dst(3,0,1);address.source[0].token=src(1,1,0);m.view.operations.push_back(address);
            Operation add;add.code=2;add.destination=dst(0,0);add.source[0].token=src(1,0);add.source[1].token=src(2,0)|0x2000;add.source[1].address=src(3,0,0);m.view.operations.push_back(add);
            if(!skinned){m.skinned=m.view.skinned=false;m.view.inputs.resize(1);m.view.operations.erase(m.view.operations.begin());m.view.operations[0].source[1].token=src(2,0);m.view.operations[0].source[1].address=0;}
            g.materials[&d.vs]=std::move(m);d.vs.AddRef();g.shaders[&d.vs].patched=&d.vs;
            struct V{float x,y,z;unsigned bone;} vertices[]={{0,0,0,0},{1,0,0,1},{0,1,0,2}};
            NorthlightAutoDisplacement::Args args{{D3DPT_TRIANGLELIST,0,0,3,0,1,false},nullptr,D3DFMT_UNKNOWN,vertices,16};
            auto seed=[&]{Mesh mesh;assert(g.snapshots.readUP(&d.decl,args.draw,nullptr,D3DFMT_UNKNOWN,vertices,16,mesh));
                auto e=std::make_unique<NorthlightAutoDisplacement::Entry>();e->source=std::make_shared<Mesh>(std::move(mesh));e->sourceDecl=&d.decl;d.decl.AddRef();e->program=1;e->domain=domain;g.retain(std::move(e));};
            unsigned replayed=0;auto capture=[&]{b.beginDraw();b.capture(&d.vs,[&]{++replayed;return D3D_OK;},true,&args);};
            for(UINT size:{64u,128u}){ // A size change clears geometry; warm a fresh immutable source afterwards.
                d.surface.size=size;for(unsigned i=0;i<3;++i)d.bank[4*i+2]=100;
                capture();seed();bool ready=false;
                for(unsigned f=0;f<20&&!ready;++f){b.endFrame();capture();ready=MaterialCaptureStateTestAccess::rejected(b)>0;}assert(ready);
                const auto reads=d.ps.reads,constants=d.pixelConstants,textures=d.textures,samplers=d.samplers,allocations=d.depthAllocations;
                for(unsigned f=0;f<8;++f){b.endFrame();capture();assert(MaterialCaptureStateTestAccess::rejected(b)>0);}
                assert(d.ps.reads==reads&&d.pixelConstants==constants&&d.textures==textures&&d.samplers==samplers&&d.depthAllocations==allocations&&!replayed);
                unsigned native=0;assert(SUCCEEDED(b.nativeDraw([&]{++native;return D3D_OK;}))&&native==1);
                b.endFrame();for(unsigned i=0;i<3;++i)d.bank[4*i+2]=0;capture();assert(!MaterialCaptureStateTestAccess::rejected(b));
                if(domain==1)assert(d.pixelConstants==constants+1); // Near/unknown proof reaches capture, even when its state save fails.
                b.endFrame();for(unsigned i=0;i<3;++i)d.bank[4*i+2]=NAN;capture();assert(!MaterialCaptureStateTestAccess::rejected(b));
                b.endFrame();for(unsigned i=0;i<3;++i)d.bank[4*i+2]=100;
                auto& op=g.materials[&d.vs].view.operations.back();const auto code=op.code;op.code=6;capture();assert(!MaterialCaptureStateTestAccess::rejected(b));op.code=code;
            }
        }assert(d.vs.refs==1&&d.ps.refs==1&&d.surface.refs==1&&d.decl.refs==1);
    }
    std::puts("MATERIAL_CAPTURE_ADMISSION_PASS farDraws=128 rigid_skinned zero_capture_setup near_unknown_nonfinite_fail_open native_retained msaa resize refs");
}
int main(){
    DisplacementStateTestAccess::capture();
    DisplacementStateTestAccess::cache();
    DisplacementStateTestAccess::posed();
    {SplitDeclaration decl;DisplacementStateTestAccess::positionOnly(&decl);}
    {RangeBackend backend;NorthlightAutoDisplacement g(&backend);auto* shader=reinterpret_cast<IDirect3DVertexShader9*>(uintptr_t(1));
     DisplacementStateTestAccess::rangeProgram(g,shader);
     float vertices[]={0,0,50000,1,0,50001,0,1,50002};NorthlightAutoDisplacement::Args args{{D3DPT_TRIANGLELIST,0,0,3,0,1,false},nullptr,D3DFMT_UNKNOWN,vertices,12};float nearest=0;
     for(float distance:{47.99f,48.f,48.01f,60.f,150.f}){backend.bank[2]=distance-50000;assert(g.nearestMaterialDepth(shader,args,nearest));assert(nearest<=double(vertices[2])+backend.bank[2]);if(distance>=60)assert(nearest>48);}
     backend.bank[2]=-50000;assert(g.nearestMaterialDepth(shader,args,nearest)&&nearest==0);
     DisplacementStateTestAccess::rangePreservesPose(g);
     DisplacementStateTestAccess::rangeProgram(g,shader,true);assert(!g.nearestMaterialDepth(shader,args,nearest));
     DisplacementStateTestAccess::rangeProgram(g,shader);vertices[2]=NAN;assert(!g.nearestMaterialDepth(shader,args,nearest));
     DisplacementStateTestAccess::rangeBudget(g,shader,&backend.decl);
     std::puts("MATERIAL_RANGE_PASS conservative_large_translation boundary_pose_changes unknown_skinned nonfinite");}
    Backend backend;DeviceMirror mirror;ExtensionDevice extension(&backend,&mirror);Meter meter(&extension);
    NorthlightAutoDisplacement g(&meter);
    auto attach=[&]{g.setConstantEpochSource(&extension,[](void* c,NorthlightConstantEpoch::Stamp& s){return static_cast<ExtensionDevice*>(c)->constantStamp(s);});};
    auto read=[&]{assert(DisplacementStateTestAccess::read(g));assert(!std::memcmp(DisplacementStateTestAccess::values(g),backend.bank,sizeof backend.bank));};
    attach();read();assert(meter.calls==1&&meter.registers==256);
    for(unsigned i=0;i<100;++i)read();assert(meter.calls==1); // No call or bank copy on an unchanged epoch.
    float row[8]={3,4,5,6,7,8,9,10};assert(SUCCEEDED(extension.SetVertexShaderConstantF(31,row,2)));
    read();assert(meter.calls==2&&meter.registers==288); // A write crossing two blocks reads both.
    assert(SUCCEEDED(extension.SetVertexShaderConstantF(252,row,1)));read();assert(meter.registers==304);
    assert(SUCCEEDED(extension.SetVertexShaderConstantF(64,row,1)));meter.fail=true;assert(!DisplacementStateTestAccess::read(g));assert(!DisplacementStateTestAccess::certified(g));meter.fail=false;read();
    assert(meter.registers==576);const auto invalidatedStart=meter.registers;
    mirror.invalidate();backend.bank[70*4]=9;read();assert(meter.registers==invalidatedStart+256);
    {ExtensionDevice::RawScope raw(extension);backend.bank[80*4]=10;read();assert(!DisplacementStateTestAccess::certified(g));}
    read();assert(meter.registers==invalidatedStart+768);
    mirror.recording=true;mirror.invalidate();read();assert(!DisplacementStateTestAccess::certified(g));mirror.recording=false;mirror.invalidate();read();
    backend.fail=true;mirror.invalidate();assert(!DisplacementStateTestAccess::read(g));assert(!DisplacementStateTestAccess::certified(g));backend.fail=false;read();
    mirror.enabled=false;mirror.invalidate();backend.bank[90*4]=11;read();assert(!DisplacementStateTestAccess::certified(g));mirror.enabled=true;mirror.invalidate();read();
    mirror.invalidate();const auto old=backend.bank[64*4];backend.mutate=&extension;assert(DisplacementStateTestAccess::read(g));assert(DisplacementStateTestAccess::values(g)[64*4]==old);
    assert(!DisplacementStateTestAccess::certified(g)&&!mirror.vsFloatKnown[64]);read();assert(DisplacementStateTestAccess::certified(g)&&DisplacementStateTestAccess::values(g)[64*4]==21);
    const auto before=meter.calls;assert(DisplacementStateTestAccess::read(g,false));read();assert(meter.calls==before+2);
    g.setConstantEpochSource(nullptr,nullptr);read();read();assert(!DisplacementStateTestAccess::certified(g));attach();read();g.reset();read();
    // A new device's equal serials cannot inherit the old object's bank.
    Backend other;other.bank[0]=12;DeviceMirror otherMirror;ExtensionDevice otherExtension(&other,&otherMirror);NorthlightAutoDisplacement next(&otherExtension);
    next.setConstantEpochSource(&otherExtension,[](void* c,NorthlightConstantEpoch::Stamp& s){return static_cast<ExtensionDevice*>(c)->constantStamp(s);});assert(DisplacementStateTestAccess::read(next));assert(DisplacementStateTestAccess::values(next)[0]==12);

    Declaration decl;for(unsigned i=0;i<100;++i)DisplacementStateTestAccess::declaration(g,&decl,12,true,i%2);assert(decl.reads==1&&decl.refs==2);
    g.reset();assert(decl.refs==1);DisplacementStateTestAccess::declaration(g,&decl,12);assert(decl.reads==2);
    Declaration unsupported(0,4);DisplacementStateTestAccess::declaration(g,&unsupported,12,false);assert(unsupported.reads==1);
    Declaration failed;failed.fail=true;const D3DVERTEXELEMENT9* elements=nullptr;UINT count=0;
    NorthlightVertexDeclarations::Cache cache;assert(!cache.get(&failed,elements,count));failed.fail=false;assert(cache.get(&failed,elements,count));assert(failed.reads==2);
    std::vector<std::unique_ptr<Declaration>> layouts;for(unsigned i=0;i<129;++i){layouts.push_back(std::make_unique<Declaration>());assert(cache.get(layouts.back().get(),elements,count));}
    assert(layouts[0]->refs==1);assert(cache.get(layouts[0].get(),elements,count));assert(layouts[0]->reads==2);cache.clear();for(auto& layout:layouts)assert(layout->refs==1);
    alignas(Declaration) unsigned char storage[sizeof(Declaration)];auto* reused=new(storage) Declaration;
    DisplacementStateTestAccess::declaration(g,reused,12);assert(reused->refs==2);g.reset();assert(reused->refs==1);reused->~Declaration();
    reused=new(storage) Declaration(4);DisplacementStateTestAccess::declaration(g,reused,16);assert(reused->reads==1&&reused->elements[0].Offset==4);g.reset();reused->~Declaration();
    std::puts("PASS displacement state reuse: exact and partial epochs, invalidation, inactive sources, failed/raced reads, source replacement, immutable declarations, UP layouts and address reuse");
}
