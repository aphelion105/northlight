#include "replay_bounds_metadata.h"
#include <cassert>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
struct Counts { unsigned adds=0,releases=0,dead=0; } counts;
template<class Tag> struct Com {
    unsigned refs=1;bool alive=true,callerOwns=true;
    unsigned AddRef(){assert(alive&&refs);++counts.adds;return ++refs;}
    unsigned Release(){assert(alive&&refs);++counts.releases;if(!--refs){alive=false;++counts.dead;}return refs;}
    void dropCaller(){assert(callerOwns);callerOwns=false;Release();}
    ~Com(){assert(refs==unsigned(callerOwns));if(callerOwns)dropCaller();assert(!alive);}
};
struct ShaderTag {};struct DeclarationTag {};
using Shader=Com<ShaderTag>;using Declaration=Com<DeclarationTag>;
struct Prepared {
    static unsigned live,created;
    const size_t charge;const std::vector<unsigned> values;
    Prepared(size_t n,unsigned value):charge(n),values{value,value+1}{++live;++created;}
    ~Prepared(){assert(live);--live;}
    size_t bytes()const{return charge;}
};
unsigned Prepared::live=0;unsigned Prepared::created=0;
using Cache=NorthlightReplayMetadata::Cache<Prepared,Shader,Declaration>;
using Ptr=std::shared_ptr<const Prepared>;
Ptr make(size_t bytes=64,unsigned value=7){return std::make_shared<const Prepared>(bytes,value);}

void identitiesAndLifetime(){
    Shader a,b;Declaration x,y;Cache cache;unsigned builds=0;
    auto build=[&](){++builds;return make(64,builds);};
    auto first=cache.get(&a,&x,build);assert(first&&builds==1&&a.refs==2&&x.refs==2);
    assert(cache.get(&a,&x,build)==first&&builds==1);
    auto ay=cache.get(&a,&y,build),bx=cache.get(&b,&x,build);
    assert(ay&&bx&&ay!=first&&bx!=first&&builds==3&&cache.entries()==3);
    cache.invalidateShader(&a);
    assert(cache.entries()==1&&a.refs==1&&x.refs==2&&y.refs==1&&b.refs==2);
    assert(first->values[0]==1&&ay->values[0]==2);
    a.dropCaller();y.dropCaller();assert(!a.alive&&!y.alive);
    // Cache-owned references keep the exact pointer pair alive after the caller releases it.
    b.dropCaller();x.dropCaller();assert(b.alive&&x.alive);
    assert(cache.get(&b,&x,build)==bx&&builds==3);
    cache.clear();assert(!b.alive&&!x.alive&&cache.bytes()==0&&cache.entries()==0);
    assert(bx->values[0]==3); // Immutable metadata does not depend on COM lifetime.
}

void preparationQuotaAndFailures(){
    Shader shader;Declaration decls[8];Cache cache;unsigned builds=0;
    auto build=[&](){++builds;return make();};
    for(unsigned i=0;i<1000;++i)assert(!cache.find(&shader,&decls[i%8]));
    assert(!cache.entries()&&!cache.stats().misses&&!cache.stats().fallbacks&&!builds);
    for(unsigned i=0;i<4;++i)assert(cache.get(&shader,&decls[i],build));
    assert(!cache.get(&shader,&decls[4],build)&&builds==4);
    assert(cache.get(&shader,&decls[0],build)&&builds==4); // Quota never blocks hits.
    assert(cache.stats().prepared==4&&cache.stats().fallbacks==1&&cache.stats().hits==1);
    cache.beginFrame();assert(cache.stats().hits==0&&cache.stats().prepared==0);
    assert(cache.get(&shader,&decls[4],build)&&builds==5);
    const unsigned adds=counts.adds;const size_t entries=cache.entries(),bytes=cache.bytes();
    assert(!cache.get(nullptr,&decls[5],build)&&!cache.get(&shader,nullptr,build));
    assert(!cache.get(&shader,&decls[5],[]()->Ptr{return {};}));
    assert(!cache.get(&shader,&decls[6],[]()->Ptr{throw std::runtime_error("prepare failed");}));
    assert(!cache.get(&shader,&decls[7],[]{return make(std::numeric_limits<size_t>::max());}));
    assert(counts.adds==adds&&cache.entries()==entries&&cache.bytes()==bytes);
    assert(builds==5); // Rejected paths did not execute the unrelated builder.
    assert(!cache.get(&shader,&decls[5],build)&&builds==5); // Failed attempts consume quota.
    cache.beginFrame();assert(cache.get(&shader,&decls[5],build)&&builds==6);
}

void countLimitAndRetainedPacket(){
    std::vector<std::unique_ptr<Shader>> shaders;
    for(unsigned i=0;i<130;++i)shaders.emplace_back(new Shader);
    Declaration declaration;Cache cache;Ptr retained;
    for(unsigned i=0;i<128;++i){if(i%4==0)cache.beginFrame();auto p=cache.get(shaders[i].get(),&declaration,[i]{return make(64,i);});assert(p);if(i==1)retained=p;}
    assert(cache.entries()==128&&declaration.refs==129);
    cache.beginFrame();assert(cache.get(shaders[0].get(),&declaration,[]()->Ptr{assert(false);return {};}));
    assert(cache.get(shaders[128].get(),&declaration,[]{return make(64,128);}));
    assert(cache.entries()==128&&cache.stats().evictions==1);
    assert(shaders[0]->refs==2&&shaders[1]->refs==1&&shaders[2]->refs==2);
    shaders[1]->dropCaller();assert(!shaders[1]->alive&&retained->values[0]==1);
    assert(cache.get(shaders[129].get(),&declaration,[]{return make(64,129);}));
    assert(shaders[2]->refs==1&&cache.stats().evictions==2);
    cache.clear();assert(declaration.refs==1&&cache.bytes()==0&&retained->values[1]==2);
}

void byteLimit(){
    Shader shader;Declaration decls[4];Cache cache;
    assert(cache.get(&shader,&decls[0],[]{return make(64);}));
    const size_t overhead=cache.bytes()-64,limit=2u*1024u*1024u;
    cache.clear();
    assert(cache.get(&shader,&decls[0],[&]{return make(limit-overhead);}));
    assert(cache.bytes()==limit&&cache.entries()==1);
    assert(!cache.get(&shader,&decls[1],[&]{return make(limit-overhead+1);}));
    assert(cache.bytes()==limit&&decls[0].refs==2&&decls[1].refs==1);
    assert(cache.get(&shader,&decls[1],[]{return make(700000);}));
    assert(cache.stats().evictions==1&&decls[0].refs==1);
    assert(cache.get(&shader,&decls[2],[]{return make(700000);}));
    cache.beginFrame();assert(cache.get(&shader,&decls[3],[]{return make(700000);}));
    assert(cache.entries()==2&&cache.bytes()<=limit&&decls[1].refs==1);
    assert(cache.stats().evictions==1);
    // Destruction, without explicit clear, releases both remaining entries.
}
}

int main(){
    identitiesAndLifetime();preparationQuotaAndFailures();countLimitAndRetainedPacket();byteLimit();
    assert(Prepared::live==0);
    assert(counts.releases==counts.adds+counts.dead);
    std::cout<<"metadata cache: identities, hit reuse, shader invalidation, caller release, retained packets, frame quota, count/byte LRU, null/throw/oversize fallback and destruction passed; prepared="<<Prepared::created<<" COM_adds="<<counts.adds<<" COM_releases="<<counts.releases<<" objects_destroyed="<<counts.dead<<'\n';
}
