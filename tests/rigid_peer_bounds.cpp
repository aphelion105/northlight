#define NOMINMAX
#include <d3d9.h>
#include <cassert>
#include <cstdio>
#include <chrono>
struct PeerClock {
    using duration=std::chrono::nanoseconds;
    using time_point=std::chrono::time_point<PeerClock,duration>;
    static time_point now(){return time_point(duration(1000000));}
};
#define NORTHLIGHT_REPLAY_BOUNDS_CLOCK PeerClock
#include "replay_bounds.h"
using namespace NorthlightReplayBounds;
using Mesh=NorthlightDrawSnapshot::Mesh;
using Program=NorthlightActorDeformation::Program;
static Program program(bool translated=false,unsigned tag=0){
    Program p;p.major=3;p.positionRegister=0;p.inputs.push_back({0,0,0});
    NorthlightActorDeformation::Operation op;op.code=translated?2:1;op.destination=0x800f0000;
    op.source[0].token=0x90e40000;op.source[1].token=0xa0e40000;p.operations.push_back(op);
    if(tag)p.definitions.push_back({250,{float(tag),0,0,0}});
    return p;
}
static std::shared_ptr<const Mesh> mesh(unsigned stride=24){
    auto m=std::make_shared<Mesh>();m->vertexCount=96;m->primitiveCount=32;
    m->streams[0].stride=stride;m->streams[0].bytes.resize(96*stride);
    for(unsigned i=0;i<96;++i)for(unsigned j=0;j+4<=stride;j+=4){
        const float v=float(int(i%7)-3)+float(j)*.25f;std::memcpy(m->streams[0].bytes.data()+i*stride+j,&v,4);
    }
    return m;
}
static D3DVERTEXELEMENT9 layout[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},D3DDECL_END()};
static float constants[1024]={},inverse[16]={};
static Status call(EnvelopeCache& cache,const Program& p,const std::shared_ptr<const Mesh>& m,Budget& budget,Bounds& b,EnvelopeCache::Stage stage=EnvelopeCache::Stage::All,const D3DVERTEXELEMENT9* d=layout){
    return cache.calculate(p,*m,m,d,2,constants,inverse,budget,b,stage);
}
static void warm(EnvelopeCache& cache,const Program& p,const std::shared_ptr<const Mesh>& m,const D3DVERTEXELEMENT9* d=layout){
    cache.beginFrame(false);Budget budget;Bounds b;assert(call(cache,p,m,budget,b,EnvelopeCache::Stage::All,d)==Status::Valid&&b.valid);
}
static void enclosed(const Program& p,const std::shared_ptr<const Mesh>& m,const Bounds& b){
    std::vector<NorthlightActorDeformation::Position> positions;assert(worldPositions(p,*m,layout,2,constants,inverse,positions));
    for(const auto& v:positions){const float xyz[]={v.x,v.y,v.z};for(unsigned j=0;j<3;++j)assert(b.valid&&b.low[j]<=xyz[j]&&b.high[j]>=xyz[j]);}
}
static void refused(const std::shared_ptr<const Mesh>& first,const std::shared_ptr<const Mesh>& other,const D3DVERTEXELEMENT9* d=layout){
    EnvelopeCache cache;warm(cache,program(),first);cache.beginFrame(false);Budget budget;budget.maxVertices=1;Bounds b;
    assert(call(cache,program(true),other,budget,b,EnvelopeCache::Stage::Build,d)==Status::Budget&&!b.valid);
    assert(budget.vertices==1&&!cache.stats().completed);
}
int main(){
    for(unsigned j=0;j<4;++j)inverse[j*5]=1;
    auto owner=mesh();const auto bytes=owner->streams[0].bytes;EnvelopeCache cache;warm(cache,program(),owner);
    constants[0]=37;constants[1]=-13;constants[2]=11;inverse[12]=1000;inverse[13]=-400;
    cache.beginFrame(false);Budget budget;budget.maxVertices=1;Bounds b;
    assert(call(cache,program(true),owner,budget,b,EnvelopeCache::Stage::Build)==Status::Budget&&!b.valid);
    // This readiness check fails on the original implementation: one vertex
    // cannot finish the second program's redundant 96-vertex source scan.
    cache.beginFrame(false);budget=Budget{};budget.maxVertices=0;
    if(call(cache,program(true),owner,budget,b,EnvelopeCache::Stage::Cheap)!=Status::Valid){std::puts("PEER_READY_MISSING");return 13;}
    assert(!budget.vertices);enclosed(program(true),owner,b);
    for(unsigned pose=0;pose<40;++pose){constants[0]+=3;constants[1]-=.25f;inverse[12]-=7;inverse[5]=float(pose+1)*.1f;
        cache.beginFrame(false);budget=Budget{};assert(call(cache,program(true),owner,budget,b)==Status::Valid);enclosed(program(true),owner,b);}
    assert(owner->streams[0].bytes==bytes&&owner.use_count()==1);

    // Identical immutable source contracts survive packet reconstruction.
    auto identified=std::make_shared<Mesh>(*owner);identified->replayIdentity=NorthlightSourceIdentity::make({1,2});
    auto clone=std::make_shared<const Mesh>(*identified);EnvelopeCache identities;warm(identities,program(),identified);
    cache.clear();identified.reset();identities.beginFrame(false);budget=Budget{};budget.maxVertices=1;
    assert(call(identities,program(true),clone,budget,b)==Status::Valid&&!budget.vertices);enclosed(program(true),clone,b);

    // The hash is only a bucket selector. Different complete contracts refuse,
    // including a deliberately constructed two-word FNV collision.
    auto different=std::make_shared<Mesh>(*clone);different->replayIdentity=NorthlightSourceIdentity::make({9,2});refused(clone,different);
    constexpr uint64_t offset=1469598103934665603ull,prime=1099511628211ull;
    const uint64_t collision=(((offset^1)*prime)^2)^((offset^3)*prime);
    different->replayIdentity=NorthlightSourceIdentity::make({3,collision});
    assert(different->replayIdentity->hash==clone->replayIdentity->hash&&!NorthlightSourceIdentity::same(different->replayIdentity,clone->replayIdentity));refused(clone,different);
    // A reconstructed packet without a verified identity has another owner.
    refused(owner,std::make_shared<const Mesh>(*owner));
    // The same address with another control block cannot prove owner lifetime.
    std::shared_ptr<const Mesh> alias(owner.get(),[](const Mesh*){});refused(owner,alias);
    auto changed=std::make_shared<Mesh>(*owner);changed->vertexCount=93;changed->primitiveCount=31;refused(owner,changed);
    changed=std::make_shared<Mesh>(*clone);changed->streams[0].stride=20;refused(clone,changed);
    D3DVERTEXELEMENT9 shifted[]={layout[0],layout[1]};shifted[0].Offset=4;refused(owner,owner,shifted);

    // Zero admission budget remains fail-open; a later frame can still finish.
    EnvelopeCache admission;warm(admission,program(),owner);admission.beginFrame(false);budget=Budget{};budget.maxVertices=0;
    assert(call(admission,program(true),owner,budget,b,EnvelopeCache::Stage::Build)==Status::Budget&&!b.valid);
    assert(admission.entries()==1);admission.beginFrame(false);budget=Budget{};budget.maxVertices=1;
    assert(call(admission,program(true),owner,budget,b)==Status::Valid&&!budget.vertices);
    admission.beginFrame(false);budget=Budget{};budget.maxOperations=0;
    assert(call(admission,program(true,1),owner,budget,b,EnvelopeCache::Stage::Build)==Status::Budget&&!b.valid&&!budget.operations);

    // Many incompatible peers cannot repeatedly consume a small source budget.
    auto wide=mesh(256);EnvelopeCache crowded;
    for(unsigned offset=0;offset<128;offset+=4){D3DVERTEXELEMENT9 d[]={layout[0],layout[1]};d[0].Offset=WORD(offset);warm(crowded,program(),wide,d);}
    D3DVERTEXELEMENT9 unique[]={layout[0],layout[1]};unique[0].Offset=128;
    size_t vertices=0;for(unsigned frame=0;frame<96;++frame){crowded.beginFrame(false);budget=Budget{};budget.maxVertices=1;budget.maxOperations=16;
        assert(call(crowded,program(),wide,budget,b,EnvelopeCache::Stage::Build,unique)==Status::Budget&&!b.valid);
        assert(budget.vertices==1&&budget.operations<=8);vertices+=budget.vertices;}
    assert(vertices==96);crowded.beginFrame(false);budget=Budget{};
    assert(call(crowded,program(),wide,budget,b,EnvelopeCache::Stage::Cheap,unique)==Status::Valid&&b.valid);
    assert(crowded.entries()==33&&crowded.reservedBytes()<=8u*1024u*1024u);
    std::puts("RIGID_PEER_PASS current_pose collision identity owner stride offset admission bounded_peer_search no_geometry_copy");
}
