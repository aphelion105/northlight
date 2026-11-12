#include "static_shadow_coverage.h"
#include "world_math.h"
#include <cassert>
#include <cstdio>
#include <chrono>
#include <memory>
static bool reference(const float* matrix,NorthlightGI::Vec3 low,NorthlightGI::Vec3 high){
    if(low.x>high.x||low.y>high.y||low.z>high.z)return false;
    for(unsigned c=0;c<8;++c){const float x=c&1?high.x:low.x,y=c&2?high.y:low.y,z=c&4?high.z:low.z;
        float q[4];for(unsigned i=0;i<4;++i)q[i]=x*matrix[i]+y*matrix[4+i]+z*matrix[8+i]+matrix[12+i];
        for(float v:q)if(!std::isfinite(v))return false;
        if(q[3]<=0||q[0]<-q[3]||q[0]>q[3]||q[1]<-q[3]||q[1]>q[3]||q[2]<0||q[2]>q[3])return false;}
    return true;
}
static unsigned seed=0x629ab419;
static float random(float lo,float hi){seed=seed*1664525u+1013904223u;return lo+(hi-lo)*float(seed>>8)/16777216.f;}
struct Box{NorthlightGI::Vec3 low,high;};
#ifdef NORTHLIGHT_CULL_WORK_INCLUDE
struct CullWorker {
    using V=NorthlightGI::Vec3;
    struct Replay {unsigned staticProofMask=0,staticM2Mask=0;V staticProofLow,staticProofHigh;struct {float low[3]={},high[3]={};bool valid=false;} pointBounds;};
    struct ReplayCull {unsigned slot=0;float matrix[16]={},cached[16]={};std::vector<uint8_t> flags;};
    std::vector<std::unique_ptr<Replay>> replays;
#include NORTHLIGHT_CULL_WORK_INCLUDE
};
static void workerProof(){
    using namespace NorthlightGI;CullWorker worker;CullWorker::ReplayCull job;
    NorthlightWorldMath::shadowMatrix({-6000,1200,700},normalized(Vec3{.7f,.3f,.035f}),192.f,job.cached);
    NorthlightWorldMath::shadowMatrix({-5980,1200,700},normalized(Vec3{.7f,.3f,.035f}),48.f,job.matrix);
    for(unsigned i=0;i<4096;++i){auto p=std::make_unique<CullWorker::Replay>();p->staticProofMask=i&15;p->staticM2Mask=(i>>4)&15;
        Vec3 c{-6000+random(-300,300),1200+random(-300,300),700+random(-300,300)},e{random(0,15),random(0,15),random(0,30)};
        p->staticProofLow=c-e;p->staticProofHigh=c+e;p->pointBounds.valid=i%7!=0;
        const float lo[]={p->staticProofLow.x,p->staticProofLow.y,p->staticProofLow.z},hi[]={p->staticProofHigh.x,p->staticProofHigh.y,p->staticProofHigh.z};
        std::copy(lo,lo+3,p->pointBounds.low);std::copy(hi,hi+3,p->pointBounds.high);
        if(i%29==0)p->staticProofLow.x=NAN;worker.replays.push_back(std::move(p));}
    for(unsigned slot=0;slot<4;++slot){job.slot=slot;worker.replayCullWork(job);
        for(unsigned i=0;i<worker.replays.size();++i){const auto& p=*worker.replays[i];const unsigned bit=1u<<slot;
            const bool contained=reference(job.cached,p.staticProofLow,p.staticProofHigh);
            assert(bool((p.staticProofMask&bit)&&(job.flags[i]&1))==bool((p.staticProofMask&bit)&&contained));
            assert(bool((p.staticM2Mask&bit)&&(job.flags[i]&1))==bool((p.staticM2Mask&bit)&&contained));
            const bool clipped=p.pointBounds.valid&&NorthlightShadowBounds::clipReject(Vec3(p.pointBounds.low[0],p.pointBounds.low[1],p.pointBounds.low[2]),Vec3(p.pointBounds.high[0],p.pointBounds.high[1],p.pointBounds.high[2]),job.matrix);
            assert(bool(job.flags[i]&2)==clipped);
        }}
    std::puts("STATIC_COVERAGE_WORKER_PASS packets=4096 slots=4 WMO=exact M2=exact clip=exact");
}
#endif
template<bool Fast>__attribute__((noinline)) static unsigned batch(const std::vector<Box>& boxes,const float* matrix){
    unsigned result=0;for(unsigned repeat=0;repeat<128;++repeat)for(const auto& b:boxes)result+=Fast?StaticShadow::containsBounds(matrix,b.low,b.high):reference(matrix,b.low,b.high);return result;
}
int main(int argc,char**){
    using namespace NorthlightGI;
    for(Vec3 light:{normalized(Vec3(1,1,.035f)),normalized(Vec3(-1,1,.25f)),Vec3(0,0,1)}){
        float m[16];const Vec3 center(100,-200,30);
        NorthlightWorldMath::shadowMatrix(center,light,240,m);
        assert(StaticShadow::containsBounds(m,center-Vec3(1,1,1),center+Vec3(1,1,1)));
        const Vec3 clipped=center+light*650;
        assert(!StaticShadow::containsBounds(m,clipped-Vec3(1,1,1),clipped+Vec3(1,1,1)));
        assert(!StaticShadow::containsBounds(m,center-Vec3(1000,1,1),center+Vec3(1000,1,1)));
        auto frame=NorthlightWorldMath::shadowFrame(center,light,192);
        auto moved=NorthlightWorldMath::shadowFrame(center+light*40,light,192);
        long x,y;float z;assert(NorthlightWorldMath::cacheOffset(moved,frame,x,y,z));
        // New depth slab contains this object but the old cached one did not.
        Vec3 edge=center+light*660;
        NorthlightWorldMath::shadowMatrixFrom(frame,240,m);
        assert(!StaticShadow::containsBounds(m,edge-Vec3(1,1,1),edge+Vec3(1,1,1)));
        NorthlightWorldMath::shadowMatrixFrom(moved,240,m);
        assert(StaticShadow::containsBounds(m,edge-Vec3(1,1,1),edge+Vec3(1,1,1)));
    }
    unsigned cases=0,inside=0;
    for(unsigned n=0;n<200000;++n){
        Vec3 c{random(-17000,17000),random(-17000,17000),random(-3000,3000)},e{random(0,20),random(0,20),random(0,40)};
        float m[16];NorthlightWorldMath::shadowMatrix(c,normalized(Vec3{random(-1,1),random(-1,1),random(.0001f,1)}),n%2?48.f:192.f,m);
        c=c+Vec3{random(-300,300),random(-300,300),random(-300,300)};auto lo=c-e,hi=c+e;
        switch(n%29){case 0:m[3]=.001f;break;case 1:m[15]=-1;break;case 2:m[n%16]=NAN;break;case 3:hi.x=INFINITY;break;case 4:lo.y=hi.y+1;break;case 5:m[n%16]=1e38f;break;case 6:lo.z=-1e38f;hi.z=1e38f;break;default:break;}
        const bool expected=reference(m,lo,hi),actual=StaticShadow::containsBounds(m,lo,hi);assert(actual==expected);inside+=actual;++cases;
    }
    float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    const float edges[]={-1.000001f,-1.f,-.999999f,-.000001f,-0.f,0.f,.000001f,.999999f,1.f,1.000001f};
    for(float x:edges)for(float y:edges)for(float z:edges){Vec3 p{x,y,z};assert(reference(identity,p,p)==StaticShadow::containsBounds(identity,p,p));++cases;}
    assert(!StaticShadow::containsBounds(nullptr,{0,0,0},{1,1,1})&&inside>1000);
    std::printf("STATIC_COVERAGE_EXACT_PASS cases=%u inside=%u boundaries=legacy projective=legacy invalid=retained\n",cases,inside);
#ifdef NORTHLIGHT_CULL_WORK_INCLUDE
    workerProof();
#endif
    if(argc>1)for(unsigned mode=0;mode<3;++mode){
        float m[16];NorthlightWorldMath::shadowMatrix({6000,650,650},normalized(Vec3{.7f,.3f,.035f}),192.f,m);std::vector<Box> boxes;
        for(unsigned i=0;i<4096;++i){const float spread=mode==0?10.f:mode==1?300.f:4000.f;Vec3 c{6000+random(-spread,spread),650+random(-spread,spread),650+random(-spread*.2f,spread*.2f)},e{5,5,5};boxes.push_back({c-e,c+e});}
        double before[9],after[9];volatile unsigned sink=0;
        auto measure=[&](bool fast){auto start=std::chrono::steady_clock::now();sink+=fast?batch<true>(boxes,m):batch<false>(boxes,m);return std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/(128*4096);};
        for(unsigned n=0;n<9;++n){if(n%2){after[n]=measure(true);before[n]=measure(false);}else{before[n]=measure(false);after[n]=measure(true);}}
        assert(batch<true>(boxes,m)==batch<false>(boxes,m));std::sort(before,before+9);std::sort(after,after+9);
        std::printf("STATIC_COVERAGE_BENCH mode=%u beforeNs=%.4f afterNs=%.4f sink=%u scope=component_only\n",mode,before[4],after[4],unsigned(sink));
    }
    puts("PASS static coverage: full XY/depth containment, grazing light and cached-depth translation");
}
