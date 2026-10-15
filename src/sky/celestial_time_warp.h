#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

// Renderer-owned orbit, driven only by the validated map render clock.
// The solar keys reproduce this client's native polar-angle table (0x7eecc0,
// linear interpolation at 0x7ed3b0). No accelerated horizon segments or use of
// the native billboard position/alpha. See native-orbit-validation-0.3.107.json.
namespace NorthlightCelestialOrbit {
inline constexpr double kPi=3.14159265358979323846;
inline constexpr double kDaySeconds=86400;
// These are the exact float constants recovered from the supported executable.
inline constexpr double kNativeSunStartDay=0.2291666716337204; // 05:30
inline constexpr double kNativeSunCrestBeginDay=0.4965277910232544; // 11:55
inline constexpr double kNativeSunCrestEndDay=0.5034722089767456; // 12:05
inline constexpr double kNativeSunEndDay=0.8958333134651184; // 21:30
inline constexpr double kNativeSunLowPolar=1.7453292608261108; // 100 degrees
inline constexpr double kNativeSunHighPolar=0.0872664675116539; // 5 degrees
inline constexpr double kHorizonFraction=(kNativeSunLowPolar-kPi/2)/(kNativeSunLowPolar-kNativeSunHighPolar);
inline constexpr double kSunriseSeconds=(kNativeSunStartDay+(kNativeSunCrestBeginDay-kNativeSunStartDay)*kHorizonFraction)*kDaySeconds;
inline constexpr double kSunsetSeconds=(kNativeSunEndDay-(kNativeSunEndDay-kNativeSunCrestEndDay)*kHorizonFraction)*kDaySeconds;
inline constexpr double kSunPeakSeconds=12*3600;
inline constexpr double kSunPeakDegrees=90-kNativeSunHighPolar*180/kPi;
inline constexpr double kMoonPeakDegrees=43;
inline constexpr double kMoonriseSeconds=20*3600+50*60; // 20:50, not at sunset (20:30): the moon rose where the sun set and covered it
inline constexpr double kMoonsetSeconds=kSunriseSeconds;
inline constexpr double kMoonPeakSeconds=24*3600;
inline constexpr double kAzimuthRadians=kPi/4;
struct Body {double elevation=0;float direction[3]={};};
struct Result {Body sun,moon;bool valid=false;};
inline double elevationDegrees(double z){return std::asin(std::clamp(z,-1.,1.))*180/kPi;}
inline double solarElevation(double day){
    double polar=kNativeSunLowPolar;
    if(day>=kNativeSunStartDay&&day<=kNativeSunEndDay){
        if(day<kNativeSunCrestBeginDay){const double u=(day-kNativeSunStartDay)/(kNativeSunCrestBeginDay-kNativeSunStartDay);polar+=(kNativeSunHighPolar-kNativeSunLowPolar)*u;}
        else if(day<=kNativeSunCrestEndDay)polar=kNativeSunHighPolar;
        else{const double u=(day-kNativeSunCrestEndDay)/(kNativeSunEndDay-kNativeSunCrestEndDay);polar=kNativeSunHighPolar+(kNativeSunLowPolar-kNativeSunHighPolar)*u;}
    }
    return 90-polar*180/kPi;
}
// The renderer-owned moon keeps one stable nocturnal arc and the existing
// 43-degree crest at 00:00, rather than the native moon's independently cycling
// orbit. 0.3.177: four quarter-sines with a day-side hold; continuous position
// and velocity; horizon rates 19.4 (rise) and 10.9 (set) degrees/h. The former
// cosine eases stalled near the horizon (the moon took 44 min after sunset to
// reach full light and dimmed 78 min before sunrise). The day side mirrors the
// set and rise quarters, so the velocity matches across both horizons; it
// holds at -43 between them. Velocity is zero only at the crest and the hold ends.
inline double lunarElevation(double seconds){
    double t=seconds;if(t<kMoonriseSeconds)t+=kDaySeconds;
    const double rise=kMoonPeakSeconds-kMoonriseSeconds,set=kDaySeconds+kMoonsetSeconds,fall=set-kMoonPeakSeconds;
    const double quarter=kPi/2,climb=kMoonriseSeconds+kDaySeconds-rise;
    if(t<=kMoonPeakSeconds)return kMoonPeakDegrees*std::sin(quarter*(t-kMoonriseSeconds)/rise);
    if(t<=set)return kMoonPeakDegrees*std::cos(quarter*(t-kMoonPeakSeconds)/fall);
    if(t<=set+fall)return -kMoonPeakDegrees*std::sin(quarter*(t-set)/fall);
    if(t<climb)return -kMoonPeakDegrees;
    return -kMoonPeakDegrees*std::cos(quarter*(t-climb)/rise);
}
inline Body body(double elevation){
    Body b;b.elevation=elevation;
    const double e=elevation*kPi/180,h=std::cos(e);
    b.direction[0]=float(h*std::cos(kAzimuthRadians));
    b.direction[1]=float(h*std::sin(kAzimuthRadians));
    b.direction[2]=float(std::sin(e));return b;
}
inline Result evaluate(double dayFraction){
    Result r;
    if(!std::isfinite(dayFraction)||dayFraction<0||dayFraction>=1)return r;
    const double seconds=dayFraction*kDaySeconds;
    r.sun=body(solarElevation(dayFraction));
    r.moon=body(lunarElevation(seconds));
    r.valid=true;return r;
}
// World-light-only low-pass: discs keep exact clock positions, while light,
// shadows and volume rays share one gently changing direction and altitude
// fade. 0.4 real seconds, frame-rate independent; no long orbit warm-up.
// Reset after clock jumps, map/camera discontinuities or a >2 s render gap.
struct LightMotion {
    bool ready=false;double day=0,sun=0,moon=0;uint32_t tick=0;
    void reset(){ready=false;}
    Result update(const Result& target,double dayFraction,uint32_t now,bool discontinuity=false){
        if(!target.valid){reset();return target;}
        double delta=dayFraction-day;delta-=std::round(delta);
        const uint32_t elapsed=now-tick;
        if(!ready||discontinuity||elapsed>2000||std::fabs(delta)*kDaySeconds>60){
            sun=target.sun.elevation;moon=target.moon.elevation;ready=true;
        }else{
            const double a=-std::expm1(-double(elapsed)/400.);
            sun+=(target.sun.elevation-sun)*a;moon+=(target.moon.elevation-moon)*a;
        }
        day=dayFraction;tick=now;
        Result r;r.sun=body(sun);r.moon=body(moon);r.valid=true;return r;
    }
};
} // namespace NorthlightCelestialOrbit
