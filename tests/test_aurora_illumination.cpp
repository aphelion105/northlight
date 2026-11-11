#include "aurora_illumination.h"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace NorthlightAurora;
int main(){
    assert(weight("Northrend",true,.88,-.1f,-.1f)==1);
    for(const char* map:{static_cast<const char*>(nullptr),"","Azeroth","Kalimdor","Expansion01","northrend"})
        assert(weight(map,true,.88,-.1f,-.1f)==0);
    for(double day:{-.1,.2,.49,1.,std::numeric_limits<double>::quiet_NaN()})assert(weight("Northrend",true,day,-.1f,-.1f)==0);
    assert(weight("Northrend",false,.88,-.1f,-.1f)==0);
    for(float z:{0.f,.01f,1.1f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
        assert(weight("Northrend",true,.88,z,-.1f)==0);assert(weight("Northrend",true,.88,-.1f,z)==0);
    }
    float previous=0,maxStep=0,peak=0;unsigned active=0;double peakDay=0;bool green=false,purple=false;
    for(unsigned second=0;second<86400;++second){
        const double day=double(second)/86400;const auto orbit=NorthlightCelestialOrbit::evaluateNative(day);
        const auto out=style("Northrend",100,true,day);
        const float w=weight("Northrend",orbit.valid,day,orbit.sun.direction[2],orbit.moon.direction[2]);
        assert(out.enabled&&out.intensity==1&&out.revision);assert((out.gain>0)==(w>0));active+=out.gain>0;
        if(out.gain>0){assert(day>.5&&orbit.sun.direction[2]<0&&orbit.moon.direction[2]<0&&out.revision);
            green|=out.phase<.2f;purple|=out.phase>.6f;assert(out.phase>=0&&out.phase<=1);}
        if(w>peak){peak=w;peakDay=day;}
        assert(out.gain>=0&&out.gain<=4);if(second)maxStep=std::max(maxStep,std::fabs(out.gain-previous));previous=out.gain;
    }
    assert(active>3600&&active<3*3600&&green&&purple&&maxStep<.02f&&peak==1);
    assert(!style("Northrend",0,true,peakDay).enabled);
    assert(!style("Northrend",100,false,peakDay).enabled);
    auto normal=style("Northrend",100,true,peakDay),twice=style("Northrend",200,true,peakDay),clamped=style("Northrend",999,true,peakDay);
    assert(normal.enabled&&twice.enabled&&normal.gain*2==twice.gain&&twice.gain==clamped.gain);
    assert(normal.phase==twice.phase&&normal.revision!=twice.revision&&twice.revision==clamped.revision);
    assert(style("Northrend",0,true,peakDay).intensity==0);
    assert(normal.intensity==1&&twice.intensity==2);
    std::printf("PASS aurora shared style fullDay twilightSeconds=%u maxGainStep=%.9f palettes=2 nativeAllDay=1 nativeOffAtZero=1\n",active,maxStep);
}
