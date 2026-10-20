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
inline constexpr double kMoonsetSeconds=5*3600+45*60; // 05:45, not at sunrise: the moon set where the sun rose and crossed it
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
// The native moon (DayNight::SetPlanets, s_moonTheta, interpolated as DayNight::InterpTable): the polar angle from
// the zenith, 35 deg (55 up) from 23:55 to 00:05, down to 100 deg (10 under, parked) at 04:00, up from there at 22:00.
// It is up from about 22:17 to 03:24. Since PR 77 the discs, light and shadows follow the native billboards.
inline double nativeLunarElevation(double day){
    struct Key{double day,polar;};
    static constexpr Key keys[5]={{0.,.61086524},{.0034722222,.61086524},{.16666667,1.7453293},{.91666669,1.7453293},{.99652779,.61086524}};
    const double t=std::clamp(day,0.,1.);
    unsigned index=0;while(index<5&&t>keys[index].day)++index;
    unsigned prev;if(index==5){index=0;prev=4;}else prev=index?index-1:4;
    double range=keys[index].day-keys[prev].day;
    if(std::fabs(range)<.001)return 90-keys[prev].polar*180/kPi;
    if(range<0)range+=1;
    double offset=t-keys[prev].day;if(offset<0)offset+=1;
    return 90-(keys[prev].polar+(keys[index].polar-keys[prev].polar)*offset/range)*180/kPi;
}
// The renderer's own smooth orbit, no longer drawn or lit since PR 77 (kept with its tests).
inline Result evaluateNatural(double dayFraction){
    Result r;
    if(!std::isfinite(dayFraction)||dayFraction<0||dayFraction>=1)return r;
    const double seconds=dayFraction*kDaySeconds;
    r.sun=body(solarElevation(dayFraction));
    r.moon=body(lunarElevation(seconds));
    r.valid=true;return r;
}
// The native orbit (the sun's keys above are the native table too): the ambient policies (night floor, twilight
// fill) and the static shadow prewarm, so they run on the sun and moon that are shown and light the world.
inline Result evaluateNative(double dayFraction){
    Result r;
    if(!std::isfinite(dayFraction)||dayFraction<0||dayFraction>=1)return r;
    r.sun=body(solarElevation(dayFraction));
    r.moon=body(nativeLunarElevation(dayFraction));
    r.valid=true;return r;
}
// The displayed orbit (discs, light, shadows). Sun and moon share one azimuth, so they pass each other between sunset
// (20:30) and moonrise (20:50) and between moonset (05:45) and sunrise (06:10). Each body keeps its own rate until its
// disc is under the horizon (centre one radius down); then it dives on a C1 cubic to a DiveDepth hold, and rises from
// it the same way, so the two pass about 9 degrees under the horizon, below where any disc shows. The radii are the
// renderer's disc sizes (resolveRendererSky).
inline constexpr double kDiveDepth=10,kEveningDiveSeconds=270,kMorningDiveSeconds=420;
inline const double kSunRadiusDegrees=std::atan(.506736/24.)*180/kPi,kMoonRadiusDegrees=std::atan(.46176318*1.75/24.)*180/kPi;
inline double hermite(double u,double p0,double m0,double p1,double m1){
    const double u2=u*u,u3=u2*u;return (2*u3-3*u2+1)*p0+(u3-2*u2+u)*m0+(3*u2-2*u3)*p1+(u3-u2)*m1;
}
struct Dives {double sunSet,sunRise,moonRise,moonSet,sunSetRate,sunRiseRate,moonRiseRate,moonSetRate;};
inline const Dives& dives(){
    static const Dives d=[]{
        auto sun=[](double t){return solarElevation(t/kDaySeconds);};
        auto moon=[](double t){return lunarElevation(std::fmod(t+kDaySeconds,kDaySeconds));};
        // Where a monotonic track passes `target` in [a,b].
        auto at=[](auto f,double a,double b,double target){const bool up=f(b)>f(a);
            for(int i=0;i<64;++i){const double m=(a+b)*.5;((f(m)<target)==up?a:b)=m;}return (a+b)*.5;};
        auto rate=[](auto f,double t){return std::fabs(f(t+.5)-f(t-.5));};
        Dives r;
        r.sunSet=at(sun,kSunsetSeconds,kSunsetSeconds+3600,-kSunRadiusDegrees);
        r.sunRise=at(sun,kSunriseSeconds-3600,kSunriseSeconds,-kSunRadiusDegrees);
        r.moonRise=at(moon,kMoonriseSeconds-3600,kMoonriseSeconds,-kMoonRadiusDegrees);
        r.moonSet=at(moon,kMoonsetSeconds,kMoonsetSeconds+3600,-kMoonRadiusDegrees);
        r.sunSetRate=rate(sun,r.sunSet);r.sunRiseRate=rate(sun,r.sunRise);
        r.moonRiseRate=rate(moon,r.moonRise);r.moonSetRate=rate(moon,r.moonSet);
        return r;
    }();
    return d;
}
// set..rise: the body's under-horizon stretch, wrapping midnight. Outside it the natural track stands.
inline double dived(double t,double natural,double set,double setRate,double setSeconds,double rise,double riseRate,double riseSeconds,double radius){
    const double sinceSet=std::fmod(t-set+2*kDaySeconds,kDaySeconds),untilRise=std::fmod(rise-t+2*kDaySeconds,kDaySeconds);
    if(sinceSet+untilRise>std::fmod(rise-set+2*kDaySeconds,kDaySeconds)+1e-6)return natural;
    if(sinceSet<setSeconds)return hermite(sinceSet/setSeconds,-radius,-setRate*setSeconds,-kDiveDepth,0);
    if(untilRise<riseSeconds)return hermite(1-untilRise/riseSeconds,-kDiveDepth,0,-radius,riseRate*riseSeconds);
    return -kDiveDepth;
}
inline Result evaluate(double dayFraction){
    Result r=evaluateNatural(dayFraction);
    if(!r.valid)return r;
    const double t=dayFraction*kDaySeconds;const Dives& d=dives();
    r.sun=body(dived(t,r.sun.elevation,d.sunSet,d.sunSetRate,kEveningDiveSeconds,d.sunRise,d.sunRiseRate,kMorningDiveSeconds,kSunRadiusDegrees));
    r.moon=body(dived(t,r.moon.elevation,d.moonSet,d.moonSetRate,kMorningDiveSeconds,d.moonRise,d.moonRiseRate,kEveningDiveSeconds,kMoonRadiusDegrees));
    return r;
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
