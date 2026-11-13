#pragma once
#include "world_local_lights.h"
#include "world_gi.h"
#include <cmath>
#include <cstddef>
#include <vector>
#include <algorithm>
#include <array>

// A bounded FGS2 fixture tag is resolved to its exact FGL source identity on
// FGS3 instance expansion. Incomplete or ambiguous enclosures fail closed.
namespace LampFixtureMetadata {
using Triangles=std::vector<std::array<NorthlightGI::Vec3,3>>;
inline bool matches(const NorthlightLocalLights::Light& light,uint64_t sourceId,unsigned triangles,
                    NorthlightGI::Vec3 low,NorthlightGI::Vec3 high){
    const bool native=(light.kind==2||(light.kind==3&&!(light.flags&NorthlightLocalLights::GlassEmitter)))&&(light.flags&NorthlightLocalLights::CastShadow);
    if((!native&&(!(light.flags&NorthlightLocalLights::GlassEmitter)||(light.kind!=1&&light.kind!=3)))||
       !sourceId||sourceId!=light.sourceId||(native?(!triangles||triangles>4096):triangles!=8)||
       !(light.attenuationEnd>0.f)||!std::isfinite(light.attenuationEnd)||
       light.attenuationStart<0.f||!std::isfinite(light.attenuationStart))return false;
    const float radius=light.kind==1||native?light.attenuationEnd:light.attenuationStart;
    if(!(radius>0.f))return false;
    const float lo[3]={low.x,low.y,low.z},hi[3]={high.x,high.y,high.z};float distance=0;
    for(unsigned i=0;i<3;++i){
        if(!std::isfinite(lo[i])||!std::isfinite(hi[i])||lo[i]>hi[i]||hi[i]-lo[i]>2.f*radius||
           !std::isfinite(light.position[i]))return false;
        if((light.kind==1||native)&&(light.position[i]<lo[i]-.02f||light.position[i]>hi[i]+.02f))return false;
        const float delta=(lo[i]+hi[i])*.5f-light.position[i];distance+=delta*delta;
    }
    const float tolerance=.02f+.12f*radius;
    return light.kind==1||native||distance<=tolerance*tolerance;
}
template<class Batches>
inline size_t unique(const NorthlightLocalLights::Light& light,const Batches& batches){
    size_t found=size_t(-1);
    if(!(light.flags&NorthlightLocalLights::CastShadow))return found;
    const size_t count=batches.size();
    for(size_t at=0;at<count;++at){const size_t i=at;const auto& b=batches[i];
        if(!b.terrain&&matches(light,b.lampSourceId,b.count,b.boundsLow,b.boundsHigh)){
            if(found!=size_t(-1))return size_t(-1);found=i;
        }}
    return found;
}
// One M2 bulb can span several materials/components. Ownership comes from the
// same FGS3 instance/source identity; keep the legacy WMO uniqueness contract.
template<class Batches>
std::vector<size_t> parts(const NorthlightLocalLights::Light& light,const Batches& batches){
    if(light.kind==1||(light.flags&NorthlightLocalLights::GlassEmitter)){
        const size_t i=unique(light,batches);return i==size_t(-1)?std::vector<size_t>{}:std::vector<size_t>{i};
    }
    std::vector<size_t> result;unsigned triangles=0;NorthlightGI::Vec3 lo{},hi{};
    const size_t count=batches.size();
    for(size_t at=0;at<count;++at){const size_t i=at;const auto& b=batches[i];
        if(b.terrain||b.lampSourceId!=light.sourceId||!b.lampSourceId)continue;
        if(!b.count||b.count>4096-triangles)return {};
        const float low[]={b.boundsLow.x,b.boundsLow.y,b.boundsLow.z},high[]={b.boundsHigh.x,b.boundsHigh.y,b.boundsHigh.z};
        for(unsigned k=0;k<3;++k)if(!std::isfinite(low[k])||!std::isfinite(high[k])||low[k]>high[k])return {};
        if(result.empty()){lo=b.boundsLow;hi=b.boundsHigh;}
        else {lo={std::min(lo.x,b.boundsLow.x),std::min(lo.y,b.boundsLow.y),std::min(lo.z,b.boundsLow.z)};
              hi={std::max(hi.x,b.boundsHigh.x),std::max(hi.y,b.boundsHigh.y),std::max(hi.z,b.boundsHigh.z)};}
        triangles+=b.count;result.push_back(i);
    }
    if(!matches(light,light.sourceId,triangles,lo,hi))return {};
    return result;
}
}
