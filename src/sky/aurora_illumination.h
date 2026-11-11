#pragma once
#include "celestial_time_warp.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>

namespace NorthlightAurora {
inline float smooth(float x){x=std::clamp(x,0.f,1.f);return x*x*(3-2*x);}
inline float weight(const char* map,bool valid,double day,float sunZ,float moonZ){
    if(!map||std::strcmp(map,"Northrend")||!valid||!std::isfinite(day)||day<.5||day>=1||
       !std::isfinite(sunZ)||!std::isfinite(moonZ)||std::fabs(sunZ)>1||std::fabs(moonZ)>1)return 0;
    constexpr float fade=.0697564737f; // sin(4 degrees), entirely below the horizon.
    return smooth(-sunZ/fade)*smooth(-moonZ/fade);
}
struct Style { bool enabled=false; float gain=0,phase=0; std::uint64_t revision=0; };
// Shared additional emission for the visible native ribbons and their angular
// transport. At zero gain the original material remains unchanged.
inline Style style(const char* map,unsigned strength,bool valid,double day){
    Style out;
    const auto orbit=NorthlightCelestialOrbit::evaluateNative(valid?day:-1.);
    out.gain=4.f*float(std::min(strength,200u))*.01f*
        weight(map,orbit.valid,day,orbit.sun.direction[2],orbit.moon.direction[2]);
    if(out.gain<=0)return {};
    out.enabled=true;out.phase=smooth(float((day-.85)/.075));
    std::uint32_t gain=0,phase=0;std::memcpy(&gain,&out.gain,4);std::memcpy(&phase,&out.phase,4);
    out.revision=(std::uint64_t(gain)<<32)|phase;
    return out;
}
}
