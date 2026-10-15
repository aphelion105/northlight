#pragma once
#include <cmath>
#include <initializer_list>
// Perspective projection alone does not identify a world caster: sky models
// reuse the model shaders/projection but render in a separate depth interval.
namespace NorthlightWorldDrawDomain {
// The client never sets ZENABLE false (device default TRUE): its "depth test off" is ZFUNC = D3DCMP_ALWAYS (8). A draw depth-tests when ZENABLE is on and ZFUNC is not ALWAYS.
constexpr unsigned long kCmpAlways=8;
inline bool depthTested(unsigned long zEnable,unsigned long zFunc){return zEnable!=0&&zFunc!=kCmpAlways;}
// Client viewport Z bands (CWorld 0.94, World.cpp horizon, DayNight s_sky*): world 0..0.94, horizon .998046875..0.9990234375, sky and glare 0.9990234375..1; UI/FFX/RT passes use 0..1 (Other).
enum class Band{World,Horizon,Sky,Other};
constexpr float kWorldMaxZ=0.94f,kHorizonMinZ=0.998046875f,kHorizonMaxZ=0.9990234375f,kSkyMinZ=0.9990234375f,kSkyMaxZ=1.f,kBandEpsilon=1e-4f;
// Horizon/sky bands are only derived when the learned world band is the client's 0..0.94; any other learned band (unusual client, RT) classifies everything but an exact world match as Other.
inline Band classify(float worldMin,float worldMax,float drawMin,float drawMax){
    for(float value:{worldMin,worldMax,drawMin,drawMax})if(!std::isfinite(value))return Band::Other;
    auto within=[](float a,float b){return std::fabs(a-b)<=kBandEpsilon;};
    if(!within(worldMin,0.f)||!within(worldMax,kWorldMaxZ))return Band::Other;
    if(within(drawMin,worldMin)&&within(drawMax,worldMax))return Band::World;
    if(within(drawMin,kHorizonMinZ)&&within(drawMax,kHorizonMaxZ))return Band::Horizon;
    if(within(drawMin,kSkyMinZ)&&within(drawMax,kSkyMaxZ))return Band::Sky;
    return Band::Other;
}
// Sun/moon/glare pre-filter: with a client-shaped world band only a sky-band draw can be one; on an unusual client the answer is "maybe" so the identity test stays the only gate.
inline bool skyClaimPossible(float worldMin,float worldMax,float drawMin,float drawMax){
    const Band b=classify(worldMin,worldMax,drawMin,drawMax);
    if(b==Band::Sky)return true;
    return !(std::isfinite(worldMin)&&std::isfinite(worldMax)&&std::fabs(worldMin)<=kBandEpsilon&&std::fabs(worldMax-kWorldMaxZ)<=kBandEpsilon);
}
inline bool accepts(bool projectionValid,bool depthEnabled,float worldMin,float worldMax,float drawMin,float drawMax){
    if(!projectionValid||!depthEnabled)return false;
    for(float value:{worldMin,worldMax,drawMin,drawMax})if(!std::isfinite(value))return false;
    if(worldMin<0||worldMax>1||worldMax<=worldMin||drawMin<0||drawMax>1||drawMax<=drawMin)return false;
    return std::fabs(drawMin-worldMin)<=1e-6f&&std::fabs(drawMax-worldMax)<=1e-6f;
}
}
