#define NOMINMAX
#include <d3d9.h>
#include "pose_bounds_handoff.h"
#include <cassert>
#include <cstdio>
#include <cstring>

using NorthlightReplayBounds::Bounds;
using NorthlightPoseBounds::Handoff;
struct PreviousLayout {Bounds bounds;std::uint64_t reset,floats,booleans,integers;const void *source,*shader,*declaration;float clipSign;bool outsideRange;};
static_assert(sizeof(Handoff)==sizeof(PreviousLayout),"Viewport proof must fit the existing padding");
static unsigned seed=0x61ac57ef;
static float random(float low,float high){seed=seed*1664525u+1013904223u;return low+(high-low)*float(seed>>8)/16777216.f;}
template<class T>static bool cornersOutside(const Bounds& box,const float* bank){
    bool outside[6]={true,true,true,true,true,true};
    for(unsigned corner=0;corner<8;++corner){T q[4];
        for(unsigned c=0;c<4;++c){const auto* row=bank+8+c*4;
            q[c]=T(corner&1?box.high[0]:box.low[0])*row[0]+T(corner&2?box.high[1]:box.low[1])*row[1]+T(corner&4?box.high[2]:box.low[2])*row[2]+row[3];}
        const T plane[]={q[0]+q[3],q[3]-q[0],q[1]+q[3],q[3]-q[1],q[2],q[3]-q[2]};
        for(unsigned p=0;p<6;++p)outside[p]&=plane[p]<0;
    }
    for(bool value:outside)if(value)return true;return false;
}
int main(){
    int mesh,shader,declaration;NorthlightConstantEpoch::Stamp stamp;stamp.valid=stamp.held=true;stamp.reset=3;stamp.floats=7;stamp.booleans=11;stamp.integers=13;
    unsigned cases=0,culled=0,retained=0;
    for(unsigned i=0;i<160000;++i){
        float bank[24]={};for(unsigned c=0;c<12;++c)bank[8+c]=random(-4,4);bank[22]=i%2?-1.f:1.f;
        Bounds box;box.valid=true;
        for(unsigned k=0;k<3;++k){const float center=random(-500,500),extent=random(0,150);box.low[k]=center-extent;box.high[k]=center+extent;}
        if(i%19==0){bank[8]=999999;bank[9]=-999998;box.low[0]=box.high[0]=999999;box.low[1]=box.high[1]=999999;}
        Handoff proof;proof.publish(box,bank,stamp,&mesh,&shader,&declaration);assert(proof.bounds.valid);
        const bool range=std::nextafter(bank[22]>0?double(box.low[2]):-double(box.high[2]),-INFINITY)>48.01;
        assert(proof.outside(&mesh,&shader,&declaration,stamp)==range);
        assert(proof.outside(&mesh,&shader,&declaration,stamp,true)==(range||proof.outsideViewport));
        if(proof.outsideViewport){assert(cornersOutside<float>(box,bank)&&cornersOutside<long double>(box,bank));++culled;
            for(unsigned change=0;change<9;++change){auto current=stamp;
                if(change==0)++current.reset;if(change==1)++current.floats;if(change==2)++current.booleans;if(change==3)++current.integers;
                if(change==4)current.valid=false;if(change==5)current.held=false;
                assert(!proof.outside(change==6?nullptr:&mesh,change==7?nullptr:&shader,change==8?nullptr:&declaration,current,true));}
        }else ++retained;
        ++cases;
    }
    // A box touching a clip plane must remain, including cancellation cases.
    for(float sign:{-1.f,1.f})for(unsigned axis=0;axis<2;++axis)for(float side:{-1.f,1.f}){
        float bank[24]={};bank[8]=bank[13]=1;bank[18]=bank[22]=sign;bank[19]=-.5f;
        Bounds box{{-.1f,-.1f,2*sign},{.1f,.1f,2*sign},true};box.low[axis]=box.high[axis]=2*side;
        Handoff proof;proof.publish(box,bank,stamp,&mesh,&shader,&declaration);assert(proof.bounds.valid&&!proof.outsideViewport);++cases;
    }
    for(unsigned invalid=0;invalid<11;++invalid){
        float bank[24]={};bank[8]=bank[13]=bank[18]=bank[22]=1;Bounds box{{100,100,2},{101,101,3},true};auto current=stamp;
        if(invalid<4)bank[8+invalid]=NAN;if(invalid==4)bank[22]=.5f;if(invalid==5)box.low[0]=NAN;if(invalid==6)box.high[1]=INFINITY;
        if(invalid==7)box.low[2]=4;if(invalid==8)box.valid=false;if(invalid==9)current.valid=false;if(invalid==10)current.held=false;
        Handoff proof;proof.publish(box,bank,current,&mesh,&shader,&declaration);assert(!proof.outsideViewport&&!proof.outside(&mesh,&shader,&declaration,current,true));++cases;
    }
    assert(culled>10000&&retained>10000);
    std::printf("POSE_VIEWPORT_CULL_PASS cases=%u culled=%u retained=%u float_and_precise_corners=safe boundaries=retained stale_epochs=retained invalid=retained range=unchanged size=unchanged\n",cases,culled,retained);
}
