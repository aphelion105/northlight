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
    // Within the 300 s prewarm before moonrise (20:45); 20:00 below stays too early.
    auto beforeMoon=makeRequest("Azeroth",{1,2,3},directions,dark,true,day(20,42),true);
    assert(!beforeMoon.active[0]&&beforeMoon.active[1]&&beforeMoon.directions[1].z==0);
    auto tooEarly=makeRequest("Azeroth",{},directions,dark,true,day(20,0),true);
    assert(!tooEarly.active[0]&&!tooEarly.active[1]);
    auto beforeSun=makeRequest("Kalimdor",{},directions,dark,true,day(6,7),false);
    assert(beforeSun.active[0]&&!beforeSun.active[1]&&!beforeSun.allowLoads);
    auto midnight=makeRequest("Kalimdor",{},directions,dark,true,day(23,59),true);
    assert(!midnight.active[0]&&!midnight.active[1]);
    auto missing=makeRequest("Azeroth",{},directions,dark,false,day(20,12),true);
    assert(!missing.active[0]&&!missing.active[1]);
    puts("PASS static request: unchanged active directions, horizon prewarm, midnight, invalid orbit, memory pressure");
}
