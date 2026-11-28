#include "point_shadow_face_pass.h"
#include "point_cube_cache.h"
#include "point_opaque_face_cache.h"
#include "point_rigid_pose_rows.h"
#include "shadow_pass_device.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

struct Fixture {
    struct Check {
        Fixture* fixture;
        bool operator()(HRESULT hr,const char* operation) const {
            if(FAILED(hr))fixture->failure=operation;
            return SUCCEEDED(hr);
        }
    };
    Device device;
    const char* failure=nullptr;
    bool minBlend=true,canPair=true;
    IDirect3DSurface9 *working=handle<IDirect3DSurface9>(1),*cached=handle<IDirect3DSurface9>(2),
        *scratch=handle<IDirect3DSurface9>(3),*depth=handle<IDirect3DSurface9>(4);
    IDirect3DTexture9 *cacheTex=handle<IDirect3DTexture9>(5),*scratchTex=handle<IDirect3DTexture9>(6);
    IDirect3DPixelShader9 *caster=handle<IDirect3DPixelShader9>(7),*merge=handle<IDirect3DPixelShader9>(8);
    auto pass(){return NorthlightPointShadow::FacePass(&device,Check{this},256,depth,caster);}
};

static std::vector<Call> prepare(){
    std::vector<Call> expected;
    for(unsigned i=0;i<13;++i)expected.push_back({Texture,i});
    for(unsigned i=1;i<4;++i)expected.push_back({Target,i});
    for(unsigned i=0;i<4;++i)expected.push_back({Frequency,i,1});
    expected.push_back({State,D3DRS_ALPHABLENDENABLE,FALSE});
    expected.push_back({State,D3DRS_COLORWRITEENABLE,15});
    return expected;
}
static void append(std::vector<Call>& out,std::initializer_list<Call> calls){out.insert(out.end(),calls);}
static void defaults(std::vector<Call>& out){
    append(out,{{Texture,0},{Texture,1},{State,D3DRS_ZENABLE,TRUE},{State,D3DRS_ZWRITEENABLE,TRUE},
        {Sampler,0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR},{Sampler,0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR}});
}

static void pairedAndNextFace(){
    Fixture f;
    auto pass=f.pass();
    assert(pass.beginFace(f.working,12));
    assert(pass.beginStatic(f.cached,true,f.canPair));
    assert(pass.seeded()&&f.device.attached==2);
    const float matrix[16]={};
    assert(pass.bindWorldGeometry(handle<IDirect3DVertexDeclaration9>(9),handle<IDirect3DVertexShader9>(10),matrix,
        handle<IDirect3DVertexBuffer9>(11),36,handle<IDirect3DIndexBuffer9>(12)));
    assert(pass.material(f.cacheTex,.5f)&&pass.material(f.cacheTex,.5f));
    f.device.record({StaticDraw});
    assert(pass.finishStatic()&&!f.device.attached);
    pass.chooseDestination(true,f.cached,false,f.minBlend);
    assert(pass.beginDynamic(f.scratch,f.minBlend,[]{assert(false);return false;}));
    f.device.record({DynamicDraw});
    assert(pass.resolve(false,f.cacheTex,f.scratchTex,f.merge));
    pass.finishFace();
    auto expected=prepare();
    append(expected,{{Depth},{Target,0,1},{State,D3DRS_COLORWRITEENABLE1,15},{Target,1,2},{Depth,4},{Viewport,256,256},
        {Op::Clear,D3DCLEAR_ZBUFFER|D3DCLEAR_TARGET,0xffffffff},{Pixel,7},{PixelRows,1,1},
        {Layout,9},{Vertex,10},{VertexRows,0,4},{Stream,11,36},{Index,12},{Sampler,0,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP},
        {Sampler,0,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP},{Texture,0,5},{PixelRows,0,1},{StaticDraw},{Target,1},
        {Op::Clear,D3DCLEAR_ZBUFFER,0},{State,D3DRS_SRCBLEND,D3DBLEND_ONE},{State,D3DRS_DESTBLEND,D3DBLEND_ONE},
        {State,D3DRS_BLENDOP,D3DBLENDOP_MIN},{State,D3DRS_ALPHABLENDENABLE,TRUE},{DynamicDraw},
        {State,D3DRS_ALPHABLENDENABLE,FALSE},{State,D3DRS_BLENDOP,D3DBLENDOP_ADD}});
    defaults(expected);
    assert(f.device.calls==expected);
    assert(pass.beginFace(f.working,12)&&!pass.seeded());
    assert(f.device.calls==expected);
    assert(pass.material(f.cacheTex,.5f));
    append(expected,{{Texture,0,5},{PixelRows,0,1}});
    assert(f.device.calls==expected);
}

static void copiedAndUnion(bool copyFails,bool staticOnly){
    Fixture f;
    NorthlightPointShadow::BaseCopyProfile copies;
    NorthlightPointShadow::FacePass pass(&f.device,Fixture::Check{&f},256,f.depth,f.caster,nullptr,&copies);
    assert(pass.beginFace(f.working,12));
    auto expected=prepare();
    if(copyFails)f.device.fault={Copy,2,1};
    pass.chooseDestination(true,f.cached,staticOnly,f.minBlend);
    assert(pass.usesUnion()==copyFails&&f.minBlend!=copyFails);
    assert(copies.attempts==1&&copies.failures==unsigned(copyFails));
    append(expected,{{Copy,2,1}});
    if(!staticOnly){
        assert(pass.beginDynamic(f.scratch,f.minBlend,[&]{return SUCCEEDED(f.device.record({Geometry}));}));
        f.device.record({DynamicDraw});
        append(expected,{{Depth},{Target,0,copyFails?3u:1u},{Depth,4},{Viewport,256,256},
            {Op::Clear,D3DCLEAR_ZBUFFER|(copyFails?D3DCLEAR_TARGET:0u),0xffffffff},{Pixel,7},{PixelRows,1,1},{Geometry}});
        if(!copyFails)append(expected,{{State,D3DRS_SRCBLEND,D3DBLEND_ONE},{State,D3DRS_DESTBLEND,D3DBLEND_ONE},
            {State,D3DRS_BLENDOP,D3DBLENDOP_MIN},{State,D3DRS_ALPHABLENDENABLE,TRUE}});
        append(expected,{{DynamicDraw}});
    }
    assert(pass.resolve(staticOnly,f.cacheTex,f.scratchTex,f.merge));
    if(copyFails){
        append(expected,{{Depth},{Target,0,1},{Viewport,256,256},{State,D3DRS_ZENABLE,FALSE},{State,D3DRS_ZWRITEENABLE,FALSE},
            {Vertex},{Fvf,D3DFVF_XYZRHW|D3DFVF_TEX1},{Frequency,0,1},{Stage,0,D3DTSS_TEXCOORDINDEX,0},
            {Stage,0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE},{Texture,0,staticOnly?5u:6u},{Texture,1,5}});
        for(unsigned i=0;i<2;++i)append(expected,{{Sampler,i,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP},
            {Sampler,i,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP},{Sampler,i,D3DSAMP_MINFILTER,D3DTEXF_POINT},
            {Sampler,i,D3DSAMP_MAGFILTER,D3DTEXF_POINT},{Sampler,i,D3DSAMP_MIPFILTER,D3DTEXF_NONE},
            {Sampler,i,D3DSAMP_SRGBTEXTURE,FALSE}});
        append(expected,{{PixelRows,0,2},{Pixel,8},{Viewport,256,256},{Quad,256,256}});
    }else if(!staticOnly)append(expected,{{State,D3DRS_ALPHABLENDENABLE,FALSE},{State,D3DRS_BLENDOP,D3DBLENDOP_ADD}});
    pass.finishFace();
    defaults(expected);
    assert(f.device.calls==expected);
}

static void reuseEmptyAuxiliaryTargets(){
    Fixture f;
    unsigned empty=0;
    {
        NorthlightPointShadow::FacePass pass(&f.device,Fixture::Check{&f},256,f.depth,f.caster,&empty);
        assert(pass.beginFace(f.working,12)&&empty==14u);
        assert(pass.beginStatic(f.cached,true,f.canPair)&&empty==12u);
        assert(pass.finishStatic()&&empty==14u);
    }
    f.device.calls.clear();
    {
        NorthlightPointShadow::FacePass pass(&f.device,Fixture::Check{&f},256,f.depth,f.caster,&empty);
        assert(pass.beginFace(f.working,12)&&empty==14u);
        assert(std::none_of(f.device.calls.begin(),f.device.calls.end(),[](const Call& call){
            return call==Call{Target,1}||call==Call{Target,2}||call==Call{Target,3};
        }));
        assert(pass.beginStatic(f.cached,true,f.canPair)&&empty==12u);
        f.device.fault={Target,1};
        assert(!pass.finishStatic()&&empty==12u);
    }
    assert(empty==14u&&!f.device.attached);
}

static void failuresAndReset(){
    Fixture f;
    {
        auto pass=f.pass();
        assert(pass.beginFace(f.working,12));
        f.device.fault={Target,1,2};
        assert(pass.beginStatic(f.cached,true,f.canPair));
        assert(!pass.seeded()&&!f.canPair&&!f.failure);
        assert(std::count(f.device.calls.begin(),f.device.calls.end(),Call{Target,0,2})==1);
        assert(pass.finishStatic());
    }
    {
        f.canPair=true;
        auto pass=f.pass();
        assert(pass.beginFace(f.working,12)&&pass.beginStatic(f.cached,true,f.canPair));
        f.device.fault={Target,1};
        assert(!pass.finishStatic());
        assert(f.device.attached==2&&f.failure);
    }
    assert(!f.device.attached&&f.device.calls.back()==(Call{Target,1}));
    // Reset is between cube submissions; there are no borrowed references left.
    f.device.calls.clear();
    f.failure=nullptr;
    auto afterReset=f.pass();
    assert(afterReset.beginFace(f.working,12));
    afterReset.chooseDestination(false,nullptr,true,f.minBlend);
    assert(afterReset.resolve(true,nullptr,f.scratchTex,f.merge));
    auto expected=prepare();
    append(expected,{{Depth},{Target,0,1},{Viewport,256,256},{Op::Clear,D3DCLEAR_TARGET,0xffffffff}});
    assert(f.device.calls==expected);
    assert(afterReset.beginFace(f.working,12));
    afterReset.chooseDestination(true,f.cached,false,f.minBlend);
    f.device.fault={State,D3DRS_BLENDOP,D3DBLENDOP_MIN};
    assert(!afterReset.beginDynamic(f.scratch,f.minBlend,[]{return true;}));
    assert(!f.minBlend&&f.failure&&f.device.calls.back()==(Call{State,D3DRS_BLENDOP,D3DBLENDOP_MIN}));
}

static void refusedStaticAllocation(){
    Fixture f;
    NorthlightPointShadow::CubeSlot slot;
    auto pass=f.pass();
    assert(pass.beginFace(f.working,12));
    const auto complete=[&]{
        return slot.ensureStaticFace(0,[](IDirect3DTexture9**){return E_OUTOFMEMORY;},Fixture::Check{&f})&&
            pass.beginStatic(slot.cacheSurface[0],true,f.canPair);
    };
    assert(!complete());
    assert(std::strcmp(f.failure,"cube cache face")==0&&f.canPair&&!slot.ready);
    assert(!slot.cacheFace[0]&&!slot.cacheSurface[0]&&f.device.calls==prepare());
}

static void meshRanges(bool terrain,bool drawFails){
    Fixture f;
    auto pass=f.pass();
    assert(pass.beginFace(f.working,12));
    f.device.calls.clear();
    using Pass=decltype(pass)::MeshPass;
    struct Batch{UINT material,page,minVertex,vertexCount,start,count;bool pointOnly;};
    const Batch ranges[]={{0,2,8,5,12,3,true},{0,2,11,12,21,4,false}};
    const auto next=[&](size_t& at){return ranges[at++];};
    const auto bind=[&](const Batch& batch,UINT& page){
        if(page==batch.page)return true;
        page=batch.page;return SUCCEEDED(f.device.record({Geometry,page}));
    };
    unsigned triangles=0;
    if(drawFails)f.device.fault={IndexedDraw,12,3,(8u<<16)|5};
    assert(pass.drawMeshRanges(2,terrain?Pass::Terrain:Pass::Static,{f.cacheTex},{.5f},
        next,bind,[&](UINT count){triangles+=count;})!=drawFails);
    std::vector<Call> expected;
    if(!terrain)append(expected,{{GetSampler,0,D3DSAMP_MINFILTER},{GetSampler,0,D3DSAMP_MAGFILTER},
        {Sampler,0,D3DSAMP_MINFILTER,D3DTEXF_POINT},{Sampler,0,D3DSAMP_MAGFILTER,D3DTEXF_POINT}});
    append(expected,{{Texture,0,5},{PixelRows,0,1},{Geometry,2},{IndexedDraw,12,3,(8u<<16)|5}});
    if(!terrain)append(expected,{{Sampler,0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR},{Sampler,0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR}});
    if(!drawFails)expected.push_back({IndexedDraw,21,4,(11u<<16)|12});
    assert(f.device.calls==expected&&triangles==(drawFails?0u:7u));
    assert(f.device.minFilter==D3DTEXF_LINEAR&&f.device.magFilter==D3DTEXF_LINEAR);
    if(drawFails)assert(std::strcmp(f.failure,terrain?"cube terrain draw":"cube static draw")==0);
    const auto bad=[](size_t& at){++at;return Batch{1,0,0,0,0,0,false};};
    assert(!pass.drawMeshRanges(1,Pass::Static,{f.cacheTex},{.5f},bad,bind,[](UINT){assert(false);}));
    assert(std::strcmp(f.failure,"cube material range")==0&&f.device.calls==expected);
}

static void liveRanges(bool indexFails){
    Fixture f;
    auto pass=f.pass();
    const decltype(pass)::LiveGeometry live={handle<IDirect3DVertexDeclaration9>(9),
        handle<IDirect3DVertexBuffer9>(10),handle<IDirect3DIndexBuffer9>(11),20,64,60};
    struct Batch{UINT start,count;};
    const Batch batches[]={{0,4},{12,3}};
    const auto batchAt=[&](size_t i)->const auto&{return batches[i];};
    unsigned triangles=0;
    if(indexFails)f.device.fault={Index,11};
    assert(pass.drawLiveTerrain(live,handle<IDirect3DVertexDeclaration9>(12),std::vector<size_t>{1,0},
        batchAt,[&]{f.device.record({Touch});},[&](UINT count){triangles+=count;})!=indexFails);
    std::vector<Call> expected={{PixelRows,0,1},{Texture,0},{Layout,9},{Stream,10,20},{Index,11}};
    if(!indexFails)append(expected,{{Touch},{IndexedDraw,72,3,64},{IndexedDraw,60,4,64},{Layout,12}});
    assert(f.device.calls==expected&&triangles==(indexFails?0u:7u));
}

static void opaqueFaceProof(){
    using namespace NorthlightPointShadow;
    struct Packet {
        struct Mesh {NorthlightSourceIdentity::Handle replayIdentity,sourceIdentity;} geometry;
        const Mesh& mesh()const{return geometry;}
        struct Usage {struct Range {unsigned first,count;} floats{0,8};} constantUsage;
        bool gpuCached=true,indexed=true,shadowSkinned=false;
        D3DPRIMITIVETYPE type=D3DPT_TRIANGLELIST;
        unsigned count=1,min=0,vertices=3,start=0,offset[4]={},stride[4]={36},alphaMode=0,projectionKind=2;
        int base=0;
        float cutoff=-1.f,constants[32]={};
        int *index=nullptr,*stream[4]={};
        IDirect3DVertexShader9* shader=nullptr;
    };
    const std::uint32_t vertex[]={0xfffe0300,0x02000001,0xe00f0000,0xa0e40006,0x0000ffff};
    const auto proof=RigidPoseRows::fromProgram(vertex,std::size(vertex));
    const auto rows=[&](const Packet&){return &proof;};
    int index=0,vertices=0;
    Packet first;
    first.geometry.sourceIdentity=NorthlightSourceIdentity::make({1,2,3});
    first.index=&index;first.stream[0]=&vertices;first.shader=handle<IDirect3DVertexShader9>(7);
    std::array<Packet,2> packets={first,first};
    packets[1].start=3;
    std::array<size_t,2> order={0,1};
    const auto at=[&](size_t i)->const Packet&{return packets[i];};
    float matrix[16]={},projection[16]={};
    OpaqueFaceCache cache;
    OpaqueFaceCache::Key key{7,11,13,48.f,handle<IDirect3DTexture9>(8),matrix,projection};
    const auto record=[&]{return cache.record(key,order,at,rows,{1,2,2});};
    CubeSlot slot,other;
    auto& token=slot.opaqueTokens[0];
    const auto match=[&]{return cache.match(token,key,order,at,rows);};
    assert(!match());
    token=record();
    assert(match()&&match()->draws==1&&match()->packets==2&&match()->triangles==2);
    key.staticFace=nullptr;
    assert(!match());
    key.staticFace=handle<IDirect3DTexture9>(8);
    packets[0].constants[8]=9;
    packets[0].constants[28]=9;
    assert(match());
    packets[0].constants[25]=-0.f;
    assert(!match());
    packets[0].constants[25]=0.f;
    projection[8]=1;
    assert(!match());
    projection[8]=0;
    std::swap(order[0],order[1]);
    assert(!match());
    std::swap(order[0],order[1]);
    packets[1].geometry.sourceIdentity=NorthlightSourceIdentity::make({1,2,4});
    assert(!match());
    packets[1].geometry.sourceIdentity=NorthlightSourceIdentity::make({1,2,3});
    assert(match());
    packets[1].alphaMode=1;
    assert(!match()&&!record());
    packets[1].alphaMode=0;
    packets[1].constantUsage.floats.count=6;
    assert(!match()&&!record());
    packets[1].constantUsage.floats.count=8;
    assert(match());
    const auto allocate=[](const Packet&)->const RigidPoseRows*{throw std::bad_alloc();};
    assert(!cache.record(key,order,at,allocate,{1,2,2})&&match());
    slot.swapCube(other);
    assert(!match()&&other.opaqueTokens[0]==0);
    token=record();
    slot.releaseCube();
    assert(!match());
    token=record();
    for(unsigned i=0;i<64;++i)assert(record());
    assert(!match());
    token=record();
    cache.clear();
    assert(!match()&&record()!=token);
    std::puts("POINT_OPAQUE_FACE_PASS exact geometry, pose and order, fixed projection, transparent fallback, cube transfer, release and eviction");
}

int main(){
    reuseEmptyAuxiliaryTargets();
    pairedAndNextFace();
    copiedAndUnion(false,false);
    copiedAndUnion(false,true);
    copiedAndUnion(true,false);
    copiedAndUnion(true,true);
    failuresAndReset();
    refusedStaticAllocation();
    for(bool failure:{false,true}){
        meshRanges(false,failure);
        meshRanges(true,failure);
        liveRanges(failure);
    }
    opaqueFaceProof();
    std::puts("POINT_FACE_PASS ordered static/terrain/live binds, ring fence, draw/union, allocation and state failures, fresh bindings after reset");
}
