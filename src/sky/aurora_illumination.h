#pragma once
#include "forever_celestial.h"
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
struct Style { bool enabled=false; float gain=0,phase=0; std::uint64_t revision=0; float intensity=1; };
// Native emission is present whenever the selected Northrend ribbons exist.
// The orbital gate controls only their extra green/purple twilight emission.
inline Style style(const char* map,unsigned strength,bool valid,double day,bool forever=false){
    Style out;
    const auto orbit=NorthlightForeverSky::evaluateSelected(valid?day:-1.,forever);
    if(!map||std::strcmp(map,"Northrend")||!orbit.valid)return {};
    out.intensity=float(std::min(strength,200u))*.01f;
    out.gain=4.f*out.intensity*
        weight(map,orbit.valid,day,orbit.sun.direction[2],orbit.moon.direction[2]);
    if(out.intensity<=0){out.intensity=0;return out;}
    out.enabled=true;out.phase=smooth(float((day-.85)/.075));
    out.revision=14695981039346656037ull;
    for(float value:{out.gain,out.phase,out.intensity}){std::uint32_t bits=0;std::memcpy(&bits,&value,4);out.revision=(out.revision^bits)*1099511628211ull;}
    return out;
}
}
