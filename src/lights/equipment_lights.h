#pragma once
#include "world_local_lights.h"
#include "local_light_selection.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace NorthlightEquipmentLights {
inline constexpr unsigned Capacity=8,MaxBanksPerFrame=16;
inline constexpr float LifeSeconds=.25f,FadeSeconds=.10f,MaxDistance=48.f;
struct Native {float position[3]{},color[3]{},attenuation[3]{};};
// c17..20 = diffuse, c21..24 = VIEW-space position, c25..27 = the
// constant/linear/quadratic denominator. Layout is admitted only by the proof.
inline bool decode(const float* bank,unsigned slot,const float* inverseView,const float* camera,Native& out){
    if(!bank||!inverseView||!camera||slot>=4)return false;Native n;
    for(unsigned i=0;i<16;++i)if(!std::isfinite(inverseView[i]))return false;
    for(unsigned i=0;i<3;++i){const float p=bank[4*(4+slot)+i];if(!std::isfinite(p)||std::fabs(p)>100000)return false;
        n.color[i]=bank[4*slot+i];n.attenuation[i]=bank[4*(8+i)+slot];
        if(!std::isfinite(n.color[i])||n.color[i]<0||n.color[i]>16||!std::isfinite(n.attenuation[i])||n.attenuation[i]<0||n.attenuation[i]>10000)return false;}
    if(std::max({n.color[0],n.color[1],n.color[2]})<.01f||n.attenuation[0]+n.attenuation[1]+n.attenuation[2]<.001f)return false;
    float d2=0;for(unsigned i=0;i<3;++i){n.position[i]=inverseView[12+i];for(unsigned k=0;k<3;++k)n.position[i]+=bank[4*(4+slot)+k]*inverseView[4*k+i];
        if(!std::isfinite(camera[i])||!std::isfinite(n.position[i])||std::fabs(n.position[i])>=100000)return false;const float d=n.position[i]-camera[i];d2+=d*d;}
    if(d2>MaxDistance*MaxDistance)return false;out=n;return true;
}
inline float distance2(const float* a,const float* b){float d=0;for(unsigned k=0;k<3;++k){const float v=a[k]-b[k];d+=v*v;}return d;}
inline bool hueClose(const float* a,const float* b){const float ap=std::max({a[0],a[1],a[2]}),bp=std::max({b[0],b[1],b[2]});
    if(!(ap>0&&bp>0))return false;for(unsigned k=0;k<3;++k)if(std::fabs(a[k]/ap-b[k]/bp)>.08f)return false;return true;}
inline bool staticDuplicate(const Native& n,const std::vector<NorthlightLocalLights::Light>& lights){
    for(const auto& l:lights)if(distance2(n.position,l.position)<.25f*.25f&&NorthlightLocalLights::valid(l)&&hueClose(n.color,l.diffuse))return true;
    return false; // Conservative suppression: a lamp already represented by FGL is never added twice, even with a different falloff fit.
}
// Fit the native denominator to the existing compact linear-light batch.
// A range of at most 12 yards and peak <= 1 bound this experimental addition;
// this is an artistic local-light approximation, not an exact native falloff.
inline NorthlightLocalLights::Light light(const Native& n,std::uint64_t id,float gain){
    NorthlightLocalLights::Light l{};const float a=n.attenuation[0],b=n.attenuation[1],c=n.attenuation[2],target=16.f;
    const float q=std::max(0.f,target-a);float reach=12.f;if(q==0)reach=2.f;else if(c>.0001f)reach=2*q/(b+std::sqrt(b*b+4*c*q));else if(b>.0001f)reach=q/b;
    reach=std::clamp(reach,2.f,12.f);const float peak=std::max({n.color[0],n.color[1],n.color[2]}),energy=std::min(1.f,peak/std::max(.5f,a+b+c));
    for(unsigned k=0;k<3;++k){l.position[k]=n.position[k];l.diffuse[k]=n.color[k]/peak*energy*gain;}
    l.attenuationStart=std::min(1.f,reach*.2f);l.attenuationEnd=reach;l.sourceId=0xfec0000000000000ull|id;l.kind=2;return l;
}
struct Tracker {
    struct Entry {Native source;std::uint64_t id=0;std::uint32_t seen=0;float gain=0;};
    std::array<Entry,Capacity> entries{};unsigned count=0,banks=0,observed=0,duplicates=0,dropped=0;std::uint64_t serial=0;std::uint32_t updated=0;
    std::array<float,44> lastBank{};bool bankKnown=false;
    std::array<NorthlightLocalLights::Light,Capacity> output{};unsigned outputCount=0;
    std::array<std::uint64_t,Capacity> suppressed{};unsigned suppressedCount=0;
    void reset(){count=banks=observed=duplicates=dropped=outputCount=suppressedCount=0;bankKnown=false;updated=0;}
    void nextFrame(){banks=observed=duplicates=dropped=0;bankKnown=false;}
    bool admit(const float* bank){if(bankKnown&&std::equal(lastBank.begin(),lastBank.end(),bank))return false;
        if(banks==MaxBanksPerFrame){++dropped;return false;}std::copy(bank,bank+44,lastBank.begin());bankKnown=true;++banks;return true;}
    void observe(const Native& n,std::uint32_t now,const std::vector<NorthlightLocalLights::Light>& fixed,const float* camera=nullptr){
        ++observed;if(staticDuplicate(n,fixed)){++duplicates;return;}
        unsigned at=count;float best=1.f;
        for(unsigned i=0;i<count;++i){const float d=distance2(n.position,entries[i].source.position);if(d<best&&hueClose(n.color,entries[i].source.color)){at=i;best=d;}}
        if(at==count){
            if(count==Capacity){
                unsigned worst=0;bool stale=false;float farthest=-1;
                for(unsigned i=0;i<count;++i){if(std::uint32_t(now-entries[i].seen)>=unsigned(LifeSeconds*1000)){worst=i;stale=true;break;}
                    if(camera){const float d=distance2(camera,entries[i].source.position);if(d>farthest){farthest=d;worst=i;}}}
                const float reach=camera?std::sqrt(distance2(camera,n.position))+1.f:0.f; // 1-yard incumbent bias avoids churn.
                if(!stale&&(!camera||!(reach*reach<farthest))){++dropped;return;}
                entries[worst]={n,++serial,now,0};
            }else entries[count++]={n,++serial,now,0};
        }
        else{entries[at].source=n;entries[at].seen=now;}
    }
    void publish(std::uint32_t now,const std::vector<NorthlightLocalLights::Light>* fixed=nullptr){
        const float dt=updated?std::min(float(std::uint32_t(now-updated))*.001f,.1f):0;updated=now;outputCount=suppressedCount=0;
        for(unsigned i=0;i<count;){auto& e=entries[i];const auto age=std::uint32_t(now-e.seen);const float target=age<100?1.f:std::clamp((LifeSeconds-float(age)*.001f)/(LifeSeconds-.1f),0.f,1.f);
            // Scene publication can add a static lamp after this entry was
            // observed. Drop it now, including the local selection's old tail.
            if(fixed&&staticDuplicate(e.source,*fixed)){suppressed[suppressedCount++]=0xfec0000000000000ull|e.id;++duplicates;entries[i]=entries[--count];continue;}
            const float step=dt/FadeSeconds;e.gain+=std::clamp(target-e.gain,-step,step);
            if(age>=unsigned(LifeSeconds*1000)&&e.gain<=.0001f){entries[i]=entries[--count];continue;}
            if(e.gain>.0001f)output[outputCount++]=light(e.source,e.id,e.gain);++i;}
    }
};
// The opposite publication transition: an FGL lamp disappears, but its former
// local-selection entry is fading. Its native replacement must not overlap it.
inline void replaceFadingDuplicates(NorthlightLocalLightSelection::Tracker& selection,const NorthlightLocalLights::Light* lights,unsigned count){
    for(unsigned n=0;n<count;++n)for(unsigned i=0;i<selection.count;){const auto& old=selection.entries[i];
        if(old.id!=lights[n].sourceId&&distance2(old.light.position,lights[n].position)<.25f*.25f&&hueClose(old.light.diffuse,lights[n].diffuse))selection.forget(old.id);
        else ++i;}
}
} // namespace NorthlightEquipmentLights
