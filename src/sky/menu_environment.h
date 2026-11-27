#pragma once
#include "menu_scene.h"
#include "forever_atmosphere.h"
#include "celestial_context.h"
#include "menu_weather.h"

namespace NorthlightMenu {
struct Environment {
    NorthlightForever::Frame atmosphere;
    NorthlightCelestial::Context celestial;
    NorthlightWeather::State weather;
};
inline Environment environment(const Snapshot& scene,const float* ambient,const float* direct){
    char name[sizeof scene.name]{};
    for(unsigned i=0;i<sizeof name-1&&scene.name[i];++i)name[i]=scene.name[i]>='A'&&scene.name[i]<='Z'?scene.name[i]+('a'-'A'):scene.name[i];
    const bool night=std::strstr(name,"scourge")||std::strstr(name,"nightelf")||std::strstr(name,"deathknight");
    Environment out;auto& c=out.celestial;auto& f=out.atmosphere;
    out.weather=backdropWeather(name);
    const bool snow=out.weather.kind==NorthlightWeather::Kind::Snow;
    // Menu backdrops have an authored mood, not a server clock. Changing a
    // character cannot inherit the last world's hour, weather or underwater state.
    c.dayFraction=night?.02f:.55f;c.sun.nativeAngularRadius=.025f;c.moon.nativeAngularRadius=.035f;
    const auto orbit=NorthlightForeverSky::evaluate(c.dayFraction);
    std::copy_n(orbit.sun.direction,3,c.sun.direction);std::copy_n(orbit.moon.direction,3,c.moon.direction);
    c.sun.alpha=c.moon.alpha=1;c.sun.altitudeVisibility=c.moon.altitudeVisibility=1;
    f.valid=f.skyValid=true;std::copy_n(ambient,3,f.ambient);std::copy_n(direct,3,f.direct);
    const float dayHue[3]={.48f,.64f,.82f},nightHue[3]={.07f,.15f,.23f},snowHue[3]={.57f,.67f,.78f};
    const float* hue=night?nightHue:snow?snowHue:dayHue;
    for(unsigned i=0;i<8;++i)for(unsigned k=0;k<3;++k){
        const float gradient=i==0?.42f:i==1?.65f:i<6?1.f:1.2f;
        f.sky.color[i][k]=hue[k]*gradient;
    }
    for(unsigned i=0;i<3;++i){auto& l=f.layer[i];
        l.flag[0]=l.flag[1]=l.flag[3]=1;l.flag[2]=i==1?1.f:0.f;
        l.p[0]=i==0?4.f:0.f;l.p[1]=i==0?(night?.035f:.018f):i==1?.035f:0.f;
        l.p[2]=1;l.p[3]=i==0?.18f:2.f;l.p[5]=i==0?100.f:6.f;
        l.p[6]=6;l.p[7]=-4;l.p[9]=1.6f;l.p[10]=.65f;l.p[19]=1;
        l.p[11]=.015f;l.p[12]=.008f;l.p[14]=-.006f;l.p[15]=.012f;
        l.p[20]=.8f;l.p[21]=.23f;l.p[22]=.8f;l.p[23]=.2f;
        for(unsigned k=0;k<3;++k){l.color[0][k]=l.color[3][k]=hue[k];l.color[1][k]=hue[k]*.16f;l.color[2][k]=hue[k]*.07f;}
    }
    return out;
}
}
