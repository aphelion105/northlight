#pragma once
#include "replay_bounds.h"
#include "captured_constant_epoch.h"

namespace NorthlightPoseBounds {
// Ephemeral metadata; the containing CaptureSource owns the raw mesh/declaration.
// No pose cache: only an unchanged current bank can consume this draw's result.
struct Handoff {
    NorthlightReplayBounds::Bounds bounds;
    // Exactly the words compared by ConstantEpoch::exact; block dirtiness is
    // unnecessary because this consumer never accepts a partially changed bank.
    std::uint64_t reset=0,floats=0,booleans=0,integers=0;
    const void *source=nullptr,*shader=nullptr,*declaration=nullptr;
    float clipSign=0;bool outsideRange=false,outsideViewport=false;
    bool bind(const NorthlightConstantEpoch::Stamp& current,const void* mesh,const void* vs,const void* decl){
        if(!current.valid||!current.held||!mesh||!vs||!decl)return false;
        reset=current.reset;floats=current.floats;booleans=current.booleans;integers=current.integers;
        source=mesh;shader=vs;declaration=decl;return true;
    }
    void publishFar(const NorthlightConstantEpoch::Stamp& current,const void* mesh,const void* vs,const void* decl){
        *this={};outsideRange=bind(current,mesh,vs,decl);
    }
    void publish(const NorthlightReplayBounds::Bounds& value,const float* bank,
                 const NorthlightConstantEpoch::Stamp& current,const void* mesh,const void* vs,const void* decl){
        *this={};if(!value.valid||!bank||!current.valid||!current.held||!mesh||!vs||!decl)return;
        if(bank[20]!=0||bank[21]!=0||bank[23]!=0||(bank[22]!=1&&bank[22]!=-1))return;
        for(unsigned i=8;i<24;++i)if(!std::isfinite(bank[i])||std::fabs(bank[i])>1000000)return;
        for(unsigned i=0;i<3;++i)if(!std::isfinite(value.low[i])||!std::isfinite(value.high[i])||value.low[i]>value.high[i]||
            std::max(std::fabs(value.low[i]),std::fabs(value.high[i]))>1000000)return;
        bounds=value;bind(current,mesh,vs,decl);clipSign=bank[22];
        // A current range proof already rejects this receiver for every viewport.
        outsideRange=outside(mesh,vs,decl,current);if(outsideRange)return;
        // Native projection is DP4(c2..c5, view.xyz1). Enclose every clip
        // component before testing a plane, including float cancellation.
        double lower[4],upper[4];
        for(unsigned c=0;c<4;++c){
            const float* row=bank+8+c*4;double lo=row[3],hi=lo,magnitude=std::fabs(lo);
            for(unsigned axis=0;axis<3;++axis){const double a=double(value.low[axis])*row[axis],b=double(value.high[axis])*row[axis];
                lo+=std::min(a,b);hi+=std::max(a,b);magnitude+=std::max(std::fabs(a),std::fabs(b));}
            const double margin=32.*std::numeric_limits<float>::epsilon()*(magnitude+1.);
            lower[c]=lo-margin;upper[c]=hi+margin;
        }
        outsideViewport=upper[0]+upper[3]<0||lower[0]>upper[3]||upper[1]+upper[3]<0||lower[1]>upper[3]||upper[2]<0||lower[2]>upper[3];
    }
    bool outside(const void* mesh,const void* vs,const void* decl,const NorthlightConstantEpoch::Stamp& current,bool clip=false)const{
        if(!current.valid||!current.held||source!=mesh||shader!=vs||declaration!=decl||
           reset!=current.reset||floats!=current.floats||booleans!=current.booleans||integers!=current.integers)return false;
        if(outsideRange)return true;
        if(clip&&outsideViewport)return true;
        if(!bounds.valid)return false;
        const double lower=clipSign==1?double(bounds.low[2]):clipSign==-1?-double(bounds.high[2]):-INFINITY;
        // Float bounds adjacent to 48.01 are many double ULPs away. Stepping
        // their exact double conversion once cannot change this comparison.
        return lower>48.01;
    }
};
}
