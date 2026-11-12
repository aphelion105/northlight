#include "expansion_atmosphere.h"
#include "celestial_context.h"
#include "settings_schema.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <limits>

int main(){
    using namespace NorthlightExpansionAtmosphere;
    unsigned cases=0;
    for(const auto& p:Profiles)for(unsigned minute=0;minute<1440;minute+=5)for(float storm:{0.f,.5f,1.f}){
        auto f=at(p.map,p.zone,float(minute)/1440,storm);assert(f.valid);
        float c[38][4];NorthlightForever::constants(f,c,1200,1);
        for(const auto& row:c)for(float v:row)assert(std::isfinite(v));
        const float ground=NorthlightForever::sigma(f,100,150,150),high=NorthlightForever::sigma(f,100,1150,150);
        assert(ground>0&&high>=0&&high<ground);
        assert(std::fabs(ground-NorthlightForever::sigma(f,100,3150,3150))<1e-7);
        if(storm==0){const auto wet=at(p.map,p.zone,float(minute)/1440,1);assert(NorthlightForever::sigma(wet,100,150,150)>ground);}
        ++cases;
    }
    auto density=[](const char* map,uint32_t zone){return NorthlightForever::sigma(at(map,zone,.5f,0),100,0,0);};
    assert(density("Expansion01",3521)>density("Expansion01",3518));
    assert(density("Northrend",3711)>density("Northrend",67));
    assert(density("Northrend",210)>density("Northrend",4395));
    assert(!at("Azeroth",3521,.5f,0).valid&&!at("Expansion01",3430,.5f,0).valid);
    assert(!at("Northrend",0,.5f,0).valid&&!at("Northrend",65,-1,0).valid);
    assert(!at("Northrend",65,std::numeric_limits<float>::quiet_NaN(),0).valid);
    // Cached parent-zone boundary: bilinear density, continuity, no camera-height dependence.
    NorthlightRegionalFog::Region region;NorthlightRegionalFog::Tile tile;tile.x=tile.y=32;
    for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x)tile.zones[y*16+x]=x<8?3518:3521;
    region.tiles.push_back(tile);
    float pos[3]={float(NorthlightRegionalFog::WorldZero-(512+8)*NorthlightRegionalFog::ChunkSize),float(NorthlightRegionalFog::WorldZero-(512+8)*NorthlightRegionalFog::ChunkSize),100};
    const auto middle=sample(region,"Expansion01",pos,.5f,0);
    const float expected=(density("Expansion01",3518)+density("Expansion01",3521))*.5f;
    assert(std::fabs(NorthlightForever::sigma(middle,100,0,0)-expected)<1e-5);
    pos[1]-=.001f;const auto right=sample(region,"Expansion01",pos,.5f,0);pos[1]+=.002f;const auto left=sample(region,"Expansion01",pos,.5f,0);
    assert(std::fabs(NorthlightForever::sigma(right,100,0,0)-NorthlightForever::sigma(left,100,0,0))<1e-6);

    using namespace NorthlightForeverSky;
    assert(std::fabs(evaluate(.5).sun.elevation-65)<1e-4);
    assert(std::fabs(evaluate(0).moon.elevation-55)<1e-4);
    assert(evaluate(19.95/24).sun.elevation<0&&NorthlightCelestialOrbit::evaluateNative(19.95/24).sun.elevation>0);
    assert(evaluate(21.5/24).moon.elevation>0&&NorthlightCelestialOrbit::evaluateNative(21.5/24).moon.elevation<0);
    assert(!evaluate(-1).valid&&!evaluate(1).valid&&!evaluate(NAN).valid);
    for(unsigned second=0;second<86400;second+=10){auto o=evaluate(double(second)/86400);assert(o.valid);
        for(const auto& b:{o.sun,o.moon}){float norm=0;for(float x:b.direction){assert(std::isfinite(x));norm+=x*x;}assert(std::fabs(norm-1)<1e-6);}
        assert(sample(SunScale,double(second)/86400)>=1&&sample(SunScale,double(second)/86400)<=2);
        assert(sample(MoonScale,double(second)/86400)>=1&&sample(MoonScale,double(second)/86400)<=1.5f);
    }
    const auto before=evaluate(1-1e-8),after=evaluate(0);assert(std::fabs(before.sun.direction[2]-after.sun.direction[2])<1e-6);
    assert(std::fabs(radius(std::atan(1.f/24),2)-std::atan(2.f/24))<1e-7);
    NorthlightCelestial::Snapshot snap;snap.dayFraction=.5f;
    for(auto& b:snap.bodies){b.center[0]=12;b.texture=1;b.size=.6f;b.baseSize=1;b.color=0xffffffff;}
    float camera[3]={},direct[3]={1,1,1};NorthlightCelestial::Context native,imported;
    assert(NorthlightCelestial::decode(snap,camera,direct,native));imported=native;
    NorthlightCelestial::resolveRendererSky(native,direct,false);
    assert(native.sun.direction[0]==1&&native.sun.angularRadius==native.sun.nativeAngularRadius);
    NorthlightCelestial::resolveRendererSky(imported,direct,true);
    assert(std::fabs(imported.sun.direction[2]-std::sin(65.f*3.14159265f/180))<1e-6);
    assert(std::fabs(imported.sun.angularRadius-std::atan(.506736f/24))<1e-7);
    assert(imported.sunWeight==1&&imported.moonWeight==0);
    const auto once=imported;NorthlightCelestial::resolveRendererSky(imported,direct,true);
    assert(imported.sun.angularRadius==once.sun.angularRadius); // never accumulate enlargement

    // Preserve the pre-native Northlight calibration, then apply only the
    // imported size curve. Native animation/base changes must not resize this mode.
    unsigned sizeCases=0;
    for(float nativeSize:{.3f,1.f,3.f})for(float baseSize:{.5f,1.f,1.75f})for(float day:{0.f,.25f,.5f,.875f}){
        snap.dayFraction=day;for(auto& b:snap.bodies){b.size=nativeSize;b.baseSize=baseSize;}
        assert(NorthlightCelestial::decode(snap,camera,direct,native));imported=native;
        NorthlightCelestial::resolveRendererSky(native,direct,false);
        assert(std::fabs(native.sun.angularRadius-std::atan(nativeSize/24))<1e-7);
        NorthlightCelestial::resolveRendererSky(imported,direct,true);
        const float sunScale=day==0||day==.25f||day==.875f?2.f:1.f;
        const float moonScale=day==0?1.f:1.5f;
        assert(std::fabs(imported.sun.angularRadius-std::atan(.506736f*sunScale/24))<1e-7);
        assert(std::fabs(imported.moon.angularRadius-std::atan(.46176318f*1.75f*moonScale/24))<1e-7);
        const auto fixed=imported;NorthlightCelestial::resolveRendererSky(imported,direct,true);
        assert(imported.sun.angularRadius==fixed.sun.angularRadius&&imported.moon.angularRadius==fixed.moon.angularRadius);
        ++sizeCases;
    }

    using namespace NorthlightSettings;
    auto docs=factoryDocuments();const auto active=configOf(docs,"");assert(active.quality.celestialStyle==0);
    auto fields=schema(docs,docs,active,"");const Field* style=nullptr;for(const auto& f:fields)if(f.key=="CelestialStyle")style=&f;
    assert(style&&style->kind=="enum"&&style->options=="Default,Forever"&&style->apply=="F");
    auto changed=applyChanges(docs,fields,style->id()+"\tForever\n");const auto next=configOf(changed,"");assert(next.quality.celestialStyle==1);
    assert(liveQualityUpdate(active.quality,next.quality).celestialStyle==1);
    assert(giRequestChanged(active.quality,next.quality,active.celestial,next.celestial));
    auto restored=applyChanges(changed,fields,style->id()+"\t@factory\n");assert(configOf(restored,"").quality.celestialStyle==0);
    bool rejected=false;try{applyChanges(docs,fields,style->id()+"\tunknown\n");}catch(...){rejected=true;}assert(rejected);
    for(const auto* text:{"Forever","forever","1"}){Document doc(std::string("[Quality]\nCelestialStyle=")+text);assert(qualityOf(doc,"").celestialStyle==1);}
    assert(canonicalOption("0","Default,Forever")=="Default"&&canonicalOption("1","Default,Forever")=="Forever");
    std::printf("PASS %u biome/time/weather cases, %u reused-size cases, zone seams, recovered orbit, scale, native mode and live settings\n",cases,sizeCases);
}
