#pragma once
#include "celestial_time_warp.h"
#include "forever_celestial_tables.h"

namespace NorthlightForeverSky {
// Periodic linear interpolation, including keys that straddle midnight.
template<size_t N> inline float sample(const Key (&keys)[N],double day){
    size_t next=0;while(next<N&&day>=keys[next].day)++next;
    const size_t prev=next?next-1:N-1;if(next==N)next=0;
    double a=keys[prev].day,b=keys[next].day;if(a>day)a-=1;if(b<=a)b+=1;if(b<day)b+=1;
    const float w=b>a?float(std::clamp((day-a)/(b-a),0.,1.)):0;
    return keys[prev].value+(keys[next].value-keys[prev].value)*w;
}
inline NorthlightCelestialOrbit::Body body(float polar,float azimuth){
    NorthlightCelestialOrbit::Body b;b.elevation=90-polar*180/NorthlightCelestialOrbit::kPi;
    b.direction[0]=std::sin(polar)*std::cos(azimuth);b.direction[1]=std::sin(polar)*std::sin(azimuth);b.direction[2]=std::cos(polar);return b;
}
inline NorthlightCelestialOrbit::Result evaluate(double day){
    NorthlightCelestialOrbit::Result r;if(!std::isfinite(day)||day<0||day>=1)return r;
    r.sun=body(sample(SunPolar,day),sample(SunAzimuth,day));
    r.moon=body(sample(MoonPolar,day),sample(MoonAzimuth,day));r.valid=true;return r;
}
inline NorthlightCelestialOrbit::Result evaluateSelected(double day,bool forever){
    return forever?evaluate(day):NorthlightCelestialOrbit::evaluateNative(day);
}
// Apply the existing lunar linear-size calibration to Outland's authored
// planet system. Keep its relative sizes and ring/body proportions; no lunar orbit.
inline float planetScale(){return float(std::tan(NorthlightCelestialOrbit::kMoonAngularRadius)/(1.75/24.));}
// Keep the solar calibration; use the client's 1.75 lunar base without the
// historical 0.46176318 reduction. Absolute Forever sizes were not recovered.
// The table scales linear size, not angular radius.
inline float radius(float baseRadius,float scale){return std::atan(std::tan(baseRadius)*scale);}
inline float sunRadius(double day){return radius(float(NorthlightCelestialOrbit::kSunAngularRadius),sample(SunScale,day));}
inline float moonRadius(double day){return std::atan((1.75f/24.f)*sample(MoonScale,day));}
}
