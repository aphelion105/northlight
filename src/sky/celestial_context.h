#pragma once
// Read-only, fixed-build sky data. See celestial-context-proof.md for provenance.
#include "world_context.h"
#include "celestial_time_warp.h"
#include <algorithm>
#include <cstddef>

namespace NorthlightCelestial {
struct BodyRecord {
    float center[3];
    uint32_t color;       // D3DCOLOR used by the actual sky billboard.
    uint32_t texture;     // Opaque client handle: never dereferenced here.
    float size, baseSize, period;
};
static_assert(sizeof(BodyRecord)==32,"Client sky record layout");
struct Snapshot {
    float dayFraction=0, skyCamera[3]={};
    BodyRecord bodies[3]={}; // sunCenter.blp, moon.blp, moon02.blp, in that order.
};
struct Body {
    float direction[3]={}, tint[3]={}, alpha=0, altitudeVisibility=0;
    float emission=1.1f;
    float nativeAngularRadius=0;
    float angularRadius=0; // atan(size/24): native template half-size .5, orbit radius12.
};
struct Context {
    float dayFraction=0;
    Body sun, moon, secondaryMoon;
    float sunWeight=0, moonWeight=0;
    float sunColor[3]={}, moonColor[3]={}; // Same units as input terrain direct RGB.
    float horizonSine=0; // the camera's far edge (apparent_horizon.h), 0..-sin 3 deg; discs and the glow ring only
};
inline float smooth(float x) { x=std::max(0.f,std::min(1.f,x));return x*x*(3-2*x); }
inline bool decodeBody(const BodyRecord& r,const float* camera,Body& out) {
    Body candidate;float length2=0;
    for(int i=0;i<3;++i) {
        if(!std::isfinite(r.center[i])||!std::isfinite(camera[i]))return false;
        candidate.direction[i]=r.center[i]-camera[i];
        length2+=candidate.direction[i]*candidate.direction[i];
    }
    // All three original sky orbit producers explicitly normalize to radius 12.
    // Reject stale/mixed-frame or uninitialized records, not merely NaNs.
    if(length2<11.9f*11.9f||length2>12.1f*12.1f || !r.texture ||
       !std::isfinite(r.size)||r.size<0||r.size>12)return false;
    const float inv=1/std::sqrt(length2);
    for(int i=0;i<3;++i)candidate.direction[i]*=inv;
    candidate.tint[0]=float((r.color>>16)&255)/255;
    candidate.tint[1]=float((r.color>>8)&255)/255;
    candidate.tint[2]=float(r.color&255)/255;
    candidate.alpha=float(r.color>>24)/255;
    candidate.angularRadius=candidate.nativeAngularRadius=std::atan(r.size/24.f);
    // Renderer policy: C1 fade over the first 4.6 degrees ABOVE the horizon.
    // No below-horizon direct source, no frame history, no invented orbit.
    candidate.altitudeVisibility=smooth(candidate.direction[2]/.08f);
    out=candidate;return true;
}
inline bool decode(const Snapshot& s,const float* camera,const float* direct,Context& out) {
    if(!std::isfinite(s.dayFraction)||s.dayFraction<0||s.dayFraction>=1)return false;
    float cameraError2=0;
    for(int i=0;i<3;++i) {
        if(!std::isfinite(camera[i])||!std::isfinite(s.skyCamera[i])||
           !std::isfinite(direct[i])||direct[i]<0||direct[i]>8)return false;
        float delta=camera[i]-s.skyCamera[i];cameraError2+=delta*delta;
    }
    // Prevent an old sky snapshot after teleport/map change driving world light.
    if(cameraError2>40.f*40.f)return false; /* 0.3.200: several frames of travel tolerated (NorthlightWmoContext::SkyCameraTolerance) */
    Context candidate;candidate.dayFraction=s.dayFraction;
    if(!decodeBody(s.bodies[0],s.skyCamera,candidate.sun)||
       !decodeBody(s.bodies[1],s.skyCamera,candidate.moon)||
       !decodeBody(s.bodies[2],s.skyCamera,candidate.secondaryMoon))return false;
    // Artistic energy allocation, NOT a recovered Blizzard irradiance formula.
    // Original zone/time direct color is the budget; never brighten it twice.
    // Moon02 remains exposed separately; it gets no invented extra radiance.
    candidate.sunWeight=candidate.sun.altitudeVisibility*candidate.sun.alpha;
    candidate.moonWeight=(1-candidate.sunWeight)*candidate.moon.altitudeVisibility*candidate.moon.alpha;
    for(int i=0;i<3;++i) {
        candidate.sunColor[i]=direct[i]*candidate.sunWeight;
        candidate.moonColor[i]=direct[i]*candidate.moonWeight;
    }
    out=candidate;return true;
}

// The light passes from the sun to the moon where they cross under the horizon (about -9 deg at 20:41) and back
// at dawn (06:00), crossfaded over this much elevation difference (sine, 1.5 deg: some 20 s at the crossing). Both
// share one azimuth and light from 6 deg at the lowest, so the shadows do not turn at the handover.
inline constexpr float HandoverWidth=.0262f;
// A sun 4.6 deg up keeps the day's light whatever the moon does (the orbit never has both up).
inline float sunShare(float sunZ,float moonZ){
    if(!std::isfinite(sunZ)||!std::isfinite(moonZ))return 0;
    return std::max(smooth((sunZ-moonZ)/HandoverWidth+.5f),smooth(sunZ/.08f));
}
// Lowest elevation (sine, 6 deg) of a light direction: a grazing light would stretch shadows to infinity. At 2 deg
// the near shadow map reached so far toward the light that a moving camera pulled 150-260 model replays into it
// (4-5 ms GPU on DXVK) instead of about 30; 6 deg shadows are a third as long.
inline constexpr float MinLightElevation=.1045f;
inline void clampLightElevation(float* direction){
    if(!(direction[2]<MinLightElevation))return;
    const float h=std::sqrt(direction[0]*direction[0]+direction[1]*direction[1]),k=h>0?std::sqrt(1-MinLightElevation*MinLightElevation)/h:0.f;
    direction[0]*=k;direction[1]*=k;direction[2]=MinLightElevation;
}
// Extension appearance/energy policy, applied AFTER clock-based orbit evaluation. Native
// billboard RGBA is zone sky artwork state, not celestial irradiance: the
// midnight captures have moon z=.81 but alpha 0, 14/255 or 1 by location.
// A raw-alpha change must not switch off the disc, shadows or fog lighting.
// Keep decode() faithful to the audited records for diagnostics/native use.
inline void applyRendererPolicy(Context& c,const float* direct){
    c.sun.alpha=c.moon.alpha=1;
    c.sun.emission=3.5f;c.moon.emission=1.1f;
    const float tint[2][3]={{1,.96f,.88f},{.94f,.97f,1}};
    for(unsigned i=0;i<3;++i){c.sun.tint[i]=tint[0][i];c.moon.tint[i]=tint[1][i];}
    // Whichever body is higher lights the world (rays, shadows), handed over where they cross under the horizon. The
    // sun's light ended one radius under the horizon: seen from high up past the far edge, it went out while still in
    // view, and the moon lit nothing until 4.6 deg up.
    c.sun.altitudeVisibility=sunShare(c.sun.direction[2],c.moon.direction[2]);
    c.moon.altitudeVisibility=1-c.sun.altitudeVisibility;
    c.sunWeight=c.sun.altitudeVisibility;
    c.moonWeight=c.moon.altitudeVisibility;
    for(unsigned i=0;i<3;++i){
        c.sunColor[i]=direct[i]*c.sunWeight;
        c.moonColor[i]=direct[i]*c.moonWeight;
    }
}
inline NorthlightCelestialOrbit::Result resolveRendererSky(Context& c,const float* direct){
    const auto orbit=NorthlightCelestialOrbit::evaluate(c.dayFraction);
    if(!orbit.valid)return orbit;
    std::memcpy(c.sun.direction,orbit.sun.direction,sizeof c.sun.direction);
    std::memcpy(c.moon.direction,orbit.moon.direction,sizeof c.moon.direction);
    // Fixed native base sizes: sun 1, moon 1.75 (client initialization at
    // 0x7f296c / 0x7f2989). Native r.size multiplies these by a time curve,
    // enlarging the bodies near the horizon. Keep that raw radius only for
    // diagnostics; the owned orbit uses constant sizes with our latest scales.
    // Repeated resolves, native size animation and zone artwork cannot resize it.
    c.sun.angularRadius=std::atan(.506736f*1.f/24.f); /* 0.3.145: -10% */
    c.moon.angularRadius=std::atan(.46176318f*1.75f/24.f); /* 0.3.145: -10% */
    applyRendererPolicy(c,direct);return orbit;
}

struct CodeSignature {uintptr_t address;const unsigned char* bytes;size_t size;};
inline const CodeSignature* signatures(size_t& count) {
    static const unsigned char skyGetter[]={0xb8,0x00,0x8b,0xd3,0x00,0xc3};
    static const unsigned char sunInit[]={0x68,0x90,0x1b,0xa4,0x00,0xb9,0x28,0x8e,0xd3,0x00,0xe8,0x44,0xa7,0x1b,0x00};
    static const unsigned char moonInit[]={0x68,0x7c,0x1b,0xa4,0x00,0xb9,0x48,0x8e,0xd3,0x00,0xd9,0x1d,0x44,0x8e,0xd3,0x00,0xe8,0x27,0xa7,0x1b,0x00};
    static const unsigned char moon2Init[]={0x68,0x68,0x1b,0xa4,0x00,0xd9,0xe8,0xb9,0x68,0x8e,0xd3,0x00,0xd9,0x1d,0x64,0x8e,0xd3,0x00,0xe8,0x04,0xa7,0x1b,0x00};
    static const unsigned char sunPosition[]={0xd9,0x05,0x18,0x8b,0xd3,0x00,0xde,0xc3,0xd9,0xca,0xd9,0x1d,0x28,0x8e,0xd3,0x00};
    static const unsigned char moonPosition[]={0xd8,0x05,0x18,0x8b,0xd3,0x00,0xd9,0x1d,0x48,0x8e,0xd3,0x00};
    static const unsigned char moon2Position[]={0xd8,0x05,0x18,0x8b,0xd3,0x00,0xd9,0x1d,0x68,0x8e,0xd3,0x00};
    static const unsigned char renderTime[]={0xd9,0x05,0x04,0x8b,0xd3,0x00,0x51,0xbe,0x05,0x00,0x00,0x00};
    static const CodeSignature table[]={
        {0x7ecef0,skyGetter,sizeof skyGetter},{0x7f295d,sunInit,sizeof sunInit},
        {0x7f2974,moonInit,sizeof moonInit},{0x7f2995,moon2Init,sizeof moon2Init},
        {0x7ef161,sunPosition,sizeof sunPosition},{0x7ef337,moonPosition,sizeof moonPosition},
        {0x7ef5b6,moon2Position,sizeof moon2Position},{0x7eefee,renderTime,sizeof renderTime}};
    count=sizeof table/sizeof table[0];return table;
}
template<class Read> inline bool verifyCode(Read read) {
    size_t count=0;const auto* table=signatures(count);
    unsigned char actual[32];
    for(size_t i=0;i<count;++i)
        if(!read(table[i].address,actual,table[i].size)||
           std::memcmp(actual,table[i].bytes,table[i].size))return false;
    return true;
}
template<class Read> inline bool readSnapshot(Read read,Snapshot& out) {
    // Includes the render clock, sky camera and all three adjacent body records.
    // Two identical copies reject concurrent updates. Call on the render thread (0.3.192 CS: with the command stream on,
    // the replay thread reads this from the game thread's snapshot, which holds the same two copies in order).
    unsigned char a[0x388],b[sizeof a];
    if(!read(0xd38b00,a,sizeof a)||!read(0xd38b00,b,sizeof b)||std::memcmp(a,b,sizeof a))return false;
    Snapshot s;std::memcpy(&s.dayFraction,a+4,4);
    std::memcpy(s.skyCamera,a+0x18,sizeof s.skyCamera);
    std::memcpy(s.bodies,a+0x328,sizeof s.bodies);out=s;return true;
}
#ifdef _WIN32
inline bool read(const float* camera,const float* direct,Context& out) {
    static const bool supported=NorthlightWorldContext::supportedClient()&&verifyCode(NorthlightWorldContext::readSelf);
    Snapshot snapshot;
    return supported&&readSnapshot(NorthlightWorldContext::readSelf,snapshot)&&decode(snapshot,camera,direct,out);
}
#endif
} // namespace NorthlightCelestial
