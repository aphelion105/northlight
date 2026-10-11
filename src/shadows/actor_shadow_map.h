#pragma once
#include "world_math.h"
#include <cmath>

// 0.3.208 (task 29): the actor shadow map. A 1024 map of Radius yards around the player per light source
// (texel 24/1024 yd, four times finer than the near cascade) that holds only the moving casters (replays); the
// near cascade skips the replays that lie wholly inside its Inset. Orthographic along the cascades' light
// direction with the cascades' depth rows, but centred on the raw pivot: no texel snap, so the player's own raster
// does not step while the player moves (the map moves with the player). The centre is a leashed anchor (leash()).
namespace NorthlightActorShadowMap {
inline constexpr float Radius=12.f;
inline constexpr float Inset=.80f;
inline constexpr unsigned Size=1024;
inline constexpr unsigned TileTexels=16;   // actor texels per tile side (world_effects.hlsl ActorTileTexels): a 64x64 tile minimum
inline constexpr unsigned TileStep=4;      // ActorTiles reduces 4x4 per step: Size -> Size/4 (256) -> Size/16 (64)
static_assert(TileStep*TileStep==TileTexels,"two ActorTiles steps make one tile");
inline constexpr float Leash=1.5f;         // yards (light-space XY) the map centre may lag the pivot
inline constexpr float JumpDistance=40.f;  // a pivot this far (light-space XY) from the centre re-centres at once (teleport, map change)
// Leashed centre. The pivot is camera-derived (eye + forward*distance), so it drifts while the camera orbits an idle player: within
// Leash the centre stays put (the map does not slide under the character); beyond it the centre is dragged so that the light-space XY
// distance is exactly Leash (it moves with a walking or riding player). Within Leash the stored centre is returned bit for bit (a
// recomputed one would random-walk by float roundoff at world coordinates); the depth origin comes from the pivot (matrix()).
// Only light-space XY places the map, so the jump test is XY too (a 3D one re-centred, a pop of up to Leash, every JumpDistance of
// flight under a high sun or of a run toward a low one); the centre's depth follows the pivot on every drag and whenever it lags by
// more than Leash, so a stale depth offset cannot rotate into XY as the light direction steps.
inline NorthlightGI::Vec3 leash(NorthlightGI::Vec3 center,bool valid,NorthlightGI::Vec3 pivot,NorthlightGI::Vec3 sun){
    using namespace NorthlightGI;
    const Vec3 delta=pivot-center;
    if(!valid||!std::isfinite(dot(delta,delta)))return pivot;
    const NorthlightWorldMath::ShadowFrame f=NorthlightWorldMath::shadowFrame(pivot,sun,Radius);
    const float dx=dot(delta,f.x),dy=dot(delta,f.y),dz=dot(delta,f.z),distance=std::sqrt(dx*dx+dy*dy);
    if(!(distance<=JumpDistance))return pivot;
    if(!(distance>Leash))return std::fabs(dz)>Leash?center+f.z*dz:center;
    const float pull=1.f-Leash/distance;
    return center+f.x*(dx*pull)+f.y*(dy*pull)+f.z*dz;
}
// XY origin from the (leashed) centre, unsnapped; depth origin from the pivot, as the cascades' (cz is never snapped).
inline NorthlightWorldMath::ShadowFrame frame(NorthlightGI::Vec3 center,NorthlightGI::Vec3 pivot,NorthlightGI::Vec3 sun){
    using namespace NorthlightGI;
    NorthlightWorldMath::ShadowFrame f=NorthlightWorldMath::shadowFrame(pivot,sun,Radius);
    f.cx=dot(center,f.x);f.cy=dot(center,f.y);
    return f;
}
inline void matrix(NorthlightGI::Vec3 center,NorthlightGI::Vec3 pivot,NorthlightGI::Vec3 sun,float* m){
    NorthlightWorldMath::shadowMatrixFrom(frame(center,pivot,sun),Radius,m);
}
inline void matrix(NorthlightGI::Vec3 pivot,NorthlightGI::Vec3 sun,float* m){matrix(pivot,pivot,sun,m);}
// A usable actor map matrix: every element finite. Checked once per frame where the matrix is built (not per replay in insideInset).
inline bool finiteMatrix(const float* m){
    if(!m)return false;
    for(unsigned i=0;i<16;++i)if(!std::isfinite(m[i]))return false;
    return true;
}
// True when the box is valid and all 8 corners project into |x|,|y|<=Inset and 0<z<1 of the actor map (row-vector matrix, which the
// caller has checked with finiteMatrix).
inline bool insideInset(bool valid,const float* low,const float* high,const float* m){
    if(!valid||!low||!high||!m)return false;
    for(unsigned axis=0;axis<3;++axis)if(!std::isfinite(low[axis])||!std::isfinite(high[axis])||low[axis]>high[axis])return false;
    for(unsigned corner=0;corner<8;++corner){
        const float p[3]={corner&1?high[0]:low[0],corner&2?high[1]:low[1],corner&4?high[2]:low[2]};
        float c[3];
        for(unsigned axis=0;axis<3;++axis)c[axis]=p[0]*m[axis]+p[1]*m[4+axis]+p[2]*m[8+axis]+m[12+axis];
        if(!(std::fabs(c[0])<=Inset)||!(std::fabs(c[1])<=Inset)||!(c[2]>0)||!(c[2]<1))return false;
    }
    return true;
}
}
