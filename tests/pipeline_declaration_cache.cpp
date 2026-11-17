#include "pipeline_declaration_fixture.h"
#include <chrono>
#include <random>
#include <string>

static void oracle(){
    std::array<IDirect3DVertexDeclaration9,192> objects;std::array<uint64_t,192> age{};uint64_t clock=0;
    for(unsigned i=0;i<objects.size();++i)objects[i].layoutOffset=i*4;
    NorthlightVertexDeclarations::Cache cache;const D3DVERTEXELEMENT9* elements;UINT count;
    std::mt19937 random(17623);uint64_t digest=0;
    for(unsigned step=0;step<50000;++step){
        if(step%997==0){cache.clear();age={};for(auto& o:objects)assert(o.refs==1);}
        const unsigned id=random()%objects.size();auto& o=objects[id];
        const bool hit=age[id]!=0;const auto reads=o.reads;
        if(!hit&&step%97==0)o.layoutOffset=(id*4+step)%1024;
        o.failure=!hit&&step%37==0;o.invalid=!hit&&step%41==0;
        const bool expected=hit||(!o.failure&&!o.invalid);
        assert(cache.get(&o,elements,count)==expected);
        assert(o.reads==reads+!hit);
        if(expected){
            if(!hit){unsigned held=0;for(auto a:age)held+=a!=0;
                if(held==128){unsigned old=0;while(!age[old])++old;for(unsigned i=old+1;i<age.size();++i)if(age[i]&&age[i]<age[old])old=i;age[old]=0;}
            }
            age[id]=++clock;assert(count==2&&elements[1].Stream==0xff&&elements[0].Offset==o.layoutOffset);UINT extent[4];assert(cache.captureLayout(&o,extent));assert(extent[0]==4+o.layoutOffset);
        }else assert(!elements&&!count);
        o.failure=o.invalid=false;
        for(unsigned i=0;i<age.size();++i)assert(objects[i].refs==1+bool(age[i]));
        digest=digest*33+id*2+expected;
    }
    cache.clear();for(const auto& o:objects)assert(o.refs==1);
    assert(!cache.get(nullptr,elements,count)&&!elements&&!count&&!cache.captureLayout(&objects[0],nullptr));
    std::printf("DECLARATION_ORACLE_PASS operations=50000 digest=%llu cacheBytes=%zu exactLRU=1\n",(unsigned long long)digest,sizeof cache);
}
static void benchmark(unsigned diversity){
    std::array<IDirect3DVertexDeclaration9,129> objects;NorthlightVertexDeclarations::Cache cache;const D3DVERTEXELEMENT9* elements;UINT count;
    for(unsigned i=0;i<objects.size();++i)objects[i].layoutOffset=i*4;
    for(unsigned i=0;i<diversity;++i)assert(cache.get(&objects[i],elements,count));
    uint64_t digest=0;const auto start=std::chrono::steady_clock::now();
    for(unsigned i=0;i<262144;++i){auto& o=objects[(i*73)%diversity];assert(cache.get(&o,elements,count));digest=digest*33+count+elements[0].Offset;}
    const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    unsigned reads=0;for(const auto& o:objects)reads+=o.reads;
    std::printf("PIPELINE_TIMING stage=declaration-%u cpuMs=%.6f digest=%llu reads=%u cacheBytes=%zu\n",diversity,ms,(unsigned long long)digest,reads,sizeof cache);
}
int main(int argc,char** argv){if(argc>1)benchmark(unsigned(std::stoul(argv[1])));else oracle();}
