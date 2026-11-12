#include <atomic>
#include <cassert>
#include <cfenv>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>
#include <type_traits>
#include "current.h"
#include "before.h"
using Bounds=NorthlightReplayBounds::Bounds;
using Stamp=NorthlightConstantEpoch::Stamp;
using Current=NorthlightPoseBounds::Handoff;
using Before=BeforePoseBounds::Handoff;
static_assert(sizeof(Current)==sizeof(Before));
struct Sample {Bounds box;float bank[24]={};Stamp stamp;};
static unsigned seed=0x935ca71u;
static float random(float low,float high){seed=seed*1664525u+1013904223u;return low+(high-low)*float(seed>>8)/16777216.f;}
static Sample sample(unsigned kind,unsigned i){
    Sample s;s.box.valid=true;s.stamp.valid=s.stamp.held=true;s.stamp.reset=3;s.stamp.floats=7;s.stamp.booleans=11;s.stamp.integers=13;
    for(unsigned k=0;k<3;++k){const float center=random(-500,500),extent=random(0,150);s.box.low[k]=center-extent;s.box.high[k]=center+extent;}
    for(unsigned k=8;k<20;++k)s.bank[k]=random(-4,4);s.bank[22]=i&1?-1.f:1.f;
    if(kind==1){
        std::fill(s.bank,s.bank+24,0.f);s.bank[8]=s.bank[13]=s.bank[18]=s.bank[22]=1;s.bank[19]=-.5f;
        s.box={{0,0,2},{0,0,2},true};const unsigned axis=i%3;
        s.box.low[axis]=s.box.high[axis]=2.f+float(int(i%1025)-512)*std::numeric_limits<float>::epsilon()/4;
        if(i&2){s.box.low[axis]=-s.box.low[axis];s.box.high[axis]=s.box.low[axis];}
    }
    if(kind==2){s.bank[8]=999999;s.bank[9]=-999998;s.box.low[0]=s.box.high[0]=999999;s.box.low[1]=s.box.high[1]=999999;}
    if(kind==3){
        const unsigned value=i%12;
        if(value==0)s.bank[8]=NAN;if(value==1)s.bank[20]=INFINITY;if(value==2)s.bank[22]=.5f;
        if(value==3)s.box.low[0]=NAN;if(value==4)s.box.high[1]=INFINITY;if(value==5)s.box.valid=false;
        if(value==6)s.stamp.valid=false;if(value==7)s.stamp.held=false;if(value==8)s.box.low[2]=s.box.high[2]+1;
        if(value==9)s.bank[20]=std::numeric_limits<float>::denorm_min();
        if(value==10)for(unsigned k=8;k<20;++k)s.bank[k]=(k&1)?-0.f:0.f;
        if(value==11){s.box.low[2]=-0.f;s.box.high[2]=std::numeric_limits<float>::denorm_min();}
    }
    if(kind==4){
        std::fill(s.bank,s.bank+24,0.f);const float sign=i&1?-1.f:1.f;
        s.bank[8]=s.bank[13]=1;s.bank[18]=s.bank[22]=sign;s.bank[19]=-.5f;
        const float z=48.01f+float(int(i%1025)-512)*std::numeric_limits<float>::epsilon()*8;
        s.box={{-.1f,-.1f,z*sign},{.1f,.1f,z*sign},true};
    }
    return s;
}
static void compare(const Sample& s,unsigned nulls=0){
    int mesh=0,shader=0,decl=0;const void* m=nulls==1?nullptr:&mesh;const void* v=nulls==2?nullptr:&shader;const void* d=nulls==3?nullptr:&decl;
    Current a;Before b;a.publish(s.box,s.bank,s.stamp,m,v,d);b.publish(s.box,s.bank,s.stamp,m,v,d);b.outsideRange=b.outside(m,v,d,s.stamp);
    assert(a.bounds.valid==b.bounds.valid&&!std::memcmp(a.bounds.low,b.bounds.low,12)&&!std::memcmp(a.bounds.high,b.bounds.high,12));
    assert(a.source==b.source&&a.shader==b.shader&&a.declaration==b.declaration&&a.reset==b.reset&&a.floats==b.floats&&a.booleans==b.booleans&&a.integers==b.integers);
    assert(!std::memcmp(&a.clipSign,&b.clipSign,4)&&a.outsideRange==b.outsideRange&&(a.outsideRange||a.outsideViewport==b.outsideViewport));
    for(unsigned change=0;change<10;++change){auto current=s.stamp;
        if(change==1)++current.reset;if(change==2)++current.floats;if(change==3)++current.booleans;if(change==4)++current.integers;
        if(change==5)current.valid=false;if(change==6)current.held=false;
        for(bool clip:{false,true})assert(a.outside(change==7?nullptr:m,change==8?nullptr:v,change==9?nullptr:d,current,clip)==b.outside(change==7?nullptr:m,change==8?nullptr:v,change==9?nullptr:d,current,clip));
    }
}
template<class Proof> __attribute__((noinline)) static void publish(Proof& p,const Sample& s){p.publish(s.box,s.bank,s.stamp,&s,&s.bank,&s.stamp);if constexpr(std::is_same_v<Proof,Before>)p.outsideRange=p.outside(&s,&s.bank,&s.stamp,s.stamp);}
static volatile unsigned sink=0;
template<class Proof> static double timing(const std::vector<Sample>& samples){
    Proof p;const auto start=std::chrono::steady_clock::now();
    for(unsigned n=0;n<32768;++n){std::atomic_signal_fence(std::memory_order_seq_cst);publish(p,samples[n%64]);sink+=p.outsideRange||p.outsideViewport;}
    return std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/32768;
}
int main(int argc,char**){
    unsigned cases=0;const int original=std::fegetround();
    for(int mode:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO}){
        assert(std::fesetround(mode)==0);
        for(unsigned kind=0;kind<5;++kind)for(unsigned i=0;i<24000;++i){compare(sample(kind,i),i%47<4?i%47:0);++cases;}
    }
    assert(std::fesetround(original)==0);
    // Exhaust every float in the threshold's exponent bin, for both clip signs.
    auto s=sample(1,0);int mesh=0,shader=0,decl=0;Current a;Before b;
    a.publish(s.box,s.bank,s.stamp,&mesh,&shader,&decl);b.publish(s.box,s.bank,s.stamp,&mesh,&shader,&decl);
    unsigned thresholdFloats=0;
    for(std::uint32_t word=0x42000000u;word<0x42800000u;++word){float z;std::memcpy(&z,&word,4);
        for(float sign:{-1.f,1.f}){a.bounds.low[2]=b.bounds.low[2]=z*sign;a.bounds.high[2]=b.bounds.high[2]=z*sign;a.clipSign=b.clipSign=sign;
            assert(a.outside(&mesh,&shader,&decl,s.stamp)==b.outside(&mesh,&shader,&decl,s.stamp));}
        ++thresholdFloats;
    }
    assert(thresholdFloats==8388608);std::printf("POSE_VIEWPORT_THRESHOLD_PASS floats=%u signs=2 double_step=redundant\n",thresholdFloats);
    std::printf("POSE_VIEWPORT_MATH_PASS cases=%u roundingModes=4 bounds=exact admission=exact clip=exact stale=retained\n",cases);
    if(argc<2)return 0;
    for(unsigned kind=0;kind<5;++kind){std::vector<Sample> samples;for(unsigned i=0;i<64;++i){auto s=sample(0,i);
        std::fill(s.bank,s.bank+24,0.f);s.bank[8]=s.bank[13]=s.bank[18]=s.bank[22]=1;s.bank[19]=-.5f;s.box={{-.2f,-.2f,20},{.2f,.2f,21},true};
        if(kind==1)s.box.low[0]=100,s.box.high[0]=101;if(kind==2)s.box.low[1]=100,s.box.high[1]=101;if(kind==3)s.box.low[2]=-21,s.box.high[2]=-20;if(kind==4)s.box.low[2]=80,s.box.high[2]=81;
        compare(s);samples.push_back(s);}
        for(unsigned round=0;round<32;++round){double a=0,b=0;
            if(round&1){b=timing<Current>(samples);a=timing<Before>(samples);}else{a=timing<Before>(samples);b=timing<Current>(samples);}
            std::printf("POSE_VIEWPORT_MATH_TIME kind=%u round=%u beforeNs=%.6f afterNs=%.6f\n",kind,round,a,b);
        }
    }
}
