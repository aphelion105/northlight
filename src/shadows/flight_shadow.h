#pragma once
#include "world_gi.h"
#include "shadow_cascades.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace NorthlightFlightShadow {
using NorthlightGI::Vec3;
constexpr float MaximumRadius=768.f,Step=96.f;
// Terrain only: an empty local GI volume in flight does not mean empty ground.
inline float ground(const NorthlightGI::WorldScene& scene,Vec3 at){
    float height=-std::numeric_limits<float>::infinity();
    for(const auto& t:scene.triangles){
        const auto a=scene.vertices[t.v0].position,b=scene.vertices[t.v1].position,c=scene.vertices[t.v2].position;
        if(at.x<std::min({a.x,b.x,c.x})||at.x>std::max({a.x,b.x,c.x})||at.y<std::min({a.y,b.y,c.y})||at.y>std::max({a.y,b.y,c.y}))continue;
        const double det=double(b.x-a.x)*(c.y-a.y)-double(b.y-a.y)*(c.x-a.x);
        if(std::fabs(det)<1e-12)continue;
        const double u=(double(at.x-a.x)*(c.y-a.y)-double(at.y-a.y)*(c.x-a.x))/det;
        const double v=(double(b.x-a.x)*(at.y-a.y)-double(b.y-a.y)*(at.x-a.x))/det;
        if(u<-.00001||v<-.00001||u+v>1.00001)continue;
        const float z=float(a.z+u*(b.z-a.z)+v*(c.z-a.z));
        if(z<=at.z+4)height=std::max(height,z);
    }
    return height;
}
inline float terrainReach(float radius){
    const float cached=radius*NorthlightShadowCascades::CacheScale;
    return std::ceil((std::sqrt(2*cached*cached+640.f*640.f)+240.f)/64.f)*64.f;
}
struct Coverage {Vec3 center;float radius=NorthlightShadowCascades::FarRadius;};
inline Coverage coverage(Vec3 pivot,float groundZ,float reach,float previous=NorthlightShadowCascades::FarRadius){
    Coverage out{pivot};
    const float altitude=pivot.z-groundZ;
    if(!std::isfinite(altitude)||altitude<=128)return out;
    out.center.z-=altitude-128;
    float wanted=std::clamp(std::ceil(altitude*1.25f/Step)*Step,NorthlightShadowCascades::FarRadius,MaximumRadius);
    // Keep a scale across small altitude changes; camera orbits never change it.
    if(wanted<previous&&altitude*1.25f>previous-Step*1.5f)wanted=previous;
    for(float radius=out.radius+Step;radius<=wanted&&terrainReach(radius)<=reach;radius+=Step)out.radius=radius;
    return out;
}
}
