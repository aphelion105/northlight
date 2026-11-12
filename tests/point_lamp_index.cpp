#include "wmo_lamp_shadow.h"
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <limits>

static bool failAllocation=false;
void* operator new(size_t bytes){if(failAllocation){failAllocation=false;throw std::bad_alloc();}if(void* p=std::malloc(bytes?bytes:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}
struct Batch {
    bool terrain=false;uint64_t lampSourceId=0;unsigned count=8;
    NorthlightGI::Vec3 boundsLow={-.5f,-.5f,-.5f},boundsHigh={.5f,.5f,.5f};
    std::shared_ptr<std::vector<int>> lampTriangles;
};
struct Counted {
    const std::vector<Batch>& batches;mutable size_t reads=0;
    size_t size()const{return batches.size();}const Batch* data()const{return batches.data();}
    const Batch& operator[](size_t i)const{++reads;return batches[i];}
};
using NorthlightWmoLampShadow::parts;
using NorthlightWmoLampShadow::SourceIndex;
using NorthlightLocalLights::Light;
static volatile size_t checksum=0;
int main(){
    Light light{};light.sourceId=1;light.kind=2;light.flags=NorthlightLocalLights::CastShadow;
    light.attenuationStart=1;light.attenuationEnd=5;light.diffuse[0]=1;
    Batch shell;shell.lampSourceId=1;shell.lampTriangles=std::make_shared<std::vector<int>>(8);
    std::vector<Batch> batches(16384);batches[9000]=shell;
    auto second=shell;second.lampTriangles=std::make_shared<std::vector<int>>(8);batches[13000]=second;
    for(unsigned i=2;i<=32;++i){auto b=shell;b.lampSourceId=i;batches[100+i]=b;}
    batches[20]=shell;batches[20].terrain=true;
    SourceIndex index;Counted counted{batches};assert(index.prepare(counted,1));
    assert(counted.reads>=batches.size());counted.reads=0;assert(index.prepare(counted,1)&&counted.reads==0);
    const auto check=[&](const Light& l){const auto range=index.find(l.sourceId);assert(parts(l,batches)==parts(l,batches,&range));};
    assert(parts(light,batches)==std::vector<size_t>({9000,13000}));check(light);
    auto missing=light;missing.sourceId=900000;check(missing);
    auto range=index.find(missing.sourceId);assert(parts(missing,counted,&range).empty()&&counted.reads==0);
    for(unsigned kind:{1u,2u,3u})for(unsigned flags:{0u,unsigned(NorthlightLocalLights::CastShadow),unsigned(NorthlightLocalLights::CastShadow|NorthlightLocalLights::GlassEmitter)}){
        auto l=light;l.kind=kind;l.flags=flags;check(l);l.position[0]=100;check(l);l=light;l.attenuationEnd=0;check(l);
    }
    // An index caches candidates, never the geometry/ambiguity proof.
    batches[13000].lampTriangles=shell.lampTriangles;check(light);assert(parts(light,batches).empty());
    batches[13000]=second;batches[13000].count=4097;check(light);assert(parts(light,batches).empty());
    batches[13000]=second;batches[13000].boundsLow.x=std::numeric_limits<float>::quiet_NaN();check(light);assert(parts(light,batches).empty());
    batches[13000]=second;batches[13000].lampTriangles.reset();check(light);assert(parts(light,batches).empty());
    batches[13000]=second;
    auto glass=light;glass.kind=3;glass.flags|=NorthlightLocalLights::GlassEmitter;check(glass);assert(parts(glass,batches).empty());
    batches[13000].lampSourceId=99;assert(index.prepare(batches,2));check(glass);assert(parts(glass,batches)==std::vector<size_t>{9000});
    batches[9000].terrain=true;assert(index.prepare(batches,3));check(light);assert(parts(light,batches).empty());
    batches.push_back(shell);assert(index.prepare(batches,3));check(light);assert(parts(light,batches)==std::vector<size_t>{batches.size()-1});
    auto replacement=batches;replacement.back().lampSourceId=88;assert(index.prepare(replacement,3));
    range=index.find(light.sourceId);assert(parts(light,replacement,&range)==parts(light,replacement));
    index.clear();failAllocation=true;const bool indexed=index.prepare(batches,4);assert(!indexed&&!failAllocation);
    range=index.find(light.sourceId);assert(parts(light,batches,indexed?&range:nullptr)==parts(light,batches));
    assert(range.first==range.second);assert(index.prepare(batches,4));check(light);
    // Warm lookup work is independent of untagged mesh size, including absent sources.
    size_t linearReads=0,indexReads=0;
    for(unsigned id=1;id<=64;++id){auto l=light;l.sourceId=id;counted.reads=0;checksum+=parts(l,counted).size();linearReads+=counted.reads;
        counted.reads=0;range=index.find(id);checksum+=parts(l,counted,&range).size();indexReads+=counted.reads;}
    assert(linearReads>=64*batches.size()&&indexReads<128);
    std::printf("POINT_LAMP_INDEX_WORK linearReads=%zu indexedReads=%zu batches=%zu lights=64\n",linearReads,indexReads,batches.size());
    const auto sample=[&](bool current){const auto start=std::chrono::steady_clock::now();
        for(unsigned repeat=0;repeat<40;++repeat)for(unsigned id=1;id<=64;++id){auto l=light;l.sourceId=id;if(current)range=index.find(id);checksum+=parts(l,batches,current?&range:nullptr).size();}
        return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/40;};
    sample(false);sample(true);
    for(unsigned round=0;round<6;++round){const bool first=round%2;const double a=sample(first),b=sample(!first);
        const auto start=std::chrono::steady_clock::now();
        for(unsigned repeat=0;repeat<40;++repeat){index.clear();assert(index.prepare(batches,4));}
        const double build=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/40;
        std::printf("POINT_LAMP_INDEX_TIMING round=%u linearMs=%.6f indexedMs=%.6f buildMs=%.6f\n",round,first?b:a,first?a:b,build);}
    std::puts("POINT_LAMP_INDEX_PASS ownership, ambiguity, invalidation, allocation fallback");
}
