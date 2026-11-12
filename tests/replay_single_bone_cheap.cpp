#undef NDEBUG
#include <chrono>
#include <cstddef>
#include <cstdint>
static std::size_t groupLookups=0;
#define NORTHLIGHT_TEST_ENVELOPE_GROUP_LOOKUP() (++groupLookups)
struct CheapClock {using duration=std::chrono::nanoseconds;using time_point=std::chrono::time_point<CheapClock,duration>;static constexpr bool is_steady=true;static inline std::int64_t ticks=0,step=1;static time_point now(){ticks+=step;return time_point(duration(ticks));}};
#define NORTHLIGHT_REPLAY_BOUNDS_CLOCK CheapClock
#include "replay_bounds_job.h"
#include "actor_client_programs.h"
#include <cstdio>
#include <cassert>
using namespace NorthlightReplayBounds;
using namespace NorthlightActorDeformation;
using Mesh=NorthlightDrawSnapshot::Mesh;
static D3DVERTEXELEMENT9 decl[]={{0,4,2,0,0,0},{0,16,5,0,2,0},{0xff,0,17,0,0,0}};
static std::shared_ptr<const Mesh> mesh(unsigned groups,float x){
    auto m=std::make_shared<Mesh>();m->vertexCount=groups*3;m->primitiveCount=groups;m->streams[0].stride=24;m->streams[0].bytes.resize(size_t(m->vertexCount)*24);
    for(unsigned i=0;i<m->vertexCount;++i){const float p[]={x+float(i%3==1),float(i%3==2),float(i%5)*.03f};auto* at=m->streams[0].bytes.data()+i*24;std::memcpy(at+4,p,12);at[16]=std::uint8_t(i/3);}
    return m;
}
static bool equal(const Bounds& a,const Bounds& b){return a.valid==b.valid&&(!a.valid||(!std::memcmp(a.low,b.low,12)&&!std::memcmp(a.high,b.high,12)));}
static void groupSourceChecks();
static void warmGroupChecks();
int main(){
    Program p;assert(compile(ClientShaders::OneBoneVs3,std::size(ClientShaders::OneBoneVs3),p)&&p.kernel.singleSkin()&&p.operations.size()==6);
    float c[1024]={},inv[16]={};for(unsigned i=0;i<4;++i)inv[i*5]=1;for(unsigned b=0;b<75;++b){c[4*(31+3*b)]=1;c[4*(32+3*b)+1]=1;c[4*(33+3*b)+2]=1;}
    const auto prepared=EnvelopeCache::prepareProgram(p,decl,3);assert(prepared);Cache cache;
    std::vector<NorthlightReplayBoundsJob::Item> items(1595);auto small=mesh(1,1000);
    for(unsigned i=0;i<items.size();++i){auto& item=items[i];item.owner=i<1536?small:mesh(i%2?16:21,i==1536?0.f:2000.f+float(i));item.prepared=prepared;item.constants=c;
        Bounds b;cache.beginFrame(false,items.size());Budget seed;WorkInfo info;assert(cache.calculatePreparedPhase(*prepared,*item.owner,item.owner,c,inv,seed,b,EnvelopeCache::Stage::All,&info)==Status::Valid);if(i==1536)assert(info.programHash==0x807061adfc37bc4dull);}
    for(unsigned step:{250u,1000u})for(unsigned pose=0;pose<4;++pose){const float angle=pose*.7f;inv[0]=inv[5]=std::cos(angle);inv[1]=std::sin(angle);inv[4]=-inv[1];inv[12]=10000+pose;
        for(unsigned b=0;b<75;++b)c[4*(31+3*b)+3]=float(pose)*.2f;
        for(auto& item:items){item.bounds={};item.work={};}CheapClock::step=step;cache.beginFrame(false,items.size());
        const auto totals=NorthlightReplayBoundsJob::schedule(cache,items,inv);const auto stats=cache.envelopeStats();
#ifdef NORTHLIGHT_CHEAP_REFERENCE
        assert(totals.valid<items.size()&&stats.heavyAttempts>0);
#else
        assert(totals.valid==items.size()&&!stats.heavyAttempts&&stats.cheapValid==items.size()&&!totals.boundVertices);
#endif
        assert(totals.boundOperations<=262144);const auto valid=totals.valid;CheapClock::step=1;
        std::uint64_t digest=14695981039346656037ull;
        for(auto& item:items){cache.beginFrame(false,items.size());Budget work;work.maxVertices=0;Bounds expected;WorkInfo info;
            assert(cache.evaluateHeavy(*prepared,*item.owner,item.owner,c,inv,work,expected,info)==Status::Valid);
            if(item.bounds.valid)assert(equal(item.bounds,expected));
            std::vector<Position> points;assert(worldPositions(p,*item.owner,decl,3,c,inv,points));for(const auto& point:points){const float at[]={point.x,point.y,point.z};for(unsigned axis=0;axis<3;++axis)assert(at[axis]>=expected.low[axis]&&at[axis]<=expected.high[axis]);}
            for(const auto* data:{expected.low,expected.high})for(unsigned k=0;k<12;++k)digest=(digest^reinterpret_cast<const unsigned char*>(data)[k])*1099511628211ull;
        }
        std::printf("SINGLE_BONE_ORACLE pose=%u hash=%016llx\n",pose,(unsigned long long)digest);
        std::printf("SINGLE_BONE_WAVE packets=1595 small=1536 groups16or21=59 clockStepNs=%u pose=%u valid=%u unboundedFacePackets=%u heavyAttempts=%zu operations=%zu\n",step,pose,valid,unsigned(items.size()-valid)*14*6,stats.heavyAttempts,totals.boundOperations);
    }
    // The operation ceiling and non-audited group guard still apply.
    for(unsigned groups:{16u,21u,22u})for(bool audited:{false,true}){
        auto program=p;if(!audited)program.kernel.valid=false;auto source=mesh(groups,0);EnvelopeCache negative;Bounds b;
        auto meta=EnvelopeCache::prepareProgram(program,decl,3);assert(meta);negative.beginFrame(false);Budget seed;
        assert(negative.calculatePrepared(*meta,*source,source,c,inv,seed,b)==Status::Valid);negative.beginFrame(false);Budget cheap;cheap.maxVertices=0;WorkInfo info;
        const auto status=negative.calculatePrepared(*meta,*source,source,c,inv,cheap,b,EnvelopeCache::Stage::Cheap,&info);
#ifdef NORTHLIGHT_CHEAP_REFERENCE
        assert(status==Status::Budget&&!b.valid&&info.kind==WorkKind::Heavy);
#else
        if(audited&&groups<=21)assert(status==Status::Valid&&b.valid&&info.kind==WorkKind::Cheap);
        else assert(status==Status::Budget&&!b.valid&&info.kind==WorkKind::Heavy);
#endif
    }
    std::puts("SINGLE_BONE_CHEAP_PASS currentPose=exact genericGroupGate=preserved operation128Gate=preserved noBuild=1");
    groupSourceChecks();
    warmGroupChecks();
}

static unsigned bone(unsigned vertex,unsigned pattern){
    if(pattern==0)return 0;
    if(pattern==1)return (vertex/64)%21;
    if(pattern==2)return (vertex/3)%21;
    if(pattern==3)return vertex%21;
    return ((vertex*1664525u+1013904223u)>>16)%21;
}
static std::shared_ptr<const Mesh> source(unsigned n,unsigned pattern){
    auto m=std::make_shared<Mesh>();m->vertexCount=n;m->primitiveCount=n/3;m->streams[0].stride=24;m->streams[0].bytes.resize(size_t(n)*24);
    for(unsigned i=0;i<n;++i){auto* at=m->streams[0].bytes.data()+i*24;const float p[]={float(int(i%7)-3)*.1f,float(int(i%11)-5)*.13f,float(i%13)*.07f};std::memcpy(at+4,p,12);
        at[16]=std::uint8_t(bone(i,pattern));at[17]=std::uint8_t(i*19);at[18]=std::uint8_t(i*7);at[19]=std::uint8_t(i*13);}
    return m;
}
static void palette(float* c){std::fill(c,c+1024,0.f);for(unsigned b=0;b<75;++b){c[4*(31+3*b)]=1;c[4*(31+3*b)+3]=float(b)*.12f;c[4*(32+3*b)+1]=1;c[4*(33+3*b)+2]=1;}}
static void extraChecks(const Program& p,const float* c,const float* inverse){
    for(unsigned layout=0;layout<4;++layout)for(unsigned bad=0;bad<7;++bad){
        D3DVERTEXELEMENT9 elements[]={{0,4,D3DDECLTYPE_FLOAT3,0,0,0},{0,20,D3DDECLTYPE_UBYTE4,0,2,0},{0xff,0,17,0,0,0}};
        if(layout==1)elements[0].Type=D3DDECLTYPE_FLOAT4;
        if(layout==2)elements[1].Type=D3DDECLTYPE_FLOAT4;
        if(layout==3){elements[1].Stream=2;elements[1].Offset=3;}
        auto m=std::make_shared<Mesh>();m->vertexCount=63;m->primitiveCount=21;m->streams[0].stride=layout==3?16:40;m->streams[0].bytes.resize(63*m->streams[0].stride);
        if(layout==3){m->streams[2].stride=8;m->streams[2].bytes.resize(63*8);}
        for(unsigned i=0;i<63;++i){const float values[]={float(i%7)*.17f,float(i%5)*-.23f,float(i%9)*.11f,i%2?.5f:1.25f};auto* position=m->streams[0].bytes.data()+i*m->streams[0].stride+4;std::memcpy(position,values,layout==1?16:12);
            auto* indices=m->streams[elements[1].Stream].bytes.data()+i*m->streams[elements[1].Stream].stride+elements[1].Offset;
            if(layout==2){float v[]={i%2?-0.f:0.f,float(i),float(i%3),1};std::memcpy(indices,v,16);}
            else {indices[0]=std::uint8_t(i%21);indices[1]=std::uint8_t(i*7);indices[2]=std::uint8_t(i*13);indices[3]=std::uint8_t(i*19);}
        }
        if(bad&&bad<6){const uint32_t patterns[]={0,0x7fc12345u,0x7f800000u,1u,0x80000001u,0x80000000u};auto* at=m->streams[0].bytes.data()+17*m->streams[0].stride+4;std::memcpy(at,&patterns[bad],4);}
        if(bad==6){auto* at=m->streams[elements[1].Stream].bytes.data()+17*m->streams[elements[1].Stream].stride+elements[1].Offset;if(layout==2){const uint32_t nan=0x7fc12345u;std::memcpy(at+4,&nan,4);}else *at=255;}
        const auto prepared=EnvelopeCache::prepareProgram(p,elements,3);assert(prepared);EnvelopeCache cache;cache.beginFrame(false);Budget work;work.maxVertices=131072;Bounds bounds;std::shared_ptr<const Mesh> owner=m;
        const auto build=cache.calculatePrepared(*prepared,*m,owner,c,inverse,work,bounds,EnvelopeCache::Stage::Build);cache.beginFrame(false);work.reset();const auto status=cache.calculatePrepared(*prepared,*m,owner,c,inverse,work,bounds);
        uint32_t bits[6]={};if(bounds.valid){std::memcpy(bits,bounds.low,12);std::memcpy(bits+3,bounds.high,12);}
        std::printf("GROUP_EXTRA_ORACLE layout=%u bad=%u build=%u status=%u valid=%u bits=%08x,%08x,%08x,%08x,%08x,%08x\n",layout,bad,unsigned(build),unsigned(status),unsigned(bounds.valid),bits[0],bits[1],bits[2],bits[3],bits[4],bits[5]);
    }
}

static void groupSourceChecks(){
    Program p;assert(compile(ClientShaders::OneBoneVs3,std::size(ClientShaders::OneBoneVs3),p)&&p.kernel.singleSkin());
    const auto prepared=EnvelopeCache::prepareProgram(p,decl,3);assert(prepared);
    float c[1024],inverse[16]={};palette(c);for(unsigned i=0;i<4;++i)inverse[i*5]=1;
    CheapClock::ticks=0;CheapClock::step=1;
    for(unsigned n:{3u,63u,32766u})for(unsigned pattern=0;pattern<5;++pattern){
        auto m=source(n,pattern);Bounds full;
        for(unsigned chunk:{11u,131072u}){
            EnvelopeCache cache;cache.beginFrame(false);Budget work;Bounds bounds;unsigned at=0;size_t expected=0;groupLookups=0;
            while(at<n){
                const unsigned end=std::min(n,at+chunk);
                bool seen[256]={};for(unsigned i=at;i<end;++i){const auto key=bone(i,pattern);expected+=!seen[key];seen[key]=true;}
                work.reset();work.maxVertices=chunk;
                const auto status=cache.calculatePrepared(*prepared,*m,m,c,inverse,work,bounds,EnvelopeCache::Stage::Build);
                assert(status==Status::Budget&&work.vertices==end-at);at=end;cache.beginFrame(false);
            }
            (void)expected;
#ifndef NORTHLIGHT_CHEAP_REFERENCE
            assert(groupLookups==expected);
#endif
            work.reset();assert(cache.calculatePrepared(*prepared,*m,m,c,inverse,work,bounds)==Status::Valid&&bounds.valid);
            if(full.valid)assert(equal(full,bounds));else full=bounds;
            std::vector<Position> vertices;assert(worldPositions(p,*m,decl,3,c,inverse,vertices));
            for(const auto& v:vertices){const float at[]={v.x,v.y,v.z};for(unsigned axis=0;axis<3;++axis)assert(at[axis]>=bounds.low[axis]&&at[axis]<=bounds.high[axis]);}
            uint32_t bits[6];std::memcpy(bits,bounds.low,12);std::memcpy(bits+3,bounds.high,12);
            std::printf("GROUP_ORACLE vertices=%u pattern=%u chunk=%u bits=%08x,%08x,%08x,%08x,%08x,%08x\n",n,pattern,chunk,bits[0],bits[1],bits[2],bits[3],bits[4],bits[5]);
        }
    }
    extraChecks(p,c,inverse);
    std::puts("GROUP_SOURCE_PASS exact_bounds=30 chunk_reset=1 group_rehash=1 locality_patterns=5 layouts=4 malformed_variants=7");
}
static void warmGroupChecks(){
    for(unsigned shader=0;shader<4;++shader)for(bool reverse:{false,true})for(unsigned bad=0;bad<8;++bad){
        Program p;
        switch(shader){
        case 0:assert(compile(ClientShaders::OneBoneVs3,std::size(ClientShaders::OneBoneVs3),p));break;
        case 1:assert(compile(ClientShaders::OneBoneVs3WLast,std::size(ClientShaders::OneBoneVs3WLast),p));break;
        case 2:assert(compile(ClientShaders::OneBoneVs3WMid,std::size(ClientShaders::OneBoneVs3WMid),p));break;
        default:assert(compile(ClientShaders::OneBoneVs2,std::size(ClientShaders::OneBoneVs2),p));break;
        }
        assert(p.kernel.singleSkin());if(reverse)std::reverse(p.inputs.begin(),p.inputs.end());
        for(auto& def:p.definitions)if(def.reg==0){if(bad==2)def.value[0]=2.5f;if(bad==3)def.value[1]=1.00001f;}
        auto source=std::const_pointer_cast<Mesh>(mesh(21,0));
        D3DVERTEXELEMENT9 elements[]={{0,4,D3DDECLTYPE_FLOAT3,0,0,0},{0,16,D3DDECLTYPE_UBYTE4,0,2,0},{0xff,0,17,0,0,0}};
        if(bad==1)for(unsigned i=0;i<source->vertexCount;++i){const float tiny=std::numeric_limits<float>::denorm_min();std::memcpy(source->streams[0].bytes.data()+i*24+4,&tiny,4);}
        if(bad==6){
            auto old=source->streams[0].bytes;source->streams[0].stride=40;source->streams[0].bytes.resize(source->vertexCount*40);elements[1].Type=D3DDECLTYPE_FLOAT4;
            for(unsigned i=0;i<source->vertexCount;++i){auto* at=source->streams[0].bytes.data()+i*40;std::memcpy(at+4,old.data()+i*24+4,12);const float indices[]={float(i/3)+.5f,float(i),-0.f,1};std::memcpy(at+16,indices,16);}
        }
        if(bad==7)source->streams[0].bytes[16]=255;
        float c[1024],inverse[16]={};for(unsigned i=0;i<4;++i)inverse[i*5]=1;
        const auto fill=[&](unsigned pose){palette(c);for(unsigned b=0;b<75;++b)c[4*(31+3*b)+3]=float((pose+b)%19)*.013f;
            if(bad==4)c[4*31]=std::numeric_limits<float>::denorm_min();if(bad==5)c[4*31]=std::numeric_limits<float>::quiet_NaN();};
        fill(0);const auto prepared=EnvelopeCache::prepareProgram(p,elements,3);assert(prepared);
        std::shared_ptr<const Mesh> owner=source;EnvelopeCache cache;CheapClock::ticks=0;CheapClock::step=1;cache.beginFrame(false);Budget seed;Bounds bounds;
        const auto seeded=cache.calculatePrepared(*prepared,*source,owner,c,inverse,seed,bounds);assert(seeded==Status::Valid||seeded==Status::Unsupported);
        std::uint64_t digest=14695981039346656037ull;
        for(unsigned pose=0;pose<4;++pose){
            fill(pose+1);cache.beginFrame(false);Budget work;work.maxVertices=0;bounds={};
            const auto status=cache.calculatePrepared(*prepared,*source,owner,c,inverse,work,bounds);
            assert(!work.vertices&&(status==Status::Valid||status==Status::Unsupported));if(!bad)assert(status==Status::Valid&&bounds.valid);
            digest=(digest^unsigned(status))*1099511628211ull;
            if(bounds.valid){
                for(const float* values:{bounds.low,bounds.high})for(unsigned k=0;k<12;++k)digest=(digest^reinterpret_cast<const unsigned char*>(values)[k])*1099511628211ull;
                std::vector<Position> vertices;assert(worldPositions(p,*source,elements,3,c,inverse,vertices));
                for(const auto& v:vertices){const float at[]={v.x,v.y,v.z};for(unsigned axis=0;axis<3;++axis)assert(at[axis]>=bounds.low[axis]&&at[axis]<=bounds.high[axis]);}
            }
        }
        std::printf("WARM_GROUP_ORACLE shader=%u reverse=%u bad=%u digest=%016llx\n",shader,unsigned(reverse),bad,(unsigned long long)digest);
    }
    std::puts("WARM_GROUP_PASS kernels=4 bindingOrders=2 fallbackCases=7 currentPose=exact noBuild=1");
}
