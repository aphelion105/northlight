#include "world_probe_cache.h"
#include "probe_cache_reference.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>

static size_t allocations=0;
static bool counting=false;
void* operator new(size_t bytes){if(counting)++allocations;if(auto* p=std::malloc(bytes?bytes:1))return p;throw std::bad_alloc();}
void* operator new[](size_t bytes){return ::operator new(bytes);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
using namespace NorthlightGI;

static Probe solved(int n){Probe p;p.position={float(n),2,3};p.valid=n%3!=0;p.samples=unsigned(n);p.sh[2]={4,float(n),6};p.moments[1]={float(n),8};return p;}
static void same(const Probe& a,const Probe& b){
    assert(a.position.x==b.position.x&&a.position.y==b.position.y&&a.position.z==b.position.z);
    assert(a.valid==b.valid&&a.samples==b.samples&&a.backFaceSamples==b.backFaceSamples&&a.maxDistance==b.maxDistance);
    for(unsigned i=0;i<4;++i)assert(a.sh[i].x==b.sh[i].x&&a.sh[i].y==b.sh[i].y&&a.sh[i].z==b.sh[i].z);
    for(unsigned i=0;i<6;++i)assert(a.moments[i].mean==b.moments[i].mean&&a.moments[i].meanSquare==b.moments[i].meanSquare);
}
static void atlas(const ProbeCache& a,const ReferenceProbeCache& b){
    auto x=a.atlas(),y=b.atlas();assert(x.size()==y.size());
    for(size_t i=0;i<x.size();++i){assert(x[i].occupied==y[i].occupied);if(x[i].occupied){assert(x[i].key==y[i].key);same(x[i].probe,y[i].probe);}}
}
template<class Cache> static size_t churn(Cache& cache,unsigned count){
    const auto before=allocations;counting=true;
    for(unsigned i=0;i<count;++i){ProbeGridKey key{int(i)+100000,0,0};Probe p;assert(!cache.get(key,p));cache.putMissing(key,solved(int(i)));}
    counting=false;return allocations-before;
}
static volatile unsigned sink=0;
template<class Cache> static double timed(){
    Cache cache(4096);for(int i=0;i<4096;++i)cache.put({i,0,0},solved(i));
    const auto start=std::chrono::steady_clock::now();
    for(int i=4096;i<135168;++i){Probe p;ProbeGridKey key{i,0,0};assert(!cache.get(key,p));cache.putMissing(key,solved(i));assert(cache.get(key,p));sink+=p.samples;}
    return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
}
int main(){
    for(unsigned n:{14u,20u,40u}){
        assert(configureProbeLayout(probeLayoutFor(n)));
        for(size_t capacity:{size_t(1),size_t(3),size_t(64),probeLayout().atlasSize()}){
            ProbeCache current(capacity);ReferenceProbeCache reference(capacity);uint32_t random=17;
            for(int i=0;i<12000;++i){
                random=random*1664525u+1013904223u;ProbeGridKey key{int(random%137)-68,int((random>>8)%7)-3,int((random>>16)%5)-2};
                if(i%23==0){key={i,0,0};Probe a,b;const bool hit=current.get(key,a);assert(hit==reference.get(key,b));
                    if(!hit){current.putMissing(key,solved(i));reference.putMissing(key,solved(i));}else same(a,b);}
                else if(i%3==0){current.put(key,solved(i));reference.put(key,solved(i));}
                else if(i%3==1){Probe a=solved(-1),b=a;assert(current.get(key,a)==reference.get(key,b));same(a,b);}
                else{bool a=false,b=false;assert(current.touchValidity(key,a)==reference.touchValidity(key,b));assert(a==b);}
                assert(current.size()==reference.size()&&current.contains(key)==reference.contains(key));
                if(i%997==0)atlas(current,reference);
                if(i==6000){current.clear();reference.clear();}
            }
            atlas(current,reference);
        }
    }
    assert(configureProbeLayout(probeLayoutFor(14)));
    ProbeCache current(64);ReferenceProbeCache reference(64);
    for(int i=0;i<64;++i){current.put({i,0,0},solved(i));reference.put({i,0,0},solved(i));}
    const auto before=churn(reference,8192),after=churn(current,8192);
    std::printf("PROBE_CACHE_ALLOCATIONS replacements=8192 before=%zu after=%zu\n",before,after);std::fflush(stdout);
    assert(before>=16384&&after==0);atlas(current,reference);
    std::vector<double> oldTimes,newTimes;
    for(unsigned block=0;block<12;++block){double a,b;if(block%4==1||block%4==2){b=timed<ProbeCache>();a=timed<ReferenceProbeCache>();}else{a=timed<ReferenceProbeCache>();b=timed<ProbeCache>();}
        if(block>=4){oldTimes.push_back(a);newTimes.push_back(b);}}
    std::sort(oldTimes.begin(),oldTimes.end());std::sort(newTimes.begin(),newTimes.end());
    std::printf("PROBE_CACHE_TIMING replacements=131072 beforeMs=%.6f afterMs=%.6f\n",oldTimes[4],newTimes[4]);
    std::puts("PASS exact LRU, validity, negative keys, atlas aliases, layouts 16/22/42, capacity, clear/refill and zero warm replacement allocations");
}
