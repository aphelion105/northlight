#include <chrono>
struct RangeBudgetClock {
    using time_point=std::chrono::steady_clock::time_point;
    inline static unsigned calls=0;
    inline static unsigned msPerCall=1;
    static time_point now(){return time_point(std::chrono::milliseconds(++calls*msPerCall));}
};
#include "clocked_range.h"
#include "range_reference.h"
#include "model_normal_range.h"
#include <cassert>
#include <cstdio>
#include <random>
using Mesh=NorthlightDrawSnapshot::Mesh;
static std::shared_ptr<const Mesh> geometry(unsigned count=8,float z=0){
    auto m=std::make_shared<Mesh>();m->vertexCount=count;m->streams[0].stride=12;m->streams[0].bytes.resize(size_t(count)*12);
    for(unsigned i=0;i<count;++i){const float p[]={float(i%3)-1,float(i%5)-2,z+float(i%7)};std::memcpy(m->streams[0].bytes.data()+size_t(i)*12,p,12);}return m;
}
static void bank(float* c){std::fill(c,c+1024,0);c[8]=c[13]=c[18]=c[22]=1;c[31*4]=c[32*4+1]=c[33*4+2]=1;c[33*4+3]=80;}
int main(int argc,char**){D3DVERTEXELEMENT9 layout[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},D3DDECL_END()};float c[1024];bank(c);
    auto warm=geometry(),cold=geometry();ClockedModelNormalRange::Cache clocked;RangeBudgetClock::calls=0;
    assert(clocked.outside(warm,layout,2,c)&&RangeBudgetClock::calls==2);
    // One cold scan spends a deliberately exaggerated 1 ms. A warm proof
    // still uses the current pose and cannot consume more cold-scan time.
    for(unsigned i=0;i<1000;++i)assert(clocked.outside(warm,layout,2,c));
    assert(RangeBudgetClock::calls==2&&!clocked.outside(cold,layout,2,c));
    unsigned coldReads=0;assert(!clocked.outsideRead(cold,layout,2,[&](unsigned,unsigned,float*){++coldReads;return true;}));assert(coldReads==0);
    c[33*4+3]=20;assert(!clocked.outside(warm,layout,2,c));c[33*4+3]=80;
    c[31*4]=NAN;assert(!clocked.outside(warm,layout,2,c));c[31*4]=1;
    c[20]=.1f;assert(!clocked.outside(warm,layout,2,c));c[20]=0;
    assert(!clocked.outsideRead(warm,layout,2,[](unsigned,unsigned,float*){return false;}));assert(RangeBudgetClock::calls==2);
    clocked.beginFrame();assert(clocked.outside(cold,layout,2,c)&&RangeBudgetClock::calls==4);
    clocked.beginFrame();RangeBudgetClock::msPerCall=0;auto full=geometry(2048);assert(clocked.outside(full,layout,2,c));const auto clocks=RangeBudgetClock::calls;
    auto extra=geometry();assert(!clocked.outside(extra,layout,2,c));assert(clocked.outside(warm,layout,2,c)&&RangeBudgetClock::calls==clocks);
    clocked.clear();assert(!clocked.outside(geometry(2049),layout,2,c)&&RangeBudgetClock::calls==clocks);
    // A recycled address with a different owner cannot borrow old bounds.
    alignas(Mesh) unsigned char storage[sizeof(Mesh)];auto owner=[&](float z){return std::shared_ptr<const Mesh>(new(storage) Mesh(*geometry(8,z)),[](const Mesh* p){p->~Mesh();});};
    RangeBudgetClock::msPerCall=1;auto first=owner(0);clocked.beginFrame();assert(clocked.outside(first,layout,2,c));first.reset();auto next=owner(-200);clocked.beginFrame();assert(!clocked.outside(next,layout,2,c));next.reset();
    std::mt19937 rng(88);ReferenceModelNormalRange::Cache before;NorthlightModelNormalRange::Cache current;unsigned rejected=0;
    for(unsigned i=0;i<200000;++i){before.beginFrame();current.beginFrame();
        for(unsigned row=0;row<3;++row)for(unsigned column=0;column<4;++column)c[(31+row)*4+column]=float(int(rng()%20001)-10000)*.0002f;
        c[22]=i%7?1.f:-1.f;c[33*4+3]=c[22]*(i%3?80.f:48.01f+float(int(rng()%1001)-500)*.0001f);
        const bool a=before.outside(warm,layout,2,c),b=current.outside(warm,layout,2,c);assert(a==b);rejected+=b;
    }
    assert(rejected>100000);std::printf("RANGE_BUDGET_PASS cases=200000 warmAfterTimeBudget=1 warmAfterVertexBudget=1 currentPose=1 failedRead=1 sourceOwnerABA=1\n");
    if(argc==1)return 0;bank(c);
    const auto measure=[&](auto& cache,unsigned mode,unsigned block){unsigned culled=0;const auto start=std::chrono::steady_clock::now();
        for(unsigned frame=0;frame<128;++frame){cache.beginFrame();for(unsigned draw=0;draw<443;++draw){c[33*4+3]=draw<133?15.f:80.f;culled+=cache.outside(warm,layout,2,c);}}
        const double ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/(128*443);assert(culled==128*310);std::printf("RANGE_BUDGET_TIMING mode=%u block=%u warm=%u ns=%.5f\n",mode,block,unsigned(block<4),ns);};
    for(unsigned block=0;block<16;++block){const unsigned mode=(block/2)%2?1-block%2:block%2;if(mode)measure(current,mode,block);else measure(before,mode,block);}
}
