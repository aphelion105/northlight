#include "zone_tuning.h"
#include <cassert>
#include <fstream>
#include <iostream>
using namespace NorthlightZoneTuning;
namespace CP=NorthlightCelestialProfiles;
static bool near(float a,float b,float eps=1e-5f){return std::fabs(a-b)<eps;}
static const char* kShipped=
    "[default]\nsun_fog_gain=4.3\nsun_fog_height_scale=2\nmoon_fog_gain=2.5\n"
    "[Kalimdor:215]\nsun_fog_gain=3.0\nsun_light_mix=0.4\n"
    "[Azeroth:12]\nsun_disc=1,0.5,0.25\n";
static bool parseText(const std::string& text,Overrides& out,std::string& error){std::istringstream in(text);return parse(in,out,error);}
static CP::Table shippedTable(){CP::Table t;std::string e;std::istringstream in(kShipped);assert(CP::parse(in,t,e));return t;}
int main(int argc,char** argv){
    assert(argc==2); // argv[1]: where to write the state JSON for the Python half
    // Shipped parser: explicit keys are recorded per zone, the [default] copies are not.
    auto shipped=shippedTable();
    assert(shipped.explicitKeys.at({"Kalimdor",215u}).count("sun_fog_gain")&&shipped.explicitKeys.at({"Kalimdor",215u}).count("sun_light_mix"));
    assert(!shipped.explicitKeys.at({"Azeroth",12u}).count("sun_fog_gain")&&shipped.explicitKeys.at({"Azeroth",12u}).count("sun_disc"));
    assert(near(shipped.at("Azeroth",12).fogGain[0],4.3f)&&near(shipped.at("Kalimdor",215).fogGain[0],3.f));
    // The shared key helper keeps the shipped ranges.
    {CP::Profile p;assert(!CP::assignKey(p,"moon_horizon_haze","2.5")&&near(p.horizonHaze[1],2.5f));
     assert(CP::assignKey(p,"sun_fog_gain","8.5")&&CP::assignKey(p,"sun_wat","1")&&CP::assignKey(p,"sun_light","0,0,0")&&CP::assignKey(p,"sun_disc","1,1"));}
    // A full overlay parses; # comments, blank lines and spaces around = are fine.
    Overrides ov;std::string error;
    const std::string good=
        "# header\n[tuning]\nenabled = 1   # on\n\n[default]\nsun_fog_gain = 5\n"
        "[Kalimdor:215]\nsun_disc = 1.00, 0.93, 0.80\nsun_light_mix = 0.3\nterrain_radius = 4096\nground_fog_day_scale = 1.5\nground_fog_night_scale = 0\nground_fog_day_add = 0.004\nground_fog_night_add = 0.05\n"
        "[Northrend:100]\nmoon_fog_gain=1\n";
    assert(parseText(good,ov,error)&&error.empty());
    assert(ov.enabled&&ov.fallback.count()==1&&ov.zones.size()==2&&ov.count()==1+7+1);
    assert(near(ov.zones.at({"Kalimdor",215u}).radius,4096)&&near(ov.zones.at({"Kalimdor",215u}).fogNight,0));
    // Every malformed input is rejected with a line number and keeps the previous overrides.
    const char* bad[]={
        "[tuning]\nenabled=2","[tuning]\nsun_fog_gain=1","[tuning]\n[tuning]","[bogus]","[Kalimdor]","[Mars:5]","[Kalimdor:0]","[Kalimdor:x]","[Kalimdor:215]\n[Kalimdor:215]",
        "sun_fog_gain=1","[default]\nsun_fog_gain=8.01","[default]\nsun_fog_height_scale=0.2","[default]\nsun_light=0,0,0","[default]\nsun_disc=1,2,3","[default]\nsun_disc=0.1,0.2",
        "[default]\nsun_disc_gain=4.1","[default]\nmoon_horizon_haze=-1","[default]\nsun_fog_gain=1\nsun_fog_gain=2","[default]\nterrain_radius=1000","[default]\nground_fog_day_scale=1","[default]\nground_fog_day_add=0.01","[Kalimdor:215]\nground_fog_day_add=0.0501","[Kalimdor:215]\nground_fog_night_add=-0.001","[Kalimdor:215]\nground_fog_night_add=x",
        "[Kalimdor:215]\nterrain_radius=927","[Kalimdor:215]\nterrain_radius=4097","[Kalimdor:215]\nground_fog_day_scale=4.1","[Kalimdor:215]\nground_fog_night_scale=-0.1",
        "[Kalimdor:215]\nsun_unknown=1","[Kalimdor:215]\nterrain_radius=nan","[Kalimdor:215]\nterrain_radius=1000 2000","[Kalimdor:215]\nterrain_radius","[Kalimdor:215]\nfoo=1",
    };
    for(const char* text:bad){Overrides keep=ov;std::string e;assert(!parseText(text,keep,e)&&!e.empty()&&e.rfind("line ",0)==0);assert(keep.count()==ov.count()&&keep.zones.size()==ov.zones.size());}
    {Overrides tmp;std::string e;assert(!parseText("[Kalimdor:215]\nterrain_radius=1\n",tmp,e)&&e=="line 2: invalid numeric value");}
    {Overrides empty;assert(parseText("",empty,error)&&empty.enabled&&empty.count()==0);}
    // Celestial: [default] reaches the fallback and every zone value the zone did not set itself; zone keys go on top.
    auto eff=applyCelestial(shipped,ov);
    assert(near(eff.fallback.fogGain[0],5.f)&&near(eff.fallback.fogGain[1],2.5f));
    assert(near(eff.at("Azeroth",12).fogGain[0],5.f));                                          // inherited the default: follows it
    assert(near(eff.at("Azeroth",12).disc[0][1],.5f)&&near(eff.at("Azeroth",12).fogHeightScale[0],2.f)); // its own key and untouched keys stay
    assert(near(eff.at("Kalimdor",215).fogGain[0],3.f));                                         // set by the zone itself: the default does not win
    assert(near(eff.at("Kalimdor",215).disc[0][1],.93f)&&near(eff.at("Kalimdor",215).mix[0],.3f)); // zone override beats the shipped zone value
    assert(near(eff.at("Northrend",100).fogGain[1],1.f)&&near(eff.at("Northrend",100).fogGain[0],5.f)); // zone without a shipped section: effective fallback + override
    assert(near(eff.at("Northrend",101).fogGain[0],5.f));                                        // unknown zones use the effective fallback
    assert(near(shipped.at("Azeroth",12).fogGain[0],4.3f)&&near(shipped.fallback.fogGain[0],4.3f)); // the shipped table is untouched
    {Overrides off=ov;off.enabled=false;auto same=applyCelestial(shipped,off);assert(near(same.fallback.fogGain[0],4.3f)&&near(same.at("Kalimdor",215).mix[0],.4f));}
    // Zone-only keys for the worker.
    assert(near(terrainRadius(ov,928,"Kalimdor",215),4096)&&near(terrainRadius(ov,1500,"Kalimdor",216),1500)&&near(terrainRadius(ov,928,"Azeroth",215),928));
    {float day=2,night=3;fogScale(ov,"Kalimdor",215,day,night);assert(near(day,3.004f)&&near(night,.05f)); // policy*scale+add
     day=2;night=3;fogScale(ov,"Kalimdor",216,day,night);assert(near(day,2)&&near(night,3));
     Overrides off=ov;off.enabled=false;day=2;night=3;fogScale(off,"Kalimdor",215,day,night);assert(near(day,2)&&near(night,3));assert(near(terrainRadius(off,928,"Kalimdor",215),928));}
    {Overrides celestialOnly,other;std::string e;
     assert(parseText("[default]\nsun_fog_gain=1\n[Kalimdor:215]\nsun_light_mix=.5\n",celestialOnly,e)&&parseText("[Kalimdor:215]\nsun_light_mix=.1\n",other,e));
     assert(celestialOnly.sameWorker(other)&&!ov.sameWorker(celestialOnly));
     Overrides moved=ov;moved.zones.at({"Kalimdor",215u}).radius=2000;assert(!ov.sameWorker(moved));Overrides added=ov;added.zones.at({"Kalimdor",215u}).addDay=.005f;assert(!ov.sameWorker(added));Overrides off=ov;off.enabled=false;assert(!ov.sameWorker(off));
     Overrides extra=ov;extra.zones[{"Azeroth",12u}].celestial.emplace_back("sun_fog_gain","2");assert(ov.sameWorker(extra)&&extra.sameWorker(ov)); // a celestial-only zone is no worker change
     extra.zones[{"Azeroth",12u}].fogDay=2;assert(!ov.sameWorker(extra)&&!extra.sameWorker(ov));}
    // State JSON.
    State s;s.build="dev \"tuning\"";s.tick=123456;s.inWorld=true;s.map="Kalimdor";s.zone=215;s.camera[0]=-1234.5f;s.camera[1]=7;s.camera[2]=std::nanf("");s.night=.25f;
    s.enabled=true;s.loaded=ov.count();s.revision=3;s.builtRevision=2;s.error="line 2: bad\nthing";
    auto ranges=NorthlightRegionalShadow::Table();ranges.zones[{"Kalimdor",215u}]=1500;
    s.base=baseValues(shipped,ranges,"Kalimdor",215);s.effective=effectiveValues(s.base,eff,ov,"Kalimdor",215);s.terrainReach=2048;s.groundFogActive=true;
    assert(groundFogActive(10)&&groundFogActive(33)&&!groundFogActive(0)&&!groundFogActive(215)&&!groundFogActive(440)&&!groundFogActive(1519)); // Tanaris, Mulgore, Stormwind: no ground fog
    assert(near(s.base.terrainRadius,1500)&&near(s.base.fogDay,1)&&near(s.effective.terrainRadius,4096)&&near(s.effective.fogDay,1.5f)&&near(s.effective.fogNight,0)&&near(s.base.fogDayAdd,0)&&near(s.effective.fogDayAdd,.004f)&&near(s.effective.fogNightAdd,.05f));
    assert(near(s.base.profile.fogGain[0],3.f)&&near(s.effective.profile.fogGain[0],3.f)&&near(s.effective.profile.mix[0],.3f)&&near(s.base.profile.mix[0],.4f));
    std::ofstream(std::string(argv[1]))<<stateJson(s);
    std::cout<<"zone tuning ok"<<std::endl;
}
