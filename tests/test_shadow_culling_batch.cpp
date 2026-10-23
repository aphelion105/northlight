#include "shadow_bounds.h"
#include "test_shadow_bounds_reference.h"
#include "world_math.h"
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
using NorthlightGI::Vec3;
// Freeze the previous interval implementation, including its exact arithmetic.
static bool baseline(Vec3 low,Vec3 high,const float* matrix,float epsilon=1e-4f,bool retainUpstream=false){
    if(!NorthlightShadowBounds::affineLightMatrix(matrix)||!std::isfinite(epsilon)||epsilon<0)return false;
    const float lo[]={low.x,low.y,low.z},hi[]={high.x,high.y,high.z};
    for(unsigned a=0;a<3;++a)if(!std::isfinite(lo[a])||!std::isfinite(hi[a])||lo[a]>hi[a])return false;
    double minimum[3],maximum[3];
    for(unsigned a=0;a<3;++a){
        double lower=matrix[12+a],upper=lower,magnitude=std::fabs(lower);
        for(unsigned r=0;r<3;++r){const double x=double(lo[r])*matrix[r*4+a],y=double(hi[r])*matrix[r*4+a];
            lower+=std::min(x,y);upper+=std::max(x,y);magnitude+=std::max(std::fabs(x),std::fabs(y));}
        const double margin=double(epsilon)+magnitude*(8.0*std::numeric_limits<float>::epsilon());
        minimum[a]=lower-margin;maximum[a]=upper+margin;
    }
    return maximum[0]<-1||minimum[0]>1||maximum[1]<-1||minimum[1]>1||(!retainUpstream&&maximum[2]<0)||minimum[2]>1;
}
static uint32_t seed=0x4591b3ae;
static float random(float lo,float hi){seed=seed*1664525u+1013904223u;return lo+(hi-lo)*(float(seed>>8)/16777216.f);}
struct Box {Vec3 low,high;};
template<bool Fast> __attribute__((noinline)) static uint64_t batch(const std::vector<Box>& boxes,const float* m,bool upstream,unsigned repeat){
    const bool affine=NorthlightShadowBounds::affineLightMatrix(m);uint64_t accepted=0;
    for(unsigned n=0;n<repeat;++n)for(const auto& b:boxes){
        bool rejected;
        if constexpr(Fast)rejected=affine&&NorthlightShadowBounds::clipRejectAffine(b.low,b.high,m,1e-4f,upstream);
        else rejected=baseline(b.low,b.high,m,1e-4f,upstream);
        accepted+=!rejected;
    }
    return accepted;
}
int main(int argc,char**){
    unsigned cases=0,retained=0,rejected=0;
    for(unsigned trial=0;trial<200000;++trial){
        Vec3 center{random(-17000,17000),random(-17000,17000),random(-3000,3000)};
        Vec3 e{random(0,400),random(0,400),random(0,700)};
        Box b{center-e,center+e};float m[16];
        auto light=NorthlightGI::normalized(Vec3{random(-1,1),random(-1,1),trial%3?random(.02f,1.f):.0001f});
        NorthlightWorldMath::shadowMatrix(center+Vec3{random(-1200,1200),random(-1200,1200),random(-600,600)},light,trial%2?48.f:192.f,m);
        switch(trial%19){
        case 0:m[3]=.001f;break; // perspective: fail open
        case 1:m[trial%16]=NAN;break;
        case 2:m[trial%16]=INFINITY;break;
        case 3:b.low.x=NAN;break;
        case 4:b.high.z=INFINITY;break;
        case 5:b.low.y=b.high.y+1;break;
        case 6:std::memset(m,0,sizeof m);m[15]=1;break; // singular affine
        default:break;}
        const float epsilon=trial%29==0?NAN:trial%31==0?-1.f:1e-4f;
        for(bool upstream:{false,true}){
            const bool a=baseline(b.low,b.high,m,epsilon,upstream);
            const bool c=NorthlightShadowBounds::clipReject(b.low,b.high,m,epsilon,upstream);
            const bool hoisted=NorthlightShadowBounds::affineLightMatrix(m)&&NorthlightShadowBounds::clipRejectAffine(b.low,b.high,m,epsilon,upstream);
            assert(a==c&&c==hoisted); // Exact retained/rejected sets of previous release.
            // No new rejection relative to the independent eight-corner oracle.
            assert(!c||ReferenceShadowBounds::clipReject(b.low,b.high,m,epsilon,upstream));
            ++cases;(c?rejected:retained)++;
        }
    }
    float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    const float edges[]={-1.0002f,-1.0001f,-1.f,-.9999f,-.0002f,-.0001f,-0.f,0.f,.0001f,.9999f,1.f,1.0001f,1.0002f};
    for(float x:edges)for(float y:edges)for(float z:edges)for(bool upstream:{false,true}){
        Vec3 p{x,y,z};assert(baseline(p,p,identity,1e-4f,upstream)==NorthlightShadowBounds::clipReject(p,p,identity,1e-4f,upstream));++cases;}
    assert(!NorthlightShadowBounds::clipReject({0,0,0},{1,1,1},nullptr));
    assert(retained>10000&&rejected>10000);
    std::printf("PASS exact shadow cull masks: %u cases (%u retained, %u rejected), independent corner safety, invalid/projective/singular/plane boundaries\n",cases,retained,rejected);
    if(argc>1){
        for(unsigned mode=0;mode<3;++mode){
            std::vector<Box> boxes;float m[16];NorthlightWorldMath::shadowMatrix({6000,650,650},NorthlightGI::normalized(Vec3{.7f,.3f,.035f}),192.f,m);
            for(unsigned n=0;n<4096;++n){
                const float spread=mode==0?15.f:mode==1?1200.f:4000.f;
                Vec3 c{6000+random(-spread,spread),650+random(-spread,spread),650+random(-spread*.25f,spread*.25f)};
                Vec3 e{random(0,15),random(0,15),random(0,30)};boxes.push_back({c-e,c+e});}
            std::vector<double> oldTimes,newTimes;uint64_t checksum=0;
            for(unsigned run=0;run<9;++run){
                auto timed=[&](bool fast){const auto start=std::chrono::steady_clock::now();const uint64_t result=fast?batch<true>(boxes,m,false,128):batch<false>(boxes,m,false,128);
                    checksum+=result;return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();};
                if(run%2){newTimes.push_back(timed(true));oldTimes.push_back(timed(false));}else{oldTimes.push_back(timed(false));newTimes.push_back(timed(true));}}
            assert(batch<true>(boxes,m,false,1)==batch<false>(boxes,m,false,1));
            std::sort(oldTimes.begin(),oldTimes.end());std::sort(newTimes.begin(),newTimes.end());
            std::printf("BENCH mode=%u baseline_ms=%.3f optimized_ms=%.3f ratio=%.4f checksum=%llu boxes_per_sample=524288\n",mode,oldTimes[4],newTimes[4],newTimes[4]/oldTimes[4],(unsigned long long)checksum);
        }
    }
}
