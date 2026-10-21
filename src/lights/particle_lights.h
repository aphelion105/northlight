#pragma once
#include "equipment_lights.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

// Candidates are submitted for this frame only. No native-light TTL or
// selection fade is allowed to keep a retired particle source alive.
namespace NorthlightParticleLights {
using Light=NorthlightLocalLights::Light;
inline constexpr unsigned Capacity=8,Clusters=32,DrawLimit=32,TrianglesPerDraw=16,Tracks=16;
// Draws placed per frame (one triangle each). Only a placed draw within MaxDistance
// takes one of the DrawLimit sampling slots: distant particle systems drawn first
// (city torches, fountains) used to fill the slots and leave the player's weapon dark.
inline constexpr unsigned ProbeLimit=128;inline constexpr float MaxDistance=48.f;
// A particle source flashes per frame (lightning) and a sampled draw can miss a
// frame (cache or sample placement). Each source therefore follows its samples:
// a flash shows at once (a delayed rise swallowed one-frame flashes) and dims
// over ~ReleaseSeconds once its particles stop, instead of switching a
// several-yard light off between frames.
inline constexpr float ReleaseSeconds=.12f,FollowSeconds=.05f,MinEnergy=.003f;
struct Sample {float position[3]{},rgb[3]{};};
struct Cluster {Sample sample;unsigned count=0;};
inline float peak(const float* rgb){return std::max({rgb[0],rgb[1],rgb[2]});}
inline bool same(const Sample& a,const Sample& b,float radius=1.25f){
    return NorthlightEquipmentLights::distance2(a.position,b.position)<radius*radius&&NorthlightEquipmentLights::hueClose(a.rgb,b.rgb);
}
inline bool triangleIndices(unsigned primitive,std::uint64_t triangle,std::uint64_t (&out)[3]){
    if(triangle>UINT32_MAX)return false;
    if(primitive==4){out[0]=triangle*3;out[1]=out[0]+1;out[2]=out[0]+2;}
    else if(primitive==5){out[0]=triangle;out[1]=triangle+1;out[2]=triangle+2;}
    else if(primitive==6){out[0]=0;out[1]=triangle+1;out[2]=triangle+2;}
    else return false;return out[2]<=UINT32_MAX;
}
struct Draw {
    bool indexed=false;std::int32_t base=0;unsigned minimum=0,vertices=0,first=0;
    bool vertex(std::uint64_t relative,std::uint32_t index,std::uint64_t& result)const{
        if(!indexed){result=std::uint64_t(first)+relative;return result<=UINT32_MAX;}
        if(!vertices||index<minimum||std::uint64_t(index)>=std::uint64_t(minimum)+vertices)return false;
        const auto value=std::int64_t(base)+index;if(value<0||std::uint64_t(value)>UINT32_MAX)return false;
        result=std::uint64_t(value);return true;
    }
};
inline void add(Cluster& cluster,const Sample& sample){
    const float scale=1.f/float(cluster.count+1);
    for(unsigned k=0;k<3;++k){cluster.sample.position[k]+=(sample.position[k]-cluster.sample.position[k])*scale;
        cluster.sample.rgb[k]+=(sample.rgb[k]-cluster.sample.rgb[k])*scale;}
    ++cluster.count;
}
class Frame {
    std::array<Cluster,Clusters> current_{};unsigned count_=0;
    std::array<Light,Capacity> previous_{};unsigned previousCount_=0;std::uint64_t serial_=0;
    struct Track {float position[3]{},rgb[3]{},target[3]{};std::uint64_t id=0;bool seen=false;};
    std::array<Track,Tracks> tracks_{};unsigned trackCount_=0;
public:
    std::array<Light,Capacity> output{};unsigned outputCount=0;
    std::array<std::uint64_t,Capacity> immediate{};unsigned immediateCount=0;
    unsigned draws=0,backgroundDraws=0,accepted=0,deferred=0,triangles=0,points=0,dropped=0,probes=0,distant=0;
    unsigned shaderMiss=0,textureMiss=0,bufferMiss=0,stateMiss=0,geometryMiss=0;
    // stateMiss by the check that refused the draw (PARTICLELIGHT state=...).
    enum State {BlendOp,AlphaTest,ColorWrite,Depth,Sampler,Projection,States};
    std::array<unsigned,States> stateWhy{};
    void refuse(State why){++stateMiss;++stateWhy[why];}
    // Diagnostics: textures particle draws missed, kept until the PARTICLELIGHT line reads them.
    struct TextureMiss {const void* key=nullptr;unsigned seen=0,width=0,height=0,format=0,levels=0,pool=0,usage=0,type=0;};
    std::array<TextureMiss,8> textureMisses{};unsigned textureMissCount=0;
    bool wantsTextureMiss(const void* key)const{
        if(textureMissCount==textureMisses.size())return false;
        for(unsigned i=0;i<textureMissCount;++i)if(textureMisses[i].key==key)return false;return true;
    }
    void missTexture(const TextureMiss& miss){if(wantsTextureMiss(miss.key))textureMisses[textureMissCount++]=miss;}
    void reset(){count_=previousCount_=trackCount_=outputCount=immediateCount=0;nextFrame();}
    void nextFrame(){count_=0;draws=backgroundDraws=accepted=deferred=triangles=points=dropped=probes=distant=0;shaderMiss=textureMiss=bufferMiss=stateMiss=geometryMiss=0;stateWhy={};}
    bool takeProbe(){if(probes>=ProbeLimit){++dropped;return false;}++probes;return true;}
    bool takeDraw(bool particle=true){if(draws>=DrawLimit||(!particle&&backgroundDraws>=DrawLimit/2)){++dropped;return false;}++draws;if(!particle)++backgroundDraws;return true;}
    void observe(const Sample& sample,const float* camera){
        for(unsigned k=0;k<3;++k)if(!std::isfinite(sample.position[k])||std::fabs(sample.position[k])>=100000||
            !std::isfinite(sample.rgb[k])||sample.rgb[k]<0||sample.rgb[k]>16||!std::isfinite(camera[k]))return;
        if(peak(sample.rgb)<.003f||NorthlightEquipmentLights::distance2(sample.position,camera)>MaxDistance*MaxDistance)return;
        ++points;
        for(unsigned i=0;i<count_;++i)if(same(current_[i].sample,sample)){add(current_[i],sample);return;}
        unsigned at=count_;
        if(at==Clusters){
            float farthest=-1;for(unsigned i=0;i<count_;++i){const float distance=NorthlightEquipmentLights::distance2(current_[i].sample.position,camera);
                if(distance>farthest){farthest=distance;at=i;}}
            if(NorthlightEquipmentLights::distance2(sample.position,camera)>=farthest){++dropped;return;}
        }else ++count_;
        current_[at]={sample,1};
    }
    // dtSeconds 0 (a gap, a new map, the tests' single frames): every source shows its current samples at once.
    void publish(const Light* native,unsigned nativeCount,const std::vector<Light>& fixed,
                 const float* camera,NorthlightLocalLightSelection::Tracker& selection,float dtSeconds=0){
        const auto rate=[&](float seconds){return dtSeconds>0?1-std::exp(-dtSeconds/seconds):1.f;};
        const float fall=rate(ReleaseSeconds),follow=rate(FollowSeconds);
        for(unsigned t=0;t<trackCount_;++t)tracks_[t].seen=false;
        for(unsigned i=0;i<count_;++i){
            const auto& s=current_[i].sample;bool duplicate=false;
            for(const auto& l:fixed)if(NorthlightEquipmentLights::distance2(s.position,l.position)<.25f*.25f&&
                NorthlightEquipmentLights::hueClose(s.rgb,l.diffuse)){duplicate=true;break;}
            if(duplicate)continue;
            float target[3];for(unsigned k=0;k<3;++k)target[k]=s.rgb[k]*std::min(.7f,1.f/peak(s.rgb));
            Track* track=nullptr;float best=2.f*2.f;
            for(unsigned t=0;t<trackCount_;++t){auto& c=tracks_[t];const float d=NorthlightEquipmentLights::distance2(s.position,c.position);
                if(!c.seen&&d<best){track=&c;best=d;}} // nearest only: a flash's hue varies from frame to frame
            if(track){for(unsigned k=0;k<3;++k){track->position[k]+=(s.position[k]-track->position[k])*follow;track->target[k]=target[k];}}
            else{
                if(trackCount_<Tracks)track=&tracks_[trackCount_++];
                else{float weakest=INFINITY;for(auto& c:tracks_)if(!c.seen&&peak(c.rgb)<weakest){weakest=peak(c.rgb);track=&c;}
                    if(!track){++dropped;continue;}}
                *track={};track->id=0xfed0000000000000ull|++serial_;
                for(unsigned k=0;k<3;++k){track->position[k]=s.position[k];track->target[k]=target[k];}
            }
            track->seen=true;
        }
        std::array<Light,Tracks+Capacity> candidates{};unsigned count=0;
        for(unsigned t=0;t<trackCount_;){auto& c=tracks_[t];
            for(unsigned k=0;k<3;++k){const float goal=c.seen?c.target[k]:0.f;c.rgb[k]=goal>c.rgb[k]?goal:c.rgb[k]+(goal-c.rgb[k])*fall;}
            if(!c.seen&&peak(c.rgb)<MinEnergy){c=tracks_[--trackCount_];continue;}
            Light l{};for(unsigned k=0;k<3;++k){l.position[k]=c.position[k];l.diffuse[k]=c.rgb[k];}
            l.attenuationStart=.5f;l.attenuationEnd=2.f+4.f*std::sqrt(std::min(1.f,peak(c.rgb)));l.kind=2;l.sourceId=c.id;
            candidates[count++]=l;++t;
        }
        const unsigned particleCount=count;
        for(unsigned i=0;native&&i<nativeCount&&i<Capacity;++i){
            bool duplicate=false;for(unsigned j=0;j<particleCount;++j)if(
                NorthlightEquipmentLights::distance2(native[i].position,candidates[j].position)<1.f&&
                NorthlightEquipmentLights::hueClose(native[i].diffuse,candidates[j].diffuse)){duplicate=true;selection.forget(native[i].sourceId);break;}
            if(!duplicate)candidates[count++]=native[i];
        }
        const auto score=[&](const Light& l){return std::sqrt(NorthlightEquipmentLights::distance2(l.position,camera))-l.attenuationEnd;};
        std::sort(candidates.begin(),candidates.begin()+count,[&](const Light& a,const Light& b){
            const float as=score(a),bs=score(b);return as!=bs?as<bs:a.sourceId<b.sourceId;});
        outputCount=std::min(count,Capacity);immediateCount=0;
        for(unsigned i=0;i<outputCount;++i){output[i]=candidates[i];
            if((output[i].sourceId&0xfff0000000000000ull)==0xfed0000000000000ull)immediate[immediateCount++]=output[i].sourceId;}
        for(unsigned i=0;i<previousCount_;++i)
            if(std::find(immediate.begin(),immediate.begin()+immediateCount,previous_[i].sourceId)==immediate.begin()+immediateCount)selection.forget(previous_[i].sourceId);
        previousCount_=0;for(unsigned i=0;i<outputCount;++i)if(
            std::find(immediate.begin(),immediate.begin()+immediateCount,output[i].sourceId)!=immediate.begin()+immediateCount)previous_[previousCount_++]=output[i];
    }
};
}
