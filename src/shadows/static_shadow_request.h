#pragma once
#include "static_shadow_scene.h"
#include "celestial_time_warp.h"

namespace StaticShadow {
// Horizon casters are warmed this long before the sun or moon rises (was 300 s: needless shadow work).
inline constexpr double PrewarmSeconds=20;
// Request policy only. Never changes the actual celestial orbit, irradiance,
// shadow matrices or GI. An approaching source warms horizon casters before
// becoming visible; the live light keeps its existing smooth motion.
inline Request makeRequest(const std::string& map,Vec3 pivot,const Vec3* directions,
                           const bool* lit,bool orbitValid,double day,bool allowLoads){
    Request r;r.map=map;r.center=pivot;r.allowLoads=allowLoads;
    const auto current=NorthlightCelestialOrbit::evaluateNative(day);
    double nextDay=day+PrewarmSeconds/NorthlightCelestialOrbit::kDaySeconds;
    nextDay-=std::floor(nextDay);
    const auto future=NorthlightCelestialOrbit::evaluateNative(nextDay);
    for(unsigned source=0;source<2;++source){
        r.directions[source]=directions[source];r.active[source]=lit[source];
        if(lit[source]||!orbitValid||!current.valid||!future.valid)continue;
        const auto& now=source?current.moon:current.sun;
        const auto& then=source?future.moon:future.sun;
        if(now.elevation<=0&&then.elevation>0){
            const auto horizon=NorthlightCelestialOrbit::body(0);
            r.directions[source]=Vec3(horizon.direction[0],horizon.direction[1],horizon.direction[2]);
            r.active[source]=true;
        }
    }
    return r;
}
}
