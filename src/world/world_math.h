#pragma once
#include "world_gi.h"
#include "shadow_cascades.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
namespace NorthlightWorldMath {
// +/-640 units along the light (formerly +/-320). Tanaris mountain faces
// inside the loaded scene projected beyond the old near plane, producing a
// clipping boundary that travelled with the player. XY resolution is unchanged.
inline constexpr float ShadowDepthSpan=1280.f;
inline constexpr float InverseShadowDepth=1.f/ShadowDepthSpan;
inline constexpr float ShadowBiasWorld=.0512f;
// Light-space frame of a cascade: orthonormal basis, texel size and the
// texel-snapped XY origin plus the unsnapped depth origin along the light.
struct ShadowFrame {NorthlightGI::Vec3 x,y,z;float texel=0,cx=0,cy=0,cz=0;};
inline ShadowFrame shadowFrame(NorthlightGI::Vec3 center,NorthlightGI::Vec3 sun,float radius){
    using namespace NorthlightGI;ShadowFrame f;
    f.z=normalized(sun);f.x=normalized(cross(std::fabs(f.z.z)>.95f?Vec3(0,1,0):Vec3(0,0,1),f.z));f.y=cross(f.z,f.x);
    f.texel=2*radius/1024;f.cx=std::floor(dot(center,f.x)/f.texel)*f.texel;f.cy=std::floor(dot(center,f.y)/f.texel)*f.texel;f.cz=dot(center,f.z);
    return f;
}
// extent is the half-width covered by the map: radius for the 1024 working
// map, radius*cacheSize/1024 for a cached map with the same texel size.
inline void shadowMatrixFrom(const ShadowFrame& f,float extent,float* m){
    std::memset(m,0,64);m[0]=f.x.x/extent;m[4]=f.x.y/extent;m[8]=f.x.z/extent;m[12]=-f.cx/extent;
    m[1]=f.y.x/extent;m[5]=f.y.y/extent;m[9]=f.y.z/extent;m[13]=-f.cy/extent;
    m[2]=-f.z.x*InverseShadowDepth;m[6]=-f.z.y*InverseShadowDepth;m[10]=-f.z.z*InverseShadowDepth;m[14]=.5f+f.cz*InverseShadowDepth;m[15]=1;
}
inline void shadowMatrix(NorthlightGI::Vec3 center,NorthlightGI::Vec3 sun,float radius,float* m){
    shadowMatrixFrom(shadowFrame(center,sun,radius),radius,m);
}
// Integer texel offset and exact depth delta between the current working frame
// and the frame a static map was cached with (same direction, same texel size):
// working texel (t,r) maps to cached texel (t+margin+offX, r+margin-offY) and
// working depth = cached depth + dz. Both are exact because XY origins are
// multiples of the texel and the depth origin is linear in the center.
inline bool cacheOffset(const ShadowFrame& now,const ShadowFrame& cached,long& offX,long& offY,float& dz){
    if(!(now.texel>0)||std::fabs(now.texel-cached.texel)>1e-6f*now.texel)return false;
    double fx=double(now.cx-cached.cx)/now.texel,fy=double(now.cy-cached.cy)/now.texel;
    offX=std::lround(fx);offY=std::lround(fy);
    if(std::fabs(fx-double(offX))>1e-3||std::fabs(fy-double(offY))>1e-3)return false;
    dz=(now.cz-cached.cz)*InverseShadowDepth;return true;
}
struct ShadowCachePlacement {
    ShadowFrame frame;long offX=0,offY=0;float dz=0;
    const char* reason=nullptr;
};
// Content refreshes keep this coordinate frame while its margin is valid.
// Recentring on every streamed model publication would throw away prepared
// static draw plans, even though the existing frame still covers the view.
inline ShadowCachePlacement shadowCachePlacement(const ShadowFrame& now,const ShadowFrame& cached,
    bool valid,bool sameDirection,long margin){
    ShadowCachePlacement result;result.frame=now;
    if(!valid)result.reason="invalid";
    else if(!sameDirection)result.reason="direction";
    else if(!cacheOffset(now,cached,result.offX,result.offY,result.dz))result.reason="frame";
    else if(std::labs(result.offX)>margin||std::labs(result.offY)>margin)result.reason="travel";
    if(result.reason){result.offX=result.offY=0;result.dz=0;}
    else result.frame=cached;
    return result;
}
// Direction lattice for cached shadow maps: components rounded to 1/2048 then
// renormalized (worst-case change below 0.03 degrees), so a cache stays valid
// while a celestial body drifts within one lattice cell.
// `steps` (ShadowDirectionSteps) coarsens the lattice to re-render less often; 2048 is 0.3.136.
inline NorthlightGI::Vec3 quantizeDirection(NorthlightGI::Vec3 direction,float steps=2048){
    using namespace NorthlightGI;
    Vec3 q(std::round(direction.x*steps)/steps,std::round(direction.y*steps)/steps,std::round(direction.z*steps)/steps);
    if(dot(q,q)<1e-12f)return direction;
    return normalized(q);
}
inline void replayProjection(const float* inverseView,const float* light,float* rows){
    std::memset(rows,0,64);
    for(int row=0;row<4;++row)for(int col=0;col<4;++col)for(int k=0;k<4;++k)
        rows[row*4+col]+=inverseView[col*4+k]*light[k*4+row];
}
}
