#pragma once
#include "celestial_time_warp.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace NorthlightAurora {
inline float smooth(float x){x=std::clamp(x,0.f,1.f);return x*x*(3-2*x);}
inline float weight(const char* map,bool valid,double day,float sunZ,float moonZ){
    if(!map||std::strcmp(map,"Northrend")||!valid||!std::isfinite(day)||day<.5||day>=1||
       !std::isfinite(sunZ)||!std::isfinite(moonZ)||std::fabs(sunZ)>1||std::fabs(moonZ)>1)return 0;
    constexpr float fade=.0697564737f; // sin(4 degrees), entirely below the horizon.
    return smooth(-sunZ/fade)*smooth(-moonZ/fade);
}
// Recolour the existing GI sky source. Upper-hemisphere paths, roof occlusion,
// material bounces and the cached colour response remain the solver's work.
inline bool skyRadiance(const char* map,unsigned strength,bool valid,double day,const float ambient[3],float out[3]){
    std::copy(ambient,ambient+3,out);
    const auto orbit=NorthlightCelestialOrbit::evaluateNative(valid?day:-1.);
    const float gain=.2f*float(std::min(strength,200u))*.01f*
        weight(map,orbit.valid,day,orbit.sun.direction[2],orbit.moon.direction[2]);
    if(gain<=0)return false;
    for(unsigned k=0;k<3;++k)if(!std::isfinite(ambient[k])||ambient[k]<0)return false;
    const float luminance=ambient[0]*.2126f+ambient[1]*.7152f+ambient[2]*.0722f;
    if(luminance<=0)return false;
    const float purple=smooth(float((day-.85)/.075));
    constexpr float green[3]={.2f,1.f,.45f},violet[3]={.85f,.25f,1.f};
    for(unsigned k=0;k<3;++k)out[k]+=luminance*gain*(green[k]+purple*(violet[k]-green[k]));
    return true;
}
}
