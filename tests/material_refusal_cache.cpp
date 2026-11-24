#include "refusal_fixture.h"
#include <string>

template<class Cache> struct Owner {IDirect3DDevice9 device;Cache cache{&device};};
static unsigned bucket(IDirect3DTexture9* source,bool model=false){return unsigned(((reinterpret_cast<uintptr_t>(source)>>4)^unsigned(model))%16);}
struct Sources {
    std::vector<IDirect3DTexture9*> all,colliding,distinct;
    Sources(){std::array<std::vector<IDirect3DTexture9*>,16> groups;
        for(unsigned i=0;i<1024;++i){auto* t=new IDirect3DTexture9;t->identity=i+1;t->desc.Usage=D3DUSAGE_DEPTHSTENCIL;all.push_back(t);groups[bucket(t)].push_back(t);}
        const auto best=std::max_element(groups.begin(),groups.end(),[](const auto& a,const auto& b){return a.size()<b.size();});assert(best->size()>=9);colliding.assign(best->begin(),best->begin()+9);
        for(auto& group:groups)if(!group.empty())distinct.push_back(group.front());assert(distinct.size()>=2);
    }
    ~Sources(){for(auto* t:all){assert(t->refs==1);t->Release();}}
};
template<class Cache> static void collisionCase(Sources& s,bool fixed,bool collisions=true){
    const auto& sources=collisions?s.colliding:s.distinct;const bool starved=collisions&&!fixed;
    Owner<Cache> o;auto* a=sources[0];auto* b=sources[1];assert(a!=b&&((bucket(a)==bucket(b))==collisions));
    const unsigned before=a->descReads+b->descReads;
    for(unsigned i=0;i<1000;++i)assert(!o.cache.resolve(i%2?a:b));
    const unsigned descriptors=a->descReads+b->descReads-before;
    auto* useful=new IDirect3DTexture9;useful->identity=2017;auto* normal=o.cache.resolve(useful);
    assert(descriptors==(starved?8u:2u)&&bool(normal)!=starved);
    assert(o.cache.attempts==(starved?8u:3u));
    std::printf("REFUSAL_COLLISION variant=%s collisions=%u calls=1000 descriptors=%u usefulAdmitted=%u attempts=%u hits=%u deferred=%u\n",fixed?"current":"baseline",unsigned(collisions),descriptors,unsigned(normal!=nullptr),o.cache.attempts,o.cache.refusalHits,o.cache.attemptDeferred);
    o.cache.reset();assert(useful->refs==1);useful->Release();
}
static void exactKeys(Sources& s){
    Owner<CacheCurrent> o;auto* source=s.colliding[0];const unsigned before=source->descReads;
    assert(!o.cache.resolve(source)&&!o.cache.resolve(source));
    assert(!o.cache.resolve(source,true)&&!o.cache.resolve(source,true));
    ++source->version;assert(!o.cache.resolve(source)&&!o.cache.resolve(source));
    assert(source->descReads-before==3&&o.cache.attempts==3&&source->refs==4);
    const auto allocationsBefore=liveObjects;failAllocation=0;allocationCount=0;
    for(unsigned i=0;i<1000;++i)assert(!o.cache.resolve(source));
    failAllocation=-1;assert(allocationCount==0&&liveObjects==allocationsBefore);
    o.cache.endFrame();assert(source->refs==1&&!o.cache.attempts);
    assert(!o.cache.resolve(source)&&source->descReads-before==4);
    NorthlightQuality::Settings settings;settings.autoHeightDetails=74;o.cache.configure(settings);assert(source->refs==1&&!o.cache.attempts);
    assert(!o.cache.resolve(source));o.cache.reset();assert(source->refs==1&&!o.cache.attempts);
}
static void limitsAndOwners(Sources& s){
    Owner<CacheCurrent> o;unsigned reads=0;
    for(unsigned i=0;i<8;++i){auto* t=s.colliding[i];reads+=t->descReads;assert(!o.cache.resolve(t));}
    unsigned after=0;for(unsigned i=0;i<8;++i)after+=s.colliding[i]->descReads;assert(after-reads==8&&o.cache.attempts==8);
    const auto hits=o.cache.refusalHits;for(unsigned i=0;i<1000;++i)assert(!o.cache.resolve(s.colliding[i%8]));
    assert(o.cache.refusalHits-hits==1000&&o.cache.attempts==8);
    auto* ninth=s.colliding[8];const auto previous=ninth->descReads;
    assert(o.cache.cannotAdmit(ninth,true)&&!o.cache.resolve(ninth)&&ninth->descReads==previous&&ninth->refs==1);
    for(unsigned i=0;i<8;++i)assert(s.colliding[i]->refs==2);
    o.cache.endFrame();for(auto* t:s.colliding)assert(t->refs==1);
    assert(!o.cache.cannotAdmit(ninth,true)&&!o.cache.resolve(ninth)&&ninth->descReads==previous+1);
    auto* external=new IDirect3DTexture9;external->desc.Usage=D3DUSAGE_DEPTHSTENCIL;const auto objects=liveObjects;
    assert(!o.cache.resolve(external)&&external->refs==2);external->Release();assert(liveObjects==objects);
    o.cache.reset();assert(liveObjects==objects-1);
    // Failed generation remains frame-local: the same valid source retries
    // after endFrame, and successful maps keep their ordinary cache/lease path.
    auto* valid=new IDirect3DTexture9;valid->identity=72;o.device.failDraw=true;assert(!o.cache.resolve(valid));const auto desc=valid->descReads;
    assert(!o.cache.resolve(valid)&&valid->descReads==desc);o.cache.endFrame();assert(o.cache.resolve(valid));o.cache.reset();assert(valid->refs==1);valid->Release();
}
static void spentGenerationBudget(Sources& s){
    Owner<CacheCurrent> o;
    auto* source=new IDirect3DTexture9;
    assert(!o.cache.cannotAdmit(source,true)&&o.cache.resolve(source,true));
    auto* other=new IDirect3DTexture9;
    assert(o.cache.resolve(other,false));
    assert(o.cache.cannotAdmit(s.colliding[0],true)&&o.cache.cannotAdmit(source,false));
    assert(!o.cache.cannotAdmit(source,true)&&!o.cache.cannotAdmit(other,false));
    ++source->version; // Inspect live entries through the ordinary resolver.
    assert(!o.cache.cannotAdmit(source,true)&&!o.cache.resolve(source,true));
    assert(o.cache.cannotAdmit(source,true));
    o.cache.reset();assert(o.cache.cannotAdmit(source,true));
    o.cache.endFrame();assert(!o.cache.cannotAdmit(source,true));
    source->Release();other->Release();
}
int main(){
    std::setvbuf(stdout,nullptr,_IONBF,0);
    {Sources sources;
#ifdef NORTHLIGHT_REFUSAL_BASELINE
     collisionCase<CacheBaseline>(sources,false);
     collisionCase<CacheBaseline>(sources,false,false);
#endif
     collisionCase<CacheCurrent>(sources,true);collisionCase<CacheCurrent>(sources,true,false);exactKeys(sources);limitsAndOwners(sources);spentGenerationBudget(sources);
    }
    assert(!liveObjects);std::puts("MATERIAL_REFUSAL_PASS keys=source_model_revision attempts=8 refs=balanced frame_reset_retry=1 allocations_on_hits=0");
}
