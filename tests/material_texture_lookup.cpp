#include "particle_texture_stats.h"
#include "material_texture_lookup_reference.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>

static const void* key(unsigned n){return reinterpret_cast<const void*>(std::uintptr_t(16u*(n+1)));}
static std::uint32_t randomState=0x59a18b27;
static unsigned randomValue(){randomState^=randomState<<13;randomState^=randomState>>17;randomState^=randomState<<5;return randomState;}
template<class A,class B>static void equal(A& a,B& b,unsigned index){
    const auto raw=key(index);assert(a.version(raw)==b.version(raw));
    NorthlightParticleTextures::View x;MaterialLookupReferenceParticles::View y;
    const bool ax=a.lookup(raw,x),by=b.lookup(raw,y);assert(ax==by);
    if(ax){assert(x.width==y.width&&x.height==y.height&&x.mip==y.mip&&x.format==y.format&&x.lifetime==y.lifetime&&x.revision==y.revision&&x.rgbaHash==y.rgbaHash);
        assert(!std::memcmp(x.rgba,y.rgba,std::size_t(x.width)*x.height*4));}
    assert(a.bytes()==b.bytes()&&a.size()==b.size());
    assert(!std::memcmp(&a.counts,&b.counts,sizeof a.counts));
}
template<class C>static void insert(C& c,unsigned index,unsigned owner,unsigned pool,const std::vector<std::uint8_t>& bytes){
    c.upload(key(index),key(owner+2048),32,32,0,21,bytes.data(),bytes.size(),128,true,true,pool);
}
template<class C>__attribute__((noinline)) static std::uint64_t query(C* c,const void* raw){return c->version(raw);}
int main(int argc,char**){
    NorthlightMaterialTextures::Cache current;MaterialLookupReference::Cache reference;
    std::vector<std::uint8_t> bytes(32*32*4,255),patch(4*4*4,96);
    insert(current,0,0,1,bytes);insert(reference,0,0,1,bytes);
    // A cached miss must not conceal a later insertion, including after the
    // positive recent hint moved to another identity.
    assert(!current.version(key(1))&&!reference.version(key(1)));
    insert(current,1,1,0,bytes);insert(reference,1,1,0,bytes);equal(current,reference,0);equal(current,reference,1);
    assert(current.version(key(1)));
    unsigned comparisons=0;
    for(unsigned n=0;n<12000;++n){
        const unsigned index=randomValue()%320,other=randomValue()%320,owner=randomValue()%4,operation=randomValue()%12;
        if(operation<=2){
            bytes[randomValue()%bytes.size()]^=std::uint8_t(randomValue());
            const unsigned pool=(index^owner)&1;insert(current,index,owner,pool,bytes);insert(reference,index,owner,pool,bytes);
        }else if(operation==3){current.invalidate(key(index));reference.invalidate(key(index));}
        else if(operation==4){current.erase(key(index));reference.erase(key(index));}
        else if(operation==5){current.reset();reference.reset();}
        else if(operation==6){current.clear();reference.clear();}
        else if(operation==7){assert(current.copy(key(other),key(index),key(owner+2048),1)==reference.copy(key(other),key(index),key(owner+2048),1));}
        else if(operation==8){assert(current.updateRect(key(index),key(owner+2048),32,32,0,21,patch.data(),patch.size(),16,4,4,8,8,true)==
                                   reference.updateRect(key(index),key(owner+2048),32,32,0,21,patch.data(),patch.size(),16,4,4,8,8,true));}
        else if(operation==9){insert(current,index,owner+1,0,bytes);insert(reference,index,owner+1,0,bytes);}
        else if(operation==10){current.erase(key(index),key(owner+2048));reference.erase(key(index),key(owner+2048));}
        for(unsigned probe:{index,other,(index+1)%320,319u}){equal(current,reference,probe);++comparisons;}
    }
    current.clear();reference.clear();
    for(unsigned i=0;i<320;++i){insert(current,i,i,1,bytes);insert(reference,i,i,1,bytes);equal(current,reference,i);}
    for(unsigned i=0;i<320;++i)equal(current,reference,i);
    current.clear();reference.clear();
    std::printf("MATERIAL_LOOKUP_PASS comparisons=%u insert_after_miss=1 replacement=1 owner_reuse=1 revisions=exact counters=exact pixels=exact limits=unchanged\n",comparisons);
    if(argc>1){
        volatile std::uint64_t sink=0;
        for(unsigned population:{0u,16u,128u})for(unsigned batch:{1u,8u,1024u}){
            NorthlightMaterialTextures::Cache now;MaterialLookupReference::Cache old;
            for(unsigned i=0;i<population;++i){insert(now,i,i,1,bytes);insert(old,i,i,1,bytes);}
            auto measure=[&](bool candidate){const auto start=std::chrono::steady_clock::now();for(unsigned n=0;n<200000;++n){const auto raw=key(1024+(n/batch)%32);sink+=candidate?query(&now,raw):query(&old,raw);}return std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/200000;};
            double before[8],after[8];for(unsigned block=0;block<8;++block){if(block%4==1||block%4==2){after[block]=measure(true);before[block]=measure(false);}else{before[block]=measure(false);after[block]=measure(true);}}
            std::sort(before,before+8);std::sort(after,after+8);
            std::printf("MATERIAL_LOOKUP_BENCH population=%u missBatch=%u beforeNs=%.4f afterNs=%.4f scope=metadata_lookup_not_frame\n",population,batch,(before[3]+before[4])*.5,(after[3]+after[4])*.5);
        }assert(!sink);
    }
}
