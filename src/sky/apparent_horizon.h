#pragma once
// The far edge as the camera sees it: the surface (ground, or the water raised over it in the regional fog
// field) under the eye and toward the sun, seen at the far clip. A camera high over the sea sees the edge under
// the mathematical horizon; a low one sees it on the horizon. The composite's horizon haze band starts at it (and
// the veil's haze at the sun); the discs, their glow and the light end at the true horizon like the native billboard
// (celestial_disc_effects.hlsl, discVisibility), so camera moves never switch shadows or GI. Pure: no D3D.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace NorthlightApparentHorizon {
// The deepest edge (sin 3 deg): the parked bodies (10 deg under, celestial_time_warp.h) never come back into view.
inline constexpr float Deepest=.0523f;
inline constexpr float Reach[]={0.f,120.f,240.f};
// sample(x, y, height) -> true with the surface height there. 0 (the horizon) without any surface.
template<class Sample> float sine(const float* eye,const float* sun,float farZ,Sample sample){
    if(!eye||!sun||!std::isfinite(farZ)||!(farZ>1)||!std::isfinite(eye[0])||!std::isfinite(eye[1])||!std::isfinite(eye[2]))return 0;
    float hx=sun[0],hy=sun[1];const float length=std::sqrt(hx*hx+hy*hy);
    if(std::isfinite(length)&&length>.01f){hx/=length;hy/=length;}else{hx=1;hy=0;}
    bool found=false;float lowest=0;
    for(float r:Reach){float height=0;
        if(sample(eye[0]+hx*r,eye[1]+hy*r,height)&&std::isfinite(height)){lowest=found?std::min(lowest,height):height;found=true;}}
    if(!found)return 0;
    const float dz=lowest-eye[2];
    if(!(dz<0))return 0;
    return std::max(-Deepest,dz/std::sqrt(dz*dz+farZ*farZ));
}
// 0.4 s low-pass (frame-rate independent); a gap over 2 s or a first value starts at the target.
struct Smooth {
    bool valid=false;std::uint32_t tick=0;float value=0;
    float update(float target,std::uint32_t now){
        if(!std::isfinite(target))target=0;
        target=std::clamp(target,-Deepest,0.f);
        const std::uint32_t elapsed=now-tick;
        if(!valid||elapsed>2000)value=target;
        else value+=(target-value)*float(-std::expm1(-double(elapsed)/400.));
        valid=true;tick=now;return value;
    }
    void reset(){valid=false;value=0;}
};
} // namespace NorthlightApparentHorizon
