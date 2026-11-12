#include "static_shadow_request.h"
#include <cassert>
#include <cstring>
#include <cstdio>
int main(){
    using namespace StaticShadow;
    const Vec3 directions[2]={{.6f,.6f,.52915f},{.7f,.7f,-.14142f}};
    const bool daylit[2]={true,false},dark[2]={false,false};
    auto day=[](unsigned h,unsigned m){return (h*3600.+m*60.)/86400.;};
    auto normal=makeRequest("Azeroth",{1,2,3},directions,daylit,true,day(12,0),true);
    assert(normal.active[0]&&!normal.active[1]);
    assert(std::memcmp(&normal.directions[0],&directions[0],sizeof(Vec3))==0);
    // Within the 20 s prewarm before the native moonrise (22:17:41.5); a minute before and 20:00 below stay too early.
    auto beforeMoon=makeRequest("Azeroth",{1,2,3},directions,dark,true,(22*3600.+17*60+41.5-10)/86400.,true);
    auto minuteBefore=makeRequest("Azeroth",{},directions,dark,true,day(22,16),true);
    assert(!minuteBefore.active[0]&&!minuteBefore.active[1]);
    assert(!beforeMoon.active[0]&&beforeMoon.active[1]&&beforeMoon.directions[1].z==0);
    auto tooEarly=makeRequest("Azeroth",{},directions,dark,true,day(20,0),true);
    assert(!tooEarly.active[0]&&!tooEarly.active[1]);
    auto beforeSun=makeRequest("Kalimdor",{},directions,dark,true,(6*3600.+10*60+21)/86400.,false); // sunrise 06:10:31
    assert(beforeSun.active[0]&&!beforeSun.active[1]&&!beforeSun.allowLoads);
    auto midnight=makeRequest("Kalimdor",{},directions,dark,true,day(23,59),true);
    assert(!midnight.active[0]&&!midnight.active[1]);
    auto missing=makeRequest("Azeroth",{},directions,dark,false,day(20,12),true);
    assert(!missing.active[0]&&!missing.active[1]);
    // Forever's 95 -> 25 degree solar polar ramp crosses at 05:57:30.
    const double foreverDawn=(5*3600.+57*60+20)/86400.;
    auto forever=makeRequest("Azeroth",{},directions,dark,true,foreverDawn,true,true);
    auto native=makeRequest("Azeroth",{},directions,dark,true,foreverDawn,true,false);
    assert(forever.active[0]&&!forever.active[1]&&!native.active[0]);
    auto foreverNight=makeRequest("Northrend",{},directions,dark,true,(21*3600.+26*60+45)/86400.,true,true);
    assert(!foreverNight.active[0]&&foreverNight.active[1]);
    puts("PASS static request: unchanged active directions, horizon prewarm, midnight, invalid orbit, memory pressure");
}
